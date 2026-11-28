#!/usr/bin/env python3
"""Render the G'MIC plug-ins with openfx's hostDemo, as a load-and-render test.

Each plug-in is rendered at frame 0 with its default parameters, on a small fixed test input
(160x120 float RGB with gradients, stripes and a rectangle: every input clip, the extra layers
included, gets the same image), in its own directory, which keeps the host's output (hostDemo.log),
with a timeout. The test fails on any load, describe, instance or render error, and on a missing
output (a crash is reported as "at exit" when hostDemo had written report.json and the outputs),
and on NaN pixels in an output, except for the plug-ins of KNOWN_NAN. A few plug-ins are skipped
(SKIP), are expected to fail with a given error (EXPECTED_FAIL), or have a longer timeout
(TIMEOUT), with the reason; the test also fails when an expected failure renders (XPASS), when a
plug-in of KNOWN_NAN renders without NaN (with --subset all; a warning otherwise), or when fewer
than --min-rendered plug-ins rendered (default: 100 with --subset pr, 700 with --subset all). It
warns when a listed plug-in, or a filter that uses the FFT, is not in the list of plug-ins, except
for COMMERCIAL builds (--stdlib is their standard library), which leave many out.

usage: render_plugins.py --host <hostDemo, or the CMake build directory> [--config <config>]
                         --plugins <dir with GMIC.ofx.bundle>
                         --list <plug-in list> [--subset pr|all] [--stdlib <gmic_stdlib_community.h>]
                         [--jobs N] [--timeout S] [--min-rendered N] [--work <dir>]
                         [--only <id>[,<id>...]] [--host-arg <arg>]... [--host-threads]
--host <build dir> reads hostDemo-path.txt there (written by CMake with GMIC_BUILD_TESTS=ON), or
hostDemo-path-<config>.txt with a multi-config generator (--config, default Release).
The plug-in list (one line per plug-in: id, context and input clips, tab-separated) is written
by describe_plugins.sh with PLUGIN_LIST=<file>. --subset pr (the default) renders a
representative subset: the filters whose command uses the FFT (found in the G'MIC standard
library given by --stdlib), those that run their preview command before their main command (the
list's "#previewFirst<tab><id>" lines, from the plug-in's log), the multi-layer filters, and every
30th plug-in (sorted by id). A render whose preview command, run first, failed, or whose preview
status didn't have one value per parameter (the plug-in's log says so), is a failure.
--only renders just the given plug-ins, also those in SKIP (e.g. the ones that download data),
and fails unless they all render. --host-arg passes an extra argument to hostDemo (repeatable),
e.g. --host-arg=--ofx14 (an OFX 1.4-only host).
hostDemo gets OMP_NUM_THREADS=2 unless it is set; with --host-threads, OMP_NUM_THREADS is unset,
so that the plug-in sets G'MIC's thread count (hostDemo's multithread suite reports 1 CPU, and the
plug-in's fair share gives the single render of a hostDemo process all the hardware threads).
"""
import argparse
import array
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
import typing
import zlib

