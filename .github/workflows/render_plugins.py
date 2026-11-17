#!/usr/bin/env python3
"""Render the G'MIC plug-ins with openfx's hostDemo, as a load-and-render test.

Each plug-in is rendered at frame 0 with its default parameters, on a small fixed test input
(160x120 float RGB with gradients, stripes and a rectangle: every input clip, the extra layers
included, gets the same image), in its own directory, with a timeout. The test fails on any
load, describe, instance or render error, and on a missing output; NaN pixels in an output are
reported as warnings. A few plug-ins are skipped (SKIP), are expected to fail (EXPECTED_FAIL), or
have a longer timeout (TIMEOUT), with the reason; the script warns when one of them is no longer a
plug-in, or when an expected failure renders.

usage: render_plugins.py --host <hostDemo> --plugins <dir with GMIC.ofx.bundle>
                         --list <plug-in list> [--subset pr|all] [--stdlib <gmic_stdlib_community.h>]
                         [--jobs N] [--timeout S] [--work <dir>]
The plug-in list (one line per plug-in: id, context and input clips, tab-separated) is written
by describe_plugins.sh with PLUGIN_LIST=<file>. --subset pr (the default) renders a
representative subset: the filters whose command uses the FFT (found in the G'MIC standard
library given by --stdlib), the multi-layer filters, and every 30th plug-in (sorted by id).
"""
import argparse
import concurrent.futures
import json
import math
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import zlib

# Plug-ins that can't render in a headless host, or that fail by design outside G'MIC-Qt
SKIP = {
    'eu.gmic.ToutInteractif': 'needs a display (x_paint)',
    'eu.gmic.DiaporamaSlideshow': 'needs a display (samj_full_screen_display: resize to the screen size, 0x0 here)',
    'eu.gmic.PinkTestOperatorBianca': "runs the external executable 'pink.asf'",
    'eu.gmic.ReverseEngineerColorCurve': "export-only (G'MIC-Qt's Export button): fails on purpose with "
                                         "'This filter does nothing on host!'",
    'eu.gmic.ReverseEngineerGradientMap': "export-only (G'MIC-Qt's Export button): fails on purpose with "
                                          "'This filter does nothing on host!'",
}
# Plug-ins that fail with their default parameters in G'MIC itself (not in the plug-in), checked
# with G'MIC 4.0.7. Listed with the G'MIC error; a plug-in that renders again is reported (XPASS).
EXPECTED_FAIL = {
    'eu.gmic.Gleam': "softlight: 'Select image(s) with 1 or 3 channels' on an RGBA image (not reported upstream)",
    'eu.gmic.MorphologyPainting': "'Unknown command fx_morpho' (renamed fx_morphological): "
                                  "https://github.com/GreycLab/gmic-community/issues/299",
    'eu.gmic.LayerManipulation': "gcd_layers: 'name: Number of arguments (4) cannot be higher than the number of "
                                 "images in the selection (1)', with any number of layers (not reported upstream)",
    'eu.gmic.MontageMultiRows': "samj_montage_row: 'Invalid selection [,-1,-2]', with 4 or 5 layers "
                                "(not reported upstream)",
    # only on the small test input: they render at 720x576
    'eu.gmic.HalftoneShapes': "'Unknown command [-1]x0' on the 160x120 test input",
    'eu.gmic.SolveMaze': "minimal_path: 'Invalid argument' on the 160x120 test input",
}
# Slow plug-ins (minutes on a CI runner): their own timeout (seconds); not in the pr subset
TIMEOUT = {
    'eu.gmic.NonscapeWoods': 900,
}

FFT_COMMANDS = re.compile(r'(?<![A-Za-z0-9_])(fft|ifft|deconvolve|deconvolve_fft|convolve_fft|bandpass|watermark_fourier)(?![A-Za-z0-9_])')
EVERY = 30


