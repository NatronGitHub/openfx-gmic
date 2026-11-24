#!/usr/bin/env python3
"""Render the filters of a user filter file (user_filters.gmic), and check their pixels.

The file is installed as ~/.config/gmic/gmic_ofx.gmic in a temporary HOME (render_plugins.py's
<work>/home), which replaces the built-in filters. Its filters call commands that the file
defines (#168: "Unknown command" before), a built-in command, and a built-in command that the
file replaces; each output must be the uniform color that the filter fills it with.

usage: user_filters_test.py --host <hostDemo, or the CMake build directory>
                            --plugins <dir with GMIC.ofx.bundle> [--work <dir>]
"""
import argparse
import os
import shutil
import struct
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
# plug-in id: the RGB color of its output (G'MIC's 0..255 values, divided by 255)
EXPECTED = {
    'eu.gmic.UserFileOwnCommand': (0.25, 0.25, 0.25),
    'eu.gmic.UserFileBuiltinCommand': (0.5, 0.0, 1.0),
    'eu.gmic.UserFileOverride': (128 / 255.0,) * 3,
}
TOLERANCE = 1e-4


def read_pfm(path):
    """Returns (width, height, channels, values) of a little- or big-endian PFM file."""
    with open(path, 'rb') as f:
        kind = f.readline().strip()
        width, height = (int(v) for v in f.readline().split())
        scale = float(f.readline())
        channels = 3 if kind == b'PF' else 1
        n = width * height * channels
        values = struct.unpack(('<' if scale < 0 else '>') + '%df' % n, f.read(4 * n))
    return width, height, channels, values


def check_output(work, pid, expected):
    """Returns a list of problems with the RGB output of plug-in pid."""
    path = os.path.join(work, pid[len('eu.gmic.'):], 'out.0l.pfm')
    if not os.path.exists(path):
        path = os.path.join(work, pid[len('eu.gmic.'):], 'out.0.pfm')
    try:
        _, _, channels, values = read_pfm(path)
    except (OSError, ValueError, struct.error) as e:
        return ['%s: cannot read its output: %s' % (pid, e)]
    if channels != 3:
        return ['%s: %d channels in %s, expected 3' % (pid, channels, path)]
    for c in range(3):
        bad = [v for v in values[c::3] if abs(v - expected[c]) > TOLERANCE]
        if bad:
            return ['%s: channel %d is %g (%d pixels), expected %g'
                    % (pid, c, bad[0], len(bad), expected[c])]
    return []


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--host', required=True)
    ap.add_argument('--plugins', required=True)
    ap.add_argument('--work')
    a = ap.parse_args()
    work = os.path.abspath(a.work) if a.work else tempfile.mkdtemp(prefix='user_filters.')
    rc_dir = os.path.join(work, 'home', '.config', 'gmic')
    os.makedirs(rc_dir, exist_ok=True)
    shutil.copy(os.path.join(HERE, 'user_filters.gmic'), os.path.join(rc_dir, 'gmic_ofx.gmic'))
    plugin_list = os.path.join(work, 'plugins.txt')
    with open(plugin_list, 'w', encoding='utf-8') as f:
        for pid in sorted(EXPECTED):
            f.write('%s\tOfxImageEffectContextFilter\tSource\n' % pid)
    rc = subprocess.call([sys.executable, os.path.join(HERE, 'render_plugins.py'),
                          '--host', a.host, '--plugins', a.plugins, '--list', plugin_list,
                          '--only', ','.join(sorted(EXPECTED)), '--work', work])
    if rc != 0:
        print('FAIL: the user file filters did not all render (see above)')
        return 1
    problems = [p for pid in sorted(EXPECTED) for p in check_output(work, pid, EXPECTED[pid])]
    for p in problems:
        print('FAIL: ' + p)
    if problems:
        return 1
    print('user file filters: %d rendered with the expected pixels' % len(EXPECTED))
    return 0


if __name__ == '__main__':
    sys.exit(main())