# Plug-ins that can't render in a headless host, or that fail by design outside G'MIC-Qt
SKIP = {
    'eu.gmic.ToutInteractif': 'needs a display (x_paint)',
    'eu.gmic.GamesDemos': 'needs a display (interactive demos, e.g. x_pacman)',
    'eu.gmic.SamjSelectionHermiteSpline': 'needs a display (x_spline)',
    'eu.gmic.Comicbook': "needs a display ('This filter requires the X11 or GDI32 graphic library')",
    'eu.gmic.Lineart': "needs a display ('This filter requires the X11 or GDI32 graphic library')",
    'eu.gmic.PinkTestOperatorBianca': "runs the external executable 'pink.asf'",
    'eu.gmic.PinkOperator': "runs Pink's external executables (pink)",
    'eu.gmic.PinkOperatorBinary': "runs Pink's external executables (pink)",
    'eu.gmic.CImgNLMeans': "runs the external executable 'use_nlmeans'",
    'eu.gmic.CImgSkeleton': "runs the external executable 'use_skeleton'",
    # they download data at render time (G'MIC's input_cached, from gmic.eu, or another site),
    # which CI runners may not reach (UpscaleCNN2x failed on Windows)
    'eu.gmic.GmickyRoddy': 'downloads its mascot images (sp gmicky...: img/sample_*.png)',
    'eu.gmic.GenerateRandomPortrait': 'downloads its image (portrait: thispersondoesnotexist.com)',
    'eu.gmic.UpscaleCNN2x': 'downloads its network (gmic_scale2x_cnn.gmz)',
    'eu.gmic.DenoiseCNN': 'downloads its network (gmic_denoise_cnn.gmz)',
    'eu.gmic.AddGrain': 'downloads its grain images (gmic_grain.gmz)',
    'eu.gmic.LightLeaks': 'downloads its light leaks (gmic_lightleaks.gmz)',
    'eu.gmic.SampleImage': 'downloads its sample images (img/sample_*.png)',
    'eu.gmic.ColorPresets': 'downloads CLUTs (clut: gmic_cluts.gmz)',
    'eu.gmic.SimulateFilm': 'downloads CLUTs (clut: gmic_cluts.gmz)',
    'eu.gmic.SaveNoisePrint': 'two passes on 2 input images, the first one saves a noise print file',
    'eu.gmic.ReverseEngineerGradientMap': "export-only (G'MIC-Qt's Export button): fails on purpose with "
                                          "'This filter does nothing on host!'",
}
# Plug-ins that fail with their default parameters, checked with G'MIC 4.0.8 and the hot fixes of
# gmic_hotfixes.gmic: in G'MIC itself (also with the gmic command line), in the plug-in
# ("plug-in:"), or because of this test ("test:", e.g. every layer input is connected). Each entry
# is (the error that the plug-in's log must contain, the reason). An expected failure only counts
# if the render exits with an error status and its log contains that error: a crash, a timeout, a
# missing output or another error is a failure. A listed plug-in that renders is a failure too
# (XPASS), so that the list never goes stale: remove it from the list.
EXPECTED_FAIL = {
    # point defaults outside the input: the plug-in moves them into Source only when the user
    # connects or changes it (#78); a host that connects Source when it creates the instance, as
    # hostDemo does, keeps them relative to the project (720x576), not to the 160x120 Source
    'eu.gmic.KaleidoscopeReptorianPolar': ("Invalid Number - Variable 3",
                                           "plug-in (hosts that connect Source at creation): point default "
                                           "outside the input"),
    'eu.gmic.SolveMaze': ("Command 'minimal_path': Invalid argument '22.5%,-356%,0,427.5%,76%,0'",
                          "plug-in (hosts that connect Source at creation): point defaults outside the input"),
    # no file by default: its Filename (filein()) is empty
    'eu.gmic.ImportData': ("Unknown filename", "imports a file chosen by the user, none by default"),
    # filters for 1 or 2 layers, which get the 4 extra layers that this test connects
    'eu.gmic.Softlight': ("softlight: Select 1-2 images",
                          "test: afre_softlight takes 1 or 2 images, and gets the 5 connected layers"),
    'eu.gmic.DOFManipulation': ("This option is available for single layer only",
                                "test: its default input mode needs a single layer, and gets the 5 connected layers"),
}
# Plug-ins whose output has NaN values with their default parameters, as seen with G'MIC 4.0.8 in
# the full Linux run (--subset all): what is known. The counts are the same in every run (6 runs
# checked), so they are not from a race; their cause in the filters is not found yet. Any other plug-in with NaN values fails. A listed plug-in that
# renders without them fails with --subset all, so that the list never goes stale, and only gets
# a warning in the pr subsets, which run on other platforms and FFT libraries.
KNOWN_NAN = {
    'eu.gmic.MSPatchNR': '288 NaN values in G\'MIC\'s result, in every full Linux run',
    'eu.gmic.PosterizeRelief': '2 NaN values in G\'MIC\'s result, in every full Linux run',
    'eu.gmic.ShadedSegmentation': '38400 NaN values in G\'MIC\'s result (2 channels of every pixel), in every full '
                                  'Linux run',
    'eu.gmic.TargetColorSpot': '4 NaN values in G\'MIC\'s result, in every full Linux run',
}
# Slow plug-ins (minutes on a CI runner): their own timeout (seconds); not in the pr subset
TIMEOUT = {
    'eu.gmic.NonscapeWoods': 900,
}

