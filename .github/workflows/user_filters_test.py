#!/usr/bin/env python3
"""Render the filters of a user filter file (user_filters.gmic) and check their output (#168).

The file is installed as ~/.config/gmic/gmic_ofx.gmic in a temporary HOME, or with --update-file
as G'MIC's filter update file (update408.gmic for the Makefile's GMICVER 4.0.8, read with
OFX_GMIC_UPDATE_FILE=1, #180), which replaces the built-in filters, or with --user-source as the
user source ~/.gmic, whose filters and commands come after the built-in ones (#206). Its filters
call a command that the file defines ("Unknown command" before #168), a built-in command, and a
built-in command that the file replaces: each output must be the uniform color that the filter
fills it with. User File Persistent and User File Persistent Store need the _persistent variable (a
string, or an image from 'store') and the status that their preview command sets (#206): their
outputs show that the plug-in ran the preview command first, and gave both to the main
command. User File Run Variables fills it with what the plug-in gives G'MIC on every run, as
G'MIC-Qt ($_host, $_input_layers, $_output_mode, the input's layer name), and User File Five
Channels returns 5 channels, which must fail the render (#206). The file also defines a command
that the plug-in hot-fixes, which must be kept, with a note. Its User File Cpus filter fills its
output with G'MIC's $_cpus, rendered without OMP_NUM_THREADS so that the plug-in sets it (#188):
the value, printed, must be the number of CPUs (hostDemo runs one render, whose fair share is all
the hardware threads). This shows that the count reaches G'MIC, not that the plug-in's prefix is
applied: the value is also G'MIC's own default.

With --stdlib, the same filters are then rendered with a copy of the whole built-in standard
library before them in the file, as in a user's copied gmic_stdlib.gmic: the outputs must be the
same, without another hot-fix note or a warning (a copy of the built-in commands still gets the
hot fixes), and the render times with both files are printed (G'MIC parses the file on every
render).

User File Render Scale, whose preview factor ends with "+", is rendered at render scales 1 and 0.5
(#179), on an input that hostDemo makes in canonical coordinates. At 0.5, the output must be half
the size of the scale-1 output, with the same region of definition, and G'MIC must get the same
percentages for a point (the plug-in converts it with the render scale) and a preview area of the
image's size. This checks the scaled render, not the rule that picks the filters that accept a
render scale: hostDemo is not Natron, and the plug-in accepts every render scale there.

usage: user_filters_test.py --host <hostDemo, or the CMake build directory> [--config <config>]
                            --plugins <dir with GMIC.ofx.bundle>
                            [--stdlib <gmic_stdlib_community.h>] [--runs N] [--work <dir>]
                            [--update-file | --user-source]
"""
import argparse
import json
import os
import re
import shutil
import sys
import tempfile

sys.dont_write_bytecode = True  # no __pycache__ for render_plugins in the checkout
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import render_plugins  # pylint: disable=wrong-import-position

HERE = os.path.dirname(os.path.abspath(__file__))
CONTEXT = 'OfxImageEffectContextFilter'
# plug-in id: the RGB color of its output (G'MIC's 0..255 values, divided by 255)
EXPECTED = {
    'eu.gmic.UserFileOwnCommand': (0.25, 0.25, 0.25),
    'eu.gmic.UserFileBuiltinCommand': (0.5, 0.0, 1.0),
    'eu.gmic.UserFileOverride': (128 / 255.0,) * 3,
    # the main command's result, with the preview's _persistent (red) and status (green, blue;
    # #206), and with an image stored in _persistent
    'eu.gmic.UserFilePersistent': (0.75, 0.5, 0.3),
    'eu.gmic.UserFilePersistentStore': (0.25, 0.5, 0.75),
    'eu.gmic.UserFileRunVariables': (1.0, 10 / 255.0, 1.0),
}
TOLERANCE = 1e-4
DEFINED_BY = ' is defined by '  # the note of a kept definition of a hot-fixed command
KEPT_NOTE = "'star_tone'" + DEFINED_BY
HOTFIX_WARNING = 'Warning: hot fix'
TIMEOUT = 120
# rendered at render scales 1 and RENDER_SCALE (#179): its preview factor ends with "+"
SCALE_PLUGIN = 'eu.gmic.UserFileRenderScale'
RENDER_SCALE = 0.5
# its input, made by hostDemo in canonical coordinates (a file input's pixels are pixels at the
# render scale), and its point, in canonical coordinates: 25% from the left, 75% from the top
SCALE_ARGS = ['-i', 'Source=rod:0,0,160,120', '-s', 'Spot=40,30']
SCALE_EXPECTED = (0.25, 0.75, 1.0)
CPUS_PLUGIN = 'eu.gmic.UserFileCpus'  # its output is G'MIC's $_cpus (divided by 255)
# its result has 5 channels: the render must fail with this message
CHANNELS_PLUGIN = 'eu.gmic.UserFileFiveChannels'
CHANNELS_ERROR = 'has 5 channels (should be at most 4)'
USER_SOURCE = '.gmic'  # (install_user_file): the user source, ~/.gmic