def write_input(path, w=160, h=120):
    """Float RGB PFM, bottom row first: gradients, stripes and a red rectangle (edges)."""
    rows = []
    for y in range(h - 1, -1, -1):
        row = []
        for x in range(w):
            if 60 <= x < 100 and 40 <= y < 80:
                r, g, b = 0.9, 0.2, 0.1
            else:
                r, g = x / (w - 1), y / (h - 1)
                b = 0.5 + 0.5 * math.sin(x / 7.0) * math.cos(y / 5.0)
            row += (r, g, b)
        rows.append(struct.pack('<%df' % len(row), *row))
    with open(path, 'wb') as f:
        f.write(b'PF\n%d %d\n-1.0\n' % (w, h))
        f.write(b''.join(rows))


def fft_plugins(stdlib_header):
    """Ids of the filters whose command (or its _<command> helpers) uses the FFT."""
    src = open(stdlib_header, encoding='latin-1').read()
    body = src[src.index('data_gmic[] = {') + len('data_gmic[] = {'):]
    data = bytes(int(v) for v in re.findall(r'\d+', body[:body.index('};')]))
    nl1 = data.index(b'\n')
    nl2 = data.index(b'\n', nl1 + 1)
    m = re.search(rb'#(\d+)', data[nl1 + 1:nl2])  # CImg serialization: compressed size
    payload = data[nl2 + 1:]
    text = (zlib.decompress(payload[:int(m.group(1))]) if m else payload).decode('latin-1')
    defs, cur = {}, None
    for line in text.split('\n'):
        d = re.match(r'^([A-Za-z_][A-Za-z0-9_]*)\s*:(?!=)', line)
        if d and not line.startswith('#'):
            cur = d.group(1)
            defs.setdefault(cur, []).append(line[d.end():])
        elif cur and not line.startswith('#'):
            defs[cur].append(line)
    bodies = {k: '\n'.join(v) for k, v in defs.items()}
    ids = set()
    for line in text.split('\n'):
        g = re.match(r'#@gui ([^:_][^:]*?)\s*:\s*([A-Za-z_][A-Za-z0-9_]*)', line)
        if not g or g.group(2) == '_none_':
            continue
        cmd = g.group(2)
        b = bodies.get(cmd, '') + ''.join(v for k, v in bodies.items() if k.startswith('_' + cmd))
        if FFT_COMMANDS.search(b):
            ids.add('eu.gmic.' + re.sub(r'[^A-Za-z0-9]', '', g.group(1)))
    return ids