# the warning of a render whose result doesn't fit the region of definition that the plug-in
# declared (#175: a size that depends on the pixels); listed, not a failure
GEOMETRY_MISMATCH = "doesn't match the output"
# the end of that warning when there was no prediction (#228), and the plug-in's log lines that
# tell why: the prediction ran out of time (a slow filter), or its probe failed in G'MIC (a filter
# that fails on the probe's gradient). Listed apart: the next frames get the result's size.
GEOMETRY_UNPREDICTED = 'Its size could not be predicted'
PREDICTION_TIMED_OUT = "measuring the result's size took more than"
PREDICTION_FAILED = "the run that measures the result's size failed"
# Plug-ins whose result's size is not predicted with their default parameters on the test's
# 160x120 input, in the full Linux run (#228), with the reason. Another one gets a warning, so that
# a filter that starts timing out stands out; a listed one that is predicted gets a warning too
# (remove it). Warnings, not failures: the timeouts depend on the runner's speed.
KNOWN_UNPREDICTED = {
    'eu.gmic.FloconSnowflakeLaser': 'timed out: a 1400x1400 generator, slower than the 10 s budget on a runner',
    'eu.gmic.Iris': 'timed out: an 800x800 generator (2x internal upscale), slower than the 10 s budget on a runner',
    'eu.gmic.NonscapeWoods': 'timed out: a 1920x1080 generator, about 3 minutes',
    'eu.gmic.UpscaleSolver2x': "failed: gcd_upscale_solver2x fails on the probe's gradient ('mask image is empty', "
                               "an upstream bug)",
}
ERROR_LINE = re.compile(r'\*\*\* Error|\bOfxMessageError\b|\bError\b|\berror:')
COMMERCIAL_MARKER = ' #  openfx-gmic : COMMERCIAL standard library'
FFT_COMMANDS = re.compile(r'(?<![A-Za-z0-9_])(fft|ifft|deconvolve|deconvolve_fft|convolve_fft|bandpass|watermark_fourier)(?![A-Za-z0-9_])')
# the plug-in's log lines (gmic_plugin.cpp: kPreviewFirstFailed, gmicCommand()) when the preview
# command that it runs before the main command fails, or when its status doesn't have one value
# per parameter: both are render failures
PREVIEW_FIRST_FAILED = 'run before the main command, failed'
STATUS_MISMATCH = "the preview's status has"
EVERY = 30
ID_PREFIX = 'eu.gmic.'


# a PFM file's header (read_pfm): its type, width, height and scale
PFM_HEADER = re.compile(rb'(P[Ff])\s+(\d+)\s+(\d+)\s+([-+0-9.eE]+)\s')
INPUT_WIDTH, INPUT_HEIGHT = 160, 120  # the size of the input image (write_input)


def write_input(path, w=INPUT_WIDTH, h=INPUT_HEIGHT):
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


def read_pfm(path):
    """Returns (width, height, channels, values) of a PFM file, or None if it can't be read."""
    try:
        with open(path, 'rb') as stream:
            data = stream.read()
    except OSError:
        return None
    # one whitespace byte after the scale: the pixels may start with bytes that look like more
    header = PFM_HEADER.match(data)
    if not header:
        return None
    width, height, channels = int(header.group(2)), int(header.group(3)), 3 if header.group(1) == b'PF' else 1
    size = width * height * channels * 4
    if len(data) - header.end() < size:
        return None
    values = array.array('f')
    values.frombytes(data[header.end():header.end() + size])
    if float(header.group(4)) > 0:  # big-endian
        values.byteswap()
    return width, height, channels, values


def stdlib_text(stdlib_header):
    """The text of the G'MIC standard library in stdlib_header (gmic_stdlib_community.h)."""
    with open(stdlib_header, encoding='latin-1') as f:
        src = f.read()
    body = src[src.index('data_gmic[] = {') + len('data_gmic[] = {'):]
    data = bytes(int(v) for v in re.findall(r'\d+', body[:body.index('};')]))
    nl1 = data.index(b'\n')
    nl2 = data.index(b'\n', nl1 + 1)
    m = re.search(rb'#(\d+)', data[nl1 + 1:nl2])  # CImg serialization: compressed size
    payload = data[nl2 + 1:]
    return (zlib.decompress(payload[:int(m.group(1))]) if m else payload).decode('latin-1').rstrip('\0')


def host_demo_path(host, config):
    """hostDemo's path: host itself, or read from a CMake build directory host (None if not found)."""
    host = os.path.abspath(host)
    if not os.path.isdir(host):
        return host
    for n in ('hostDemo-path.txt', 'hostDemo-path-%s.txt' % config):
        if os.path.exists(os.path.join(host, n)):
            with open(os.path.join(host, n)) as f:
                return f.read().strip()
    return None