def update_file_name():
    """Returns the name of G'MIC's update file for the Makefile's GMICVER.

    Returns:
        update<major><minor><patch>.gmic, e.g. update408.gmic for G'MIC 4.0.8.

    Raises:
        ValueError: the Makefile has no GMICVER=<major>.<minor>.<patch> line.
    """
    with open(os.path.join(HERE, '..', '..', 'Makefile'), encoding='utf-8') as f:
        match = re.search(r'^GMICVER\s*=\s*(\d+)\.(\d+)\.(\d+)\s*$', f.read(), re.MULTILINE)
    if not match:
        raise ValueError('no GMICVER=<major>.<minor>.<patch> in the Makefile')
    return 'update%s.gmic' % ''.join(match.groups())


def install_user_file(work, text, name):
    """Makes work/home/.config/gmic hold only a filter file with the given name and text.

    Args:
        work: the work directory (render_plugins.render() sets HOME to work/home).
        text: the contents of the filter file.
        name: its name: gmic_ofx.gmic, G'MIC's update file (update_file_name()), or USER_SOURCE
            for the user source, work/home/.gmic, which the plug-in reads after the built-in
            filters (then work/home/.config/gmic is left empty).
    """
    home = os.path.join(work, 'home')
    rc_dir = os.path.join(home, '.config', 'gmic')
    shutil.rmtree(rc_dir, ignore_errors=True)
    os.makedirs(rc_dir)
    path = os.path.join(home, '.gmic') if name == USER_SOURCE else os.path.join(rc_dir, name)
    with open(path, 'w', encoding='latin-1', newline='') as f:
        f.write(text)


def check_output(report, expected):
    """Checks that the first output of a render has every pixel of the expected color.

    Args:
        report: the parsed report.json of the render (hostDemo --report).
        expected: the RGB value of every output pixel.

    Returns:
        A list of problems, empty if the output is right.
    """
    renders = report.get('renders') or []
    output = (renders[0].get('output') if renders else None) or {}
    if not output.get('files'):
        return ['no output file in the report']
    width, height = output.get('w') or 0, output.get('h') or 0
    if width * height <= 0:
        return ['empty output (%sx%s)' % (width, height)]
    problems = []
    lows, highs = output.get('min') or [], output.get('max') or []
    for c, value in enumerate(expected):
        if c >= len(lows) or c >= len(highs) or lows[c] is None or highs[c] is None:
            problems.append('no channel %d in the output (%s)' % (c, output.get('comps')))
        elif abs(lows[c] - value) > TOLERANCE or abs(highs[c] - value) > TOLERANCE:
            problems.append('channel %d is in [%g, %g], expected %g'
                            % (c, lows[c], highs[c], value))
    return problems


def render_all(host, plugins, work, runs):
    """Renders every plug-in of EXPECTED runs times, and checks their outputs.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.
        runs: the number of renders of each plug-in.

    Returns:
        (problems, logs, seconds): a list of problems, the log of the first render of each
        plug-in, and the shortest render time of each plug-in (in seconds).
    """
    problems, logs, seconds = [], [], {}
    for pid in sorted(EXPECTED):
        for run in range(runs):
            _, _, problem, _, t, errors, log, _ = render_plugins.render(
                host, [], plugins, work, TIMEOUT, (pid, CONTEXT, ['Source']))
            if run == 0:
                logs.append(log)
            if problem:
                problems.append('%s: %s %s' % (pid, problem, ' '.join(errors)))
                break
            seconds[pid] = min(t, seconds.get(pid, t))
            report_path = os.path.join(work, pid[len('eu.gmic.'):], 'report.json')
            with open(report_path, encoding='utf-8') as f:
                report = json.load(f)
            problems += ['%s: %s' % (pid, p) for p in check_output(report, EXPECTED[pid])]
    return problems, logs, seconds


