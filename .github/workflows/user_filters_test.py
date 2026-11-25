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

User File Larger, User File Smaller and User File Shifted give results of another size or
position (#175): with Resize Mode Dynamic, the default, the output must have the result's size,
and the region of definition its size and position, from Source's top-left corner; with Resize
Mode Fixed (Inplace), User File Larger keeps Source's size and region of definition. The
plug-in predicts these sizes from small probes, exactly: no render may warn that the result
doesn't match the output, except User File Content Size, whose size depends on the pixels, and
User File Area Size, whose size isn't an affine function of the input's (Source's size).

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
import math
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
# (#175) the cases of check_geometry(): plug-in id, hostDemo arguments (a Source input that
# doesn't start at 0,0: OFFSET_SOURCE), the expected output size relative to Source's and its
# pos(x,y) (pixels, y down), the RGBA value of every output pixel (PADDED: the result in its
# top-left part, transparent black elsewhere, both checked through the minimum and maximum), and
# whether the render must warn that the result doesn't match the output
OFFSET_SOURCE = ['-i', 'Source=file:../input.pfm@20,10']
PADDED = 'padded'
GEOMETRY_CASES = [
    ('eu.gmic.UserFileLarger', [], (2.0, 1.5, 0, 0), (1.0, 0.0, 0.0, 1.0), False),
    ('eu.gmic.UserFileSmaller', [], (0.5, 0.25, 0, 0), (0.0, 1.0, 0.0, 1.0), False),
    ('eu.gmic.UserFileShifted', [], (1.0, 1.0, 10, 20), (0.0, 0.0, 1.0, 1.0), False),
    ('eu.gmic.UserFileLarger', OFFSET_SOURCE, (2.0, 1.5, 0, 0), (1.0, 0.0, 0.0, 1.0), False),
    ('eu.gmic.UserFileShifted', OFFSET_SOURCE, (1.0, 1.0, 10, 20), (0.0, 0.0, 1.0, 1.0), False),
    # Fixed (Inplace): Source's rectangle, cropped, padded, pos() ignored
    ('eu.gmic.UserFileLarger', ['-s', 'Resize Mode=0'], (1.0, 1.0, 0, 0), (1.0, 0.0, 0.0, 1.0), False),
    ('eu.gmic.UserFileSmaller', ['-s', 'Resize Mode=0'], (1.0, 1.0, 0, 0), PADDED, False),
    ('eu.gmic.UserFileShifted', ['-s', 'Resize Mode=0'], (1.0, 1.0, 0, 0), (0.0, 0.0, 1.0, 1.0), False),
    # Downsample 1/2: resized to Source's size
    ('eu.gmic.UserFileLarger', ['-s', 'Resize Mode=2'], (1.0, 1.0, 0, 0), (1.0, 0.0, 0.0, 1.0), False),
    # the probes' size (40x60), and the warning
    ('eu.gmic.UserFileContentSize', [], (0.25, 0.5, 0, 0), (1.0, 1.0, 0.0, 1.0), True),
    # not affine: Source's size, and the warning
    ('eu.gmic.UserFileAreaSize', [], (1.0, 1.0, 0, 0), (0.0, 1.0, 1.0, 1.0), True),
    # Output Layer "Frame" at frame 0: result [0]
    ('eu.gmic.UserFileFrames', ['-s', 'Output Layer=11'], (0.5, 0.5, 5, 6), (1.0, 0.0, 1.0, 1.0), False),
]
GEOMETRY_WARNING = "doesn't match the output"
# the plug-in's notes when it refines a prediction with thin probes, or can't (see resultGeometry)
REFINED_NOTE = 'refined with thin probes'
NOT_REFINED_NOTE = 'from the small probes (not refined'
ROTATE_PLUGIN = 'eu.gmic.UserFileRotate'
ROTATE_ANGLE = 30.0
ROTATE_SOURCE = (1920, 1080)
ROD_TOLERANCE = 1e-3
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


def check_padded(report):
    """Checks an output whose top-left part is opaque green and the rest transparent black.

    Args:
        report: the parsed report.json of the render.

    Returns:
        A list of problems, empty if green and alpha go from 0 to 1, and red and blue are 0.
    """
    renders = report.get('renders') or []
    output = (renders[0].get('output') if renders else None) or {}
    lows, highs = output.get('min') or [], output.get('max') or []
    if len(lows) < 4 or len(highs) < 4 or None in lows[:4] or None in highs[:4]:
        return ['no RGBA output (%s, %s)' % (lows, highs)]
    expected = [(0.0, 0.0), (0.0, 1.0), (0.0, 0.0), (0.0, 1.0)]
    return ['channel %d is in [%g, %g], expected [%g, %g]' % (c, lows[c], highs[c], lo, hi)
            for c, (lo, hi) in enumerate(expected)
            if abs(lows[c] - lo) > TOLERANCE or abs(highs[c] - hi) > TOLERANCE]


def check_rotation(host, plugins, work):
    """Renders ROTATE_PLUGIN on a large Source: the refined prediction must be exact.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if the output has the rotated size, with no warning.
    """
    sw, sh = ROTATE_SOURCE
    args = ['-i', 'Source=rod:0,0,%d,%d' % (sw, sh)]
    result = render_plugins.render(host, args, plugins, work, TIMEOUT, (ROTATE_PLUGIN, CONTEXT, []))
    problem, errors, log = result[2], result[5], result[6]
    if problem:
        return ['%s: %s %s' % (ROTATE_PLUGIN, problem, ' '.join(errors))]
    problems = []
    if GEOMETRY_WARNING in log:
        problems.append('the result does not match the output')
    if REFINED_NOTE not in log:
        problems.append('no refinement with thin probes')
    report_path = os.path.join(work, ROTATE_PLUGIN[len('eu.gmic.'):], 'report.json')
    with open(report_path, encoding='utf-8') as f:
        report = json.load(f)
    problems += check_output(report, (1.0, 128 / 255.0, 0.0, 1.0))
    first = (report.get('renders') or [{}])[0]
    output = first.get('output') or {}
    rod = (first.get('regionOfDefinition') or {}).get('rod') or []
    src = ((first.get('inputs') or {}).get('Source') or {}).get('regionOfDefinition') or []
    par = ((((report.get('clipPreferences') or {}).get('clips') or {}).get('Source') or {})
           .get('pixelAspectRatio') or 1.0)
    if len(src) != 4:
        return ['%s: no region of definition of Source' % ROTATE_PLUGIN]
    # CImg's get_rotate, on Source's pixels
    width, height = round((src[2] - src[0]) / par), round(src[3] - src[1])
    c, s = abs(math.cos(math.radians(ROTATE_ANGLE))), abs(math.sin(math.radians(ROTATE_ANGLE)))
    rw, rh = round(1 + c * (width - 1) + s * (height - 1)), round(1 + s * (width - 1) + c * (height - 1))
    if (output.get('w'), output.get('h')) != (rw, rh):
        problems.append('the output is %sx%s, expected %dx%d (Source %dx%d pixels)'
                        % (output.get('w'), output.get('h'), rw, rh, width, height))
    expected = [src[0], src[3] - rh, src[0] + rw * par, src[3]]
    if len(rod) != 4 or any(abs(r - e) > ROD_TOLERANCE for r, e in zip(rod, expected)):
        problems.append('the region of definition is %s, expected %s' % (rod, expected))
    return ['%s: %s' % (ROTATE_PLUGIN, p) for p in problems]


def check_geometry(host, plugins, work):
    """Renders each of GEOMETRY_CASES, and checks the output's size and region of definition.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if every output has the expected size, position and color.
    """
    problems = []
    for pid, args, geometry, color, warns in GEOMETRY_CASES:
        name = '%s %s' % (pid, ' '.join(args)) if args else pid
        clips = [] if args is OFFSET_SOURCE else ['Source']
        result = render_plugins.render(host, args, plugins, work, TIMEOUT, (pid, CONTEXT, clips))
        problem, errors, log = result[2], result[5], result[6]
        if problem:
            problems.append('%s: %s %s' % (name, problem, ' '.join(errors)))
            continue
        if REFINED_NOTE in log or NOT_REFINED_NOTE in log:
            problems.append('%s: reached the refinement with thin probes (its sizes are exact '
                            'fractions of the input\'s)' % name)
        if (GEOMETRY_WARNING in log) != warns:
            problems.append('%s: %s' % (name, 'no warning that the result does not match the output'
                                        if warns else 'the result does not match the output'))
        report_path = os.path.join(work, pid[len('eu.gmic.'):], 'report.json')
        with open(report_path, encoding='utf-8') as f:
            report = json.load(f)
        problems += ['%s: %s' % (name, p) for p in
                     (check_padded(report) if color == PADDED else check_output(report, color))]
        renders = report.get('renders') or []
        first = renders[0] if renders else {}
        output = first.get('output') or {}
        # ([x1, y1, x2, y2] in canonical coordinates)
        rod = (first.get('regionOfDefinition') or {}).get('rod') or []
        src = ((first.get('inputs') or {}).get('Source') or {}).get('regionOfDefinition') or []
        if len(src) != 4:
            problems.append('%s: no region of definition of Source' % name)
            continue
        # Source's pixels: the file's (render scale 1), so the pixel aspect ratio is its RoD width
        # over its pixel width
        src_w, src_h = render_plugins.INPUT_WIDTH, render_plugins.INPUT_HEIGHT
        par = (src[2] - src[0]) / src_w
        sx, sy, dx, dy = geometry
        width, height = round(src_w * sx), round(src_h * sy)
        x1 = src[0] + dx * par
        y2 = src[3] - dy
        expected = [x1, y2 - height, x1 + width * par, y2]
        if (output.get('w'), output.get('h')) != (width, height):
            problems.append('%s: the output is %sx%s, expected %dx%d'
                            % (name, output.get('w'), output.get('h'), width, height))
        if len(rod) != 4 or any(abs(r - e) > ROD_TOLERANCE for r, e in zip(rod, expected)):
            problems.append('%s: the region of definition is %s, expected %s (Source: %s)'
                            % (name, rod, expected, src))
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
    problems += check_geometry(host, plugins, work)
    problems += check_rotation(host, plugins, work)
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