def plugin_id(filter_name):
    """The plug-in id of a G'MIC filter, from its name in its '#@gui <name> :' line.

    The plug-in's rule (getUniqueId, Helpers/gmic_parser.cpp) on a plain name: main() warns about
    the FFT filters whose id is not a plug-in.
    """
    return ID_PREFIX + re.sub(r'[^A-Za-z0-9]', '', filter_name)


def fft_plugins(stdlib_header):
    """Ids of the filters whose command (or its _<command> helpers) uses the FFT."""
    text = stdlib_text(stdlib_header)
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
        gui = re.match(r'#@gui ([^:_][^:]*?)\s*:\s*([A-Za-z_][A-Za-z0-9_]*)', line)
        if not gui or gui.group(2) == '_none_':
            continue
        cmd = gui.group(2)
        body = bodies.get(cmd, '') + ''.join(v for k, v in bodies.items() if k.startswith('_' + cmd))
        if FFT_COMMANDS.search(body):
            ids.add(plugin_id(gui.group(1)))
    return ids


def _progress_problem(report):
    """The problem with the host's progress in a hostDemo report (None if there is none).

    Each progress (G'MIC's progress index) must be ended exactly once, and the last value given to
    the host must be in [0, 1].

    Args:
        report: the parsed report.json of a render (hostDemo --report).

    Returns:
        A description of the first problem found, or None.
    """
    for pr in report.get('progress', []):
        last = pr.get('lastValue')
        if pr.get('ends') != 1:
            return 'progress ended %s times instead of once' % pr.get('ends')
        if last is not None and not 0 <= last <= 1:
            return 'last progress value %s outside [0, 1]' % last
    return None


class RenderResult(typing.NamedTuple):
    """The result of render(), in this order (callers may also index it).

    Attributes:
        pid: the plug-in id.
        rc: hostDemo's exit status (negative: a signal), None if it timed out.
        problem: why the render failed, None if it rendered.
        nan: the number of NaN values in the outputs.
        seconds: the render time.
        errors: the first error lines of the log.
        log: hostDemo's output, with the plug-in's log.
        updates: the number of progress updates that the host got.
        timed_out: whether the render timed out.
        report: the parsed report.json (hostDemo --report), None if there is none.
    """
    pid: str
    rc: typing.Optional[int]
    problem: typing.Optional[str]
    nan: int
    seconds: float
    errors: typing.List[str]
    log: str
    updates: int
    timed_out: bool
    report: typing.Optional[dict]


def work_dir(work, pid):
    """The directory of the renders of plug-in pid, in the work directory work."""
    return os.path.join(work, pid[len(ID_PREFIX):])


def _outputs_written(d):
    """True if d has report.json and every output file it lists."""
    try:
        with open(os.path.join(d, 'report.json')) as f:
            report = json.load(f)
    except (OSError, ValueError):
        return False
    files = [f for r in report.get('renders', []) for f in ((r.get('output') or {}).get('files') or [])]
    return bool(files) and all(os.path.exists(os.path.join(d, f)) for f in files)