def check_cpus(host, plugins, work):
    """Renders CPUS_PLUGIN without OMP_NUM_THREADS, and prints and checks G'MIC's $_cpus.

    The value must be the number of CPUs: this catches a count that falls back to hostDemo's 1,
    but the plug-in's count for a single render is also G'MIC's own default.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if $_cpus is the number of CPUs.
    """
    result = render_plugins.render(
        host, [], plugins, work, TIMEOUT, (CPUS_PLUGIN, CONTEXT, ['Source']), host_threads=True)
    problem, errors = result[2], result[5]  # (independent of the number of results)
    if problem:
        return ['%s: %s %s' % (CPUS_PLUGIN, problem, ' '.join(errors))]
    report_path = os.path.join(work, CPUS_PLUGIN[len('eu.gmic.'):], 'report.json')
    with open(report_path, encoding='utf-8') as f:
        report = json.load(f)
    renders = report.get('renders') or []
    output = (renders[0].get('output') if renders else None) or {}
    lows, highs = output.get('min') or [None], output.get('max') or [None]
    if lows[0] is None or highs[0] is None or abs(highs[0] - lows[0]) > TOLERANCE:
        return ['%s: not a uniform output (%s, %s)' % (CPUS_PLUGIN, lows, highs)]
    cpus, ncpu = round(lows[0] * 255), os.cpu_count() or 1
    print("G'MIC's $_cpus in a render without OMP_NUM_THREADS: %d (%d CPUs)" % (cpus, ncpu))
    if cpus != ncpu:
        return ['%s: $_cpus is %d, not the number of CPUs (%d)' % (CPUS_PLUGIN, cpus, ncpu)]
    return []


def check_channels(host, plugins, work):
    """Renders CHANNELS_PLUGIN, whose render must fail with CHANNELS_ERROR.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if the render failed with that message.
    """
    result = render_plugins.render(
        host, [], plugins, work, TIMEOUT, (CHANNELS_PLUGIN, CONTEXT, ['Source']))
    problem, log = result[2], result[6]
    if not problem:
        return ['%s: rendered, expected a failure (%s)' % (CHANNELS_PLUGIN, CHANNELS_ERROR)]
    if CHANNELS_ERROR not in log:
        return ['%s: %s, without the message %r' % (CHANNELS_PLUGIN, problem, CHANNELS_ERROR)]
    return []


def render_scaled(host, plugins, work, scale):
    """Renders SCALE_PLUGIN at a render scale, on its SCALE_ARGS input and point.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.
        scale: the render scale.

    Returns:
        (problem, report): a problem (None if it rendered), and the first render of the parsed
        report (an empty dict if there is none).
    """
    result = render_plugins.render(host, ['--scale', str(scale)] + SCALE_ARGS, plugins, work,
                                   TIMEOUT, (SCALE_PLUGIN, CONTEXT, []))
    problem, errors = result[2], result[5]
    if problem:
        return '%s %s' % (problem, ' '.join(errors)), {}
    report_path = os.path.join(work, SCALE_PLUGIN[len('eu.gmic.'):], 'report.json')
    with open(report_path, encoding='utf-8') as f:
        report = json.load(f)
    renders = report.get('renders') or []
    return None, (renders[0] if renders else {})


def check_render_scale(host, plugins, work):
    """Renders SCALE_PLUGIN at scales 1 and RENDER_SCALE, and compares them (#179).

    Both outputs must be SCALE_EXPECTED, i.e. the same point percentages and a preview area of
    the image's size; at RENDER_SCALE, the output is the scale-1 size times RENDER_SCALE, with the
    same region of definition (canonical coordinates).

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if both renders are right.
    """
    problems, renders = [], {}
    for scale in (1, RENDER_SCALE):
        problem, render = render_scaled(host, plugins, work, scale)
        if problem:
            problems.append('at scale %s: %s' % (scale, problem))
            continue
        renders[scale] = render
        problems += ['at scale %s: %s' % (scale, p)
                     for p in check_output({'renders': [render]}, SCALE_EXPECTED)]
    if len(renders) == 2:
        full, scaled = renders[1], renders[RENDER_SCALE]
        out1, out2 = full.get('output') or {}, scaled.get('output') or {}
        width = round((out1.get('w') or 0) * RENDER_SCALE)
        height = round((out1.get('h') or 0) * RENDER_SCALE)
        if (out2.get('w'), out2.get('h')) != (width, height):
            problems.append('at scale %s: the output is %sx%s, expected %dx%d (%sx%s at scale 1)'
                            % (RENDER_SCALE, out2.get('w'), out2.get('h'), width, height,
                               out1.get('w'), out1.get('h')))
        rod1 = (full.get('regionOfDefinition') or {}).get('rod')
        rod2 = (scaled.get('regionOfDefinition') or {}).get('rod')
        if rod1 is None or rod2 != rod1:
            problems.append('at scale %s: the region of definition is %s, %s at scale 1'
                            % (RENDER_SCALE, rod2, rod1))
    return ['%s: %s' % (SCALE_PLUGIN, p) for p in problems]