def render(host, plugins, work, timeout, plugin):
    pid, context, clips = plugin
    d = os.path.join(work, pid[len('eu.gmic.'):])
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    cmd = [host, '--no-std-paths', '--no-cache', '--quiet', '--allow-unwritten', '--format', 'pfm',
           '-d', 'float', '-p', pid, '-c', 'general' if 'General' in context else 'filter',
           '--frames', '0', '-o', 'out',
           '--report', 'report.json']
    for c in clips:
        cmd += ['-i', c + '=file:../input.pfm']
    env = dict(os.environ, OFX_PLUGIN_PATH=plugins, HOME=os.path.join(work, 'home'),
               OMP_NUM_THREADS=os.environ.get('OMP_NUM_THREADS', '2'))
    start = time.time()
    try:
        p = subprocess.run(cmd, cwd=d, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout)
        rc, log = p.returncode, p.stdout.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired as e:
        rc, log = 'timeout', (e.stdout or b'').decode('utf-8', 'replace')
    t = time.time() - start
    problem, nan = None, 0
    if rc != 0:
        problem = 'timeout after %ds' % timeout if rc == 'timeout' else 'exit status %s' % rc
    else:
        try:
            report = json.load(open(os.path.join(d, 'report.json')))
            outputs = [r.get('output') for r in report.get('renders', [])]
            if not outputs or any(not o or not o.get('files') for o in outputs):
                problem = 'no output image'
            else:
                for o in outputs:
                    for f in o['files']:
                        if not os.path.exists(os.path.join(d, f)):
                            problem = 'missing output file ' + f
                    nan += sum(o.get('nan') or 0) if isinstance(o.get('nan'), list) else (o.get('nan') or 0)
        except (OSError, ValueError) as e:
            problem = 'no report: %s' % e
    errors = [l for l in log.splitlines() if 'rror' in l][:3]
    return pid, problem, nan, t, errors


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', required=True)
    ap.add_argument('--plugins', required=True)
    ap.add_argument('--list', required=True)
    ap.add_argument('--subset', choices=('pr', 'all'), default='pr')
    ap.add_argument('--stdlib')
    ap.add_argument('--jobs', type=int, default=2)
    ap.add_argument('--timeout', type=int, default=300)
    ap.add_argument('--work')
    a = ap.parse_args()

    host = os.path.abspath(a.host)
    plugins_dir = os.path.abspath(a.plugins)
    work = os.path.abspath(a.work) if a.work else tempfile.mkdtemp(prefix='render_plugins.')
    os.makedirs(os.path.join(work, 'home'), exist_ok=True)
    write_input(os.path.join(work, 'input.pfm'))

    allp = {}
    for line in open(a.list, encoding='utf-8'):
        f = line.rstrip('\r\n').split('\t')
        if f and f[0]:
            allp[f[0]] = (f[0], f[1] if len(f) > 1 else 'filter', f[2:])
    ids = sorted(allp)
    for pid in sorted(set(SKIP) | set(EXPECTED_FAIL) | set(TIMEOUT)):
        if pid not in allp:
            print('WARNING: %s is in the skip or expected-failure list, but is not a plug-in' % pid)
    if a.subset == 'all':
        sel = ids
    else:
        if not a.stdlib:
            ap.error('--subset pr needs --stdlib')
        fft = fft_plugins(a.stdlib)
        multi = {p for p in ids if any(c.startswith('Layer ') for c in allp[p][2])}
        every = set(ids[::EVERY])
        sel = sorted(((fft & set(ids)) | multi | every) - set(TIMEOUT))  # slow ones: --subset all only
        print('subset: %d plug-ins (%d using the FFT, %d multi-layer, every %dth of %d)' % (
            len(sel), len(fft & set(ids)), len(multi), EVERY, len(ids)))
    todo = [allp[p] for p in sel if p not in SKIP]
    print('rendering %d plug-ins (%d skipped), %d at a time, in %s' % (len(todo), len(sel) - len(todo), a.jobs, work))
    sys.stdout.flush()

    start = time.time()
    fails, xfails, xpasses, nans, times = [], [], [], [], []
    with concurrent.futures.ThreadPoolExecutor(a.jobs) as ex:
        run = lambda p: render(host, plugins_dir, work, TIMEOUT.get(p[0], a.timeout), p)
        for pid, problem, nan, t, errors in ex.map(run, todo):
            times.append((t, pid))
            if len(times) % 25 == 0:
                print('... %d/%d (%.0fs)' % (len(times), len(todo), time.time() - start))
                sys.stdout.flush()
            if problem and pid in EXPECTED_FAIL:
                xfails.append(pid)
            elif problem:
                fails.append(pid)
                print('FAIL %s: %s' % (pid, problem))
                for e in errors:
                    print('    ' + e)
            elif pid in EXPECTED_FAIL:
                xpasses.append(pid)
            if not problem and nan:
                nans.append((pid, nan))
    print('%d rendered in %.0fs: %d failed, %d expected failures, %d skipped' % (
        len(todo), time.time() - start, len(fails), len(xfails), len(sel) - len(todo)))
    print('slowest: ' + ', '.join('%s %.0fs' % (p[len('eu.gmic.'):], t) for t, p in sorted(times)[-5:]))
    for pid, nan in nans:
        print('WARNING: %s: %d NaN values in its output' % (pid, nan))
    for pid in xpasses:
        print('WARNING: %s rendered, remove it from EXPECTED_FAIL (%s)' % (pid, EXPECTED_FAIL[pid]))
    if fails:
        print('FAIL: %d plug-ins failed to render: %s (logs and images in %s)' % (len(fails), ' '.join(fails), work))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main())