def render(host, host_args, plugins, work, timeout, plugin, host_threads=False, frame='0', extra_env=None,
           expected_log=()):
    """Renders a plug-in at frame 0 with hostDemo, in its own directory, and checks the render.

    Args:
        host: the path of hostDemo.
        host_args: more hostDemo arguments.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with input.pfm (the input of every clip) and home (HOME and
            USERPROFILE of the render).
        timeout: the timeout of the render, in seconds.
        plugin: (id, context, input clips), as in the plug-in list.
        host_threads: if True, render without OMP_NUM_THREADS (the plug-in sets G'MIC's thread
            count); else OMP_NUM_THREADS is 2 unless it is set.
        frame: the frame to render (hostDemo's --frames).
        extra_env: environment variables to set, or to remove (None), for this render only (after
            the ones that render() removes).
        expected_log: texts of log lines that the caller expects (e.g. a test of a status
            mismatch): those lines are no problem.

    Returns:
        A RenderResult.
    """
    pid, context, clips = plugin
    d = work_dir(work, pid)
    shutil.rmtree(d, ignore_errors=True)
    os.makedirs(d)
    cmd = [host] + host_args + ['--no-std-paths', '--no-cache', '--quiet', '--allow-unwritten', '--format', 'pfm',
           '-d', 'float', '-p', pid, '-c', 'general' if 'General' in context else 'filter',
           '--frames', frame, '-o', 'out',
           '--report', 'report.json']
    for c in clips:
        cmd += ['-i', c + '=file:../input.pfm']
    # the home folder (Windows: USERPROFILE), where filters with an empty folder parameter write
    # their files (e.g. Valeur Moyenne LCH), is in the work directory; so is G'MIC's folder
    # (gmic::path_rc(): HOME's .config/gmic, without GMIC_PATH and XDG_CONFIG_HOME), except on
    # Windows, where it is the runner's %APPDATA%/gmic (APPDATA is not changed)
    home = os.path.join(work, 'home')
    env = dict(os.environ, OFX_PLUGIN_PATH=plugins, HOME=home, USERPROFILE=home)
    for var in ('GMIC_PATH', 'XDG_CONFIG_HOME'):
        env.pop(var, None)
    # extra_env: variables to set, or to remove (None), for this render only
    for name, value in (extra_env or {}).items():
        if value is None:
            env.pop(name, None)
        else:
            env[name] = value
    if host_threads:
        env.pop('OMP_NUM_THREADS', None)
    else:
        env['OMP_NUM_THREADS'] = os.environ.get('OMP_NUM_THREADS', '2')
    start = time.time()
    timed_out = False
    try:
        p = subprocess.run(cmd, cwd=d, env=env, stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                           timeout=timeout)
        rc, log = p.returncode, p.stdout.decode('utf-8', 'replace')
    except subprocess.TimeoutExpired as e:
        rc, log, timed_out = None, (e.stdout or b'').decode('utf-8', 'replace'), True
    seconds = time.time() - start
    # the host's output (with the plug-in's log), kept for the failures' triage
    with open(os.path.join(d, 'hostDemo.log'), 'w', encoding='utf-8') as f:
        f.write(log)
    problem, nan, updates, report = None, 0, 0, None
    if timed_out:
        problem = 'timeout after %ds' % timeout
    elif rc < 0:
        # hostDemo writes report.json after the renders and before it unloads the plug-in: a
        # crash after it and all the output files were written is a crash at exit (#213)
        when = 'at exit' if _outputs_written(d) else 'during the render'
        problem = 'crash %s (signal %d)' % (when, -rc)
    elif rc > 0:
        problem = 'exit status %d' % rc
    else:
        try:
            with open(os.path.join(d, 'report.json'), encoding='utf-8') as f:
                report = json.load(f)
            outputs = [r.get('output') for r in report.get('renders', [])]
            if not outputs or any(not o or not o.get('files') for o in outputs):
                problem = 'no output image'
            else:
                for o in outputs:
                    for name in o['files']:
                        if not os.path.exists(os.path.join(d, name)):
                            problem = 'missing output file ' + name
                    nan += sum(o.get('nan') or 0) if isinstance(o.get('nan'), list) else (o.get('nan') or 0)
            updates = sum(pr.get('updates') or 0 for pr in report.get('progress', []))
            if problem is None:
                problem = _progress_problem(report)
        except (OSError, ValueError) as e:
            problem = 'no report: %s' % e
    # the log lines that the caller expects (e.g. a test of a status mismatch) are no problem
    checked = '\n'.join(l for l in log.splitlines() if not any(e in l for e in expected_log))
    if problem is None and PREVIEW_FIRST_FAILED in checked:
        problem = 'the preview command, run before the main command, failed'
    if problem is None and STATUS_MISMATCH in checked:
        problem = "the preview's status doesn't have one value per parameter"
    errors = [line for line in checked.splitlines()
              if ERROR_LINE.search(line) or PREVIEW_FIRST_FAILED in line or STATUS_MISMATCH in line][:3]
    return RenderResult(pid, rc, problem, nan, seconds, errors, log, updates, timed_out, report)