def run_test(host, plugins, stdlib, runs, work, name):
    """Runs both renders of the test (see the module docstring).

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        stdlib: gmic_stdlib_community.h, or None to skip the copy of the standard library.
        runs: the number of renders of each plug-in with each file.
        work: the work directory.
        name: the name of the filter file (see install_user_file()).

    Returns:
        True if the test passed.
    """
    render_plugins.write_input(os.path.join(work, 'input.pfm'))
    with open(os.path.join(HERE, 'user_filters.gmic'), encoding='latin-1') as f:
        user_file = f.read()

    install_user_file(work, user_file, name)
    problems, logs, small = render_all(host, plugins, work, runs)
    problems += check_render_scale(host, plugins, work)
    problems += check_cpus(host, plugins, work)
    problems += check_channels(host, plugins, work)
    if not any(KEPT_NOTE in log for log in logs):
        problems.append("no note that the file's own definition of 'star_tone' is kept")
    for p in problems:
        print('FAIL (user file): ' + p)
    passed = not problems
    if not stdlib:
        return passed

    install_user_file(work, render_plugins.stdlib_text(stdlib) + '\n' + user_file, name)
    problems, logs, full = render_all(host, plugins, work, runs)
    for log in logs:
        problems += ['unexpected: ' + line.strip() for line in log.splitlines()
                     if (DEFINED_BY in line and KEPT_NOTE not in line) or HOTFIX_WARNING in line]
    for p in problems:
        print('FAIL (user file with a copy of the standard library): ' + p)
    passed = passed and not problems
    for pid in sorted(set(small) & set(full)):
        print('%s, shortest of %d renders: %.3f s with the user file, %.3f s with a copy of the '
              'standard library in it (%+.3f s)'
              % (pid, runs, small[pid], full[pid], full[pid] - small[pid]))
    return passed


def main():
    """Runs the test.

    Returns:
        The exit status: 0 if the test passed.
    """
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--host', required=True)
    parser.add_argument('--config', default='Release')
    parser.add_argument('--plugins', required=True)
    parser.add_argument('--stdlib')
    parser.add_argument('--runs', type=int, default=3)
    parser.add_argument('--work')
    parser.add_argument('--update-file', action='store_true',
                        help="install the file as G'MIC's update file, not as gmic_ofx.gmic")
    parser.add_argument('--user-source', action='store_true',
                        help='install the file as the user source ~/.gmic, read after the '
                             'built-in filters, not as gmic_ofx.gmic')
    args = parser.parse_args()
    if args.update_file and args.user_source:
        parser.error('--update-file and --user-source are exclusive')
    # G'MIC's folder (gmic::path_rc(), where the plug-in looks for the update file) is the HOME's
    # .config/gmic only without GMIC_PATH and XDG_CONFIG_HOME; the renders inherit this environment
    for var in ('GMIC_PATH', 'XDG_CONFIG_HOME', 'OFX_GMIC_UPDATE_FILE'):
        os.environ.pop(var, None)
    name = 'gmic_ofx.gmic'
    if args.update_file:
        name = update_file_name()
        os.environ['OFX_GMIC_UPDATE_FILE'] = '1'
    elif args.user_source:
        if os.name == 'nt':
            parser.error('--user-source: not on Windows (%USERPROFILE%\\user.gmic)')
        name = USER_SOURCE
    host = render_plugins.host_demo_path(args.host, args.config)
    if host is None:
        parser.error('no hostDemo-path.txt or hostDemo-path-%s.txt in %s'
                     % (args.config, args.host))
    if args.work:
        work = os.path.abspath(args.work)
        os.makedirs(work, exist_ok=True)
    else:
        work = tempfile.mkdtemp(prefix='user_filters.')
    try:
        passed = run_test(host, os.path.abspath(args.plugins), args.stdlib, args.runs, work, name)
    finally:
        if not args.work:
            shutil.rmtree(work, ignore_errors=True)
    if not passed:
        print('FAIL: see above' + (' (renders in %s)' % work if args.work else ''))
        return 1
    print('%s filters: %d rendered with the expected output' % (name, len(EXPECTED)))
    return 0


if __name__ == '__main__':
    sys.exit(main())