def main():
    """Runs the test (see the module docstring).

    Returns:
        The exit status: 0 if the test passed.
    """
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--host', required=True)
    parser.add_argument('--config', default='Release')
    parser.add_argument('--plugins', required=True)
    parser.add_argument('--list', required=True)
    parser.add_argument('--subset', choices=('pr', 'all'), default='pr')
    parser.add_argument('--stdlib')
    parser.add_argument('--jobs', type=int, default=2)
    parser.add_argument('--timeout', type=int, default=300)
    parser.add_argument('--min-rendered', type=int)
    parser.add_argument('--work')
    parser.add_argument('--only')
    parser.add_argument('--host-arg', action='append', default=[])
    parser.add_argument('--host-threads', action='store_true')
    args = parser.parse_args()

    host = host_demo_path(args.host, args.config)
    if host is None:
        parser.error('no hostDemo-path.txt or hostDemo-path-%s.txt in %s (configure with GMIC_BUILD_TESTS=ON)'
                     % (args.config, os.path.abspath(args.host)))
    min_rendered = args.min_rendered if args.min_rendered is not None else (700 if args.subset == 'all' else 100)
    commercial = False
    if args.stdlib:
        with open(args.stdlib, encoding='latin-1') as f:
            commercial = any(line.startswith(COMMERCIAL_MARKER) for line in f.readlines()[:20])
    plugins_dir = os.path.abspath(args.plugins)
    work = os.path.abspath(args.work) if args.work else tempfile.mkdtemp(prefix='render_plugins.')
    os.makedirs(os.path.join(work, 'home'), exist_ok=True)
    write_input(os.path.join(work, 'input.pfm'))

    listed = {}  # id: (id, context, input clips)
    preview_first = set()  # the plug-ins that run their preview command first
    with open(args.list, encoding='utf-8') as lst:
        for line in lst:
            fields = line.rstrip('\r\n').split('\t')
            if fields[0] == '#previewFirst' and len(fields) > 1:
                preview_first.add(fields[1])
            elif fields[0] and not fields[0].startswith('#'):
                listed[fields[0]] = (fields[0], fields[1] if len(fields) > 1 else 'filter', fields[2:])
    ids = sorted(listed)
    for pid in sorted(set(SKIP) | set(EXPECTED_FAIL) | set(TIMEOUT) | set(KNOWN_NAN)):
        if pid not in listed and not commercial:
            print('WARNING: %s is in the skip, expected-failure, timeout or NaN list, but is not a plug-in' % pid)
    if args.only:
        selected = sorted(set(args.only.split(',')))
        missing = [p for p in selected if p not in listed]
        if missing:
            parser.error('--only: not in the plug-in list: ' + ' '.join(missing))
        min_rendered = len(selected)
    elif args.subset == 'all':
        selected = ids
    else:
        if not args.stdlib:
            parser.error('--subset pr needs --stdlib')
        fft = fft_plugins(args.stdlib)
        for pid in sorted(fft - set(listed)):
            if not commercial:
                print('WARNING: %s uses the FFT, but is not a plug-in (a hidden filter, or another id '
                      'rule than the plug-in\'s?)' % pid)
        fft &= set(ids)
        multi = {p for p in ids if any(c.startswith('Layer ') for c in listed[p][2])}
        every = set(ids[::EVERY])
        first = preview_first & set(ids)
        # (the slow ones: --subset all only)
        selected = sorted((fft | first | multi | every) - set(TIMEOUT))
        print('subset: %d plug-ins (%d using the FFT, %d running their preview command first, '
              '%d multi-layer, every %dth of %d)'
              % (len(selected), len(fft), len(first), len(multi), EVERY, len(ids)))
    todo = [listed[p] for p in selected if args.only or p not in SKIP]
    skipped = len(selected) - len(todo)
    print('rendering %d plug-ins (%d skipped), %d at a time, in %s' % (len(todo), skipped, args.jobs, work))
    sys.stdout.flush()

    start = time.time()
    fails, xfails, xpasses, nans, stale_nans, times, mismatches, unpredicted = [], [], [], [], [], [], [], []
    rendered = 0
    rendered_ids = set()
    progressed = 0  # plug-ins that updated the host's progress
    with concurrent.futures.ThreadPoolExecutor(args.jobs) as ex:
        def run(plugin):
            return render(host, args.host_arg, plugins_dir, work, TIMEOUT.get(plugin[0], args.timeout), plugin,
                          host_threads=args.host_threads)

        for result in ex.map(run, todo):
            pid, problem = result.pid, result.problem
            times.append((result.seconds, pid))
            if len(times) % 25 == 0:
                print('... %d/%d (%.0fs)' % (len(times), len(todo), time.time() - start))
                sys.stdout.flush()
            if problem and pid in EXPECTED_FAIL:
                expected, reason = EXPECTED_FAIL[pid]
                # only an error exit with the documented G'MIC error: not a crash or a timeout
                if result.rc is not None and result.rc > 0 and expected in result.log:
                    xfails.append(pid)
                    continue
                problem = 'expected to fail with "%s" (%s), but: %s' % (expected, reason, problem)
            if problem:
                fails.append(pid)
                print('FAIL %s: %s' % (pid, problem))
                for e in result.errors:
                    print('    ' + e)
                continue
            rendered += 1
            rendered_ids.add(pid)
            if result.updates:
                progressed += 1
            if pid in EXPECTED_FAIL:
                xpasses.append(pid)
                print('XPASS %s: rendered, remove it from EXPECTED_FAIL (%s)' % (pid, EXPECTED_FAIL[pid][1]))
            if result.nan:
                nans.append((pid, result.nan))
            elif pid in KNOWN_NAN:
                stale_nans.append(pid)
            if GEOMETRY_UNPREDICTED in result.log:
                if PREDICTION_TIMED_OUT in result.log:
                    why = 'timed out'
                elif PREDICTION_FAILED in result.log:
                    why = 'failed'
                else:
                    why = 'see its log'
                unpredicted.append((pid, why))
            elif GEOMETRY_MISMATCH in result.log:
                mismatches.append(pid)
    print('%d plug-ins in %.0fs: %d rendered, %d failed, %d expected failures, %d skipped' % (
        len(todo), time.time() - start, rendered, len(fails), len(xfails), skipped))
    print('%d reported progress' % progressed)
    print('slowest: ' + ', '.join('%s %.0fs' % (p[len(ID_PREFIX):], t) for t, p in sorted(times)[-5:]))
    new_nans = [pid for pid, _ in nans if pid not in KNOWN_NAN]
    for pid, nan in nans:
        if pid in KNOWN_NAN:
            print('WARNING: %s: %d NaN values in its output (KNOWN_NAN: %s)' % (pid, nan, KNOWN_NAN[pid]))
        else:
            print('FAIL %s: %d NaN values in its output' % (pid, nan))
    if unpredicted:
        print('NOTE: %d plug-ins whose result\'s size was not predicted (#228: the first frame keeps Source\'s '
              'size, cropped or padded; the next renders with the same settings get the result\'s): %s'
              % (len(unpredicted), ' '.join('%s (%s)' % u for u in sorted(unpredicted))))
    for pid, why in sorted(unpredicted):
        if pid not in KNOWN_UNPREDICTED:
            print('WARNING: %s: its result\'s size was not predicted (%s), and it is not in KNOWN_UNPREDICTED'
                  % (pid, why))
    unpredicted_ids = {pid for pid, _ in unpredicted}
    for pid in sorted(set(KNOWN_UNPREDICTED) & (rendered_ids - unpredicted_ids)):
        print('WARNING: %s: its result\'s size was predicted: remove it from KNOWN_UNPREDICTED (%s)'
              % (pid, KNOWN_UNPREDICTED[pid]))
    if mismatches:
        print('WARNING: %d plug-ins whose result does not match their region of definition (#175, '
              'cropped or padded): %s' % (len(mismatches), ' '.join(sorted(mismatches))))
    status = 0
    if fails:
        print('FAIL: %d plug-ins failed to render: %s (logs and images in %s)' % (len(fails), ' '.join(fails), work))
        status = 1
    if xpasses:
        print('FAIL: %d expected failures rendered (XPASS), remove them from EXPECTED_FAIL: %s' % (
            len(xpasses), ' '.join(xpasses)))
        status = 1
    if new_nans:
        print('FAIL: %d plug-ins have NaN values in their output, fix them or add them to KNOWN_NAN: %s' % (
            len(new_nans), ' '.join(new_nans)))
        status = 1
    if stale_nans and args.subset == 'all':
        print('FAIL: %d plug-ins of KNOWN_NAN rendered without NaN values, remove them from KNOWN_NAN: %s' % (
            len(stale_nans), ' '.join(stale_nans)))
        status = 1
    elif stale_nans:
        print('WARNING: %d plug-ins of KNOWN_NAN rendered without NaN values here (the list is checked with '
              '--subset all): %s' % (len(stale_nans), ' '.join(stale_nans)))
    if rendered < min_rendered:
        print('FAIL: only %d plug-ins rendered, fewer than %d (--min-rendered)' % (rendered, min_rendered))
        status = 1
    return status


if __name__ == '__main__':
    sys.exit(main())
