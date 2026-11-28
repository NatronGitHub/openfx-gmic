#!/usr/bin/env python3
"""Render the filters of a user filter file (user_filters.gmic) and check their output (#168).

The file is installed as ~/.config/gmic/gmic_ofx.gmic in a temporary HOME, or with --update-file
as G'MIC's filter update file (update408.gmic for GMICVER 4.0.8 in gmic_sources.txt, read with
OFX_GMIC_UPDATE_FILE=1, #180), which replaces the built-in filters, or with --user-source as the
user source ~/.gmic, whose filters and commands come after the built-in ones (#206). Its filters
call a command that the file defines ("Unknown command" before #168), a built-in command, and a
built-in command that the file replaces: each output must be the uniform color that the filter
fills it with. User File Persistent and User File Persistent Store need the _persistent variable (a
string, or an image from 'store') and the status that their preview command sets (#206): their
outputs show that the plug-in ran the preview command first, and gave both to the main
command. User File Run Variables fills it with what the plug-in gives G'MIC on every run, as
G'MIC-Qt ($_host, $_input_layers, $_output_mode, the input's layer name), and User File Five
Channels returns 5 channels, which must fail the render (#206). User File Button and User File
Status Visibility have push buttons (#174): renders get 0 for them, and a press (hostDemo's -s
<button>, an instance changed action) runs the preview command once with 1, whose status sets
parameter values, which the report's params then show, also as a key of an animated parameter.
User File Status Edit sets a status after each edit (hostDemo's -s name=value, #173): the plug-in
runs the preview command at the end of the edit, and sets the values and, with
OFX_GMIC_TEST_STATUS_VISIBILITY=1 (as on Natron), the secret and enabled flags that it gives.
User File Status Keep gives the empty value "{}" for every parameter but one, which must keep its
value whatever its type, also with a visibility ("{}_0", "{}_1", #231), after edits and in a
render, which runs its preview command first.
The file also defines a command that the plug-in hot-fixes, which must be kept, with a note. Its
User File Cpus filter fills its output with G'MIC's $_cpus, rendered without OMP_NUM_THREADS so
that the plug-in sets it (#188): the value, printed, must be the number of CPUs (hostDemo runs one
render, whose fair share is all the hardware threads). This shows that the count reaches G'MIC,
not that the plug-in's prefix is applied: the value is also G'MIC's own default.

The result-size checks (#175) run only with the user filter file (not with --update-file or
--user-source: they don't depend on where the file comes from). hostDemo reports multi-resolution
support. With OFX_GMIC_MULTIRES=0 (a test aid of the plug-in: it then acts as on a host without
it), User File Larger (Resize Mode Dynamic) and User File Smaller (Fixed) must be resized to
Source's size, as before #175, with no warning. The other checks render on hostDemo as it is,
where the plug-in predicts the result's size, as on Natron. With Resize Mode Dynamic, the default,
User File Larger, User File Smaller, User File Taller, User File Layer Name, User File Copy and
User File Shifted must output the result's size, and the region of definition its size and
position: Shifted's pos(10,20) from Source's top-left corner, the others (no layout: no pos,
G'MIC's default pos(0,0), or a "+" copy's name) at Source's bottom-left corner (#238; Taller,
twice Source's height, must have its top half red at the top); with Fixed (Inplace), Source's
size and region of definition, with the same placement cropped or padded (Taller: green). No render may warn that
the result doesn't match the output, except User File Content Size, whose size depends on the
pixels. User File Larger and User File Shifted are also rendered at render scale 0.5: the output
is half the scale-1 output; Larger's canonical region of definition is the scale-1 one, and
Shifted's pos(10,20), in pixels of the image G'MIC gets, moves it twice as far in canonical
coordinates. On a 1800x1080-pixel Source: rotations (User File Rotate) and sizes in percent that
are no exact fractions (User File Percent) must be predicted within the plug-in's 1-pixel
margin; Larger, Smaller and Shifted must be exact, with no margin; and User File Frame Max
(percent of max(w,h)), User File Area Size (sqrt(W*H)) and User File Product (W*H/1024) must be
confirmed at full size, exactly, all without a warning. User File Slow is slower than the
plug-in's 10 s prediction budget: rendered at frames 0 and 1, its prediction must time out
once, the second query must not try again, and both outputs have Source's size (about 35 s).

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
render scale: hostDemo is not Natron, and the plug-in accepts every render scale there. It is
also rendered with its default point on hostDemo --no-normalised-defaults, a host without
kOfxParamPropDefaultCoordinateSystem that gives the normalised default as is, as DaVinci Resolve
(#221): the plug-in must convert it with the project size, so that the point and the output are
those of a host that converts it (hostDemo by default); also after the user sets the raw default
again (hostDemo's -s Spot=0.5,0.5, as a Reset on that host).

usage: user_filters_test.py --host <hostDemo, or the CMake build directory> [--config <config>]
                            --plugins <dir with GMIC.ofx.bundle>
                            [--stdlib <gmic_stdlib_community.h>] [--runs N] [--work <dir>]
                            [--update-file | --user-source]
"""
import argparse
import collections
import json
import math
import os
import re
import shutil
import subprocess
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
    # without a press: the parameters' defaults, and 0 for the push button (green; #174)
    'eu.gmic.UserFileButton': (0.25, 0.0, 0.1),
    'eu.gmic.UserFileStatusVisibility': (0.2, 0.0, 0.0),
    # without an edit, no status run: the defaults (#173)
    'eu.gmic.UserFileStatusEdit': (0.2, 0.0, 0.3),
    'eu.gmic.UserFileStatusDown': (0.5, 0.0, 0.1),
    'eu.gmic.UserFileStatusVariable': (0.1, 0.0, 0.0),
    'eu.gmic.UserFileStatusExpression': (0.1, 0.0, 0.0),
    'eu.gmic.UserFileStatusMainOnly': (0.1, 0.0, 0.0),
    # (#231) the preview's status, which the render runs first, gives Steps 7 and keeps Flag on
    'eu.gmic.UserFileStatusKeep': (0.25, 1.0, 0.7),
    'eu.gmic.UserFilePressSize': (0.0, 0.0, 1.0),
}
TOLERANCE = 1e-4
DEFINED_BY = ' is defined by '  # the note of a kept definition of a hot-fixed command
# (#175) the cases of check_geometry(): plug-in id, hostDemo arguments (a Source input that
# doesn't start at 0,0: OFFSET_SOURCE), the expected output size relative to Source's and its
# position: the pos(x,y) of a result of a layout run (some result with a non-zero pos: pixels
# from Source's top-left corner, y down), or AT_SOURCE_BOTTOM, Source's bottom-left corner, for
# the results of other runs (#238), the RGBA value of every output pixel (Padded(color): the
# result's color in part of the output, transparent black elsewhere, both checked through the
# minimum and maximum; SPLIT: the top half of the output red, the bottom half green, checked in
# the output file), and whether the render must warn that the result doesn't match the output.
# With Resize Mode Fixed, the output is Source's rectangle, with Dynamic's placement (#238).
OFFSET_SOURCE = ['-i', 'Source=file:../input.pfm@20,10']
AT_SOURCE_BOTTOM = 'at Source bottom'
Padded = collections.namedtuple('Padded', ['color'])
SPLIT = 'split'
# (each case: plug-in id, hostDemo arguments, the output's size and pos as above, its color, whether
# the render warns, how the size is found: EXACT, with no note (exact fractions; the test's small
# Source is probed at its own size), CONFIRMED by a probe at full size after a failed fit, or
# NO_PROBE (Fixed, Downsample), and the frame)
EXACT, CONFIRMED, NO_PROBE = 'exact', 'confirmed', 'no probe'
GEOMETRY_CASES = [
    ('eu.gmic.UserFileLarger', [], (2.0, 1.5, AT_SOURCE_BOTTOM), (1.0, 0.0, 0.0, 1.0), False, EXACT, '0'),
    ('eu.gmic.UserFileSmaller', [], (0.5, 0.25, AT_SOURCE_BOTTOM), (0.0, 1.0, 0.0, 1.0), False, EXACT, '0'),
    ('eu.gmic.UserFileShifted', [], (1.0, 1.0, (10, 20)), (0.0, 0.0, 1.0, 1.0), False, EXACT, '0'),
    ('eu.gmic.UserFileLarger', OFFSET_SOURCE, (2.0, 1.5, AT_SOURCE_BOTTOM), (1.0, 0.0, 0.0, 1.0), False, EXACT, '0'),
    ('eu.gmic.UserFileShifted', OFFSET_SOURCE, (1.0, 1.0, (10, 20)), (0.0, 0.0, 1.0, 1.0), False, EXACT, '0'),
    # taller than Source, its top half red: the output's top half (#238)
    ('eu.gmic.UserFileTaller', [], (1.0, 2.0, AT_SOURCE_BOTTOM), SPLIT, False, EXACT, '0'),
    ('eu.gmic.UserFileTaller', OFFSET_SOURCE, (1.0, 2.0, AT_SOURCE_BOTTOM), SPLIT, False, EXACT, '0'),
    # G'MIC's default pos(0,0) (gui_set_layer_name), and a "+" copy (named Source_c1): no layout
    ('eu.gmic.UserFileLayerName', [], (2.0, 2.0, AT_SOURCE_BOTTOM), (1.0, 1.0, 0.0, 1.0), False, EXACT, '0'),
    ('eu.gmic.UserFileCopy', [], (2.0, 1.5, AT_SOURCE_BOTTOM), (0.0, 1.0, 1.0, 1.0), False, EXACT, '0'),
    # Fixed (Inplace): Source's rectangle, Dynamic's placement cropped or padded: Taller's bottom
    # half (green), Shifted moved by its pos
    ('eu.gmic.UserFileLarger', ['-s', 'Resize Mode=0'], (1.0, 1.0, (0, 0)), (1.0, 0.0, 0.0, 1.0), False, NO_PROBE, '0'),
    ('eu.gmic.UserFileSmaller', ['-s', 'Resize Mode=0'], (1.0, 1.0, (0, 0)), Padded((0.0, 1.0, 0.0, 1.0)), False,
     NO_PROBE, '0'),
    ('eu.gmic.UserFileShifted', ['-s', 'Resize Mode=0'], (1.0, 1.0, (0, 0)), Padded((0.0, 0.0, 1.0, 1.0)), False,
     NO_PROBE, '0'),
    ('eu.gmic.UserFileTaller', ['-s', 'Resize Mode=0'], (1.0, 1.0, (0, 0)), (0.0, 1.0, 0.0, 1.0), False, NO_PROBE, '0'),
    # Downsample 1/2: resized to Source's size
    ('eu.gmic.UserFileLarger', ['-s', 'Resize Mode=2'], (1.0, 1.0, (0, 0)), (1.0, 0.0, 0.0, 1.0), False, NO_PROBE, '0'),
    # the probes' size (40x60), and the warning
    ('eu.gmic.UserFileContentSize', [], (0.25, 0.5, AT_SOURCE_BOTTOM), (1.0, 1.0, 0.0, 1.0), True, EXACT, '0'),
    # not affine, sqrt(W*H) = 139 wide: a small Source, probed at its own size
    ('eu.gmic.UserFileAreaSize', [], (139 / 160.0, 1.0, AT_SOURCE_BOTTOM), (0.0, 1.0, 1.0, 1.0), False, EXACT, '0'),
    # Output Layer "Frame", a layout: result [0] at frame 0 (pos(5,6)), [1] (Source's size, no pos) at
    # frame 1, at Source's top-left corner
    ('eu.gmic.UserFileFrames', ['-s', 'Output Layer=11'], (0.5, 0.5, (5, 6)), (1.0, 0.0, 1.0, 1.0), False, EXACT, '0'),
    ('eu.gmic.UserFileFrames', ['-s', 'Output Layer=11'], (1.0, 1.0, (0, 0)), (1.0, 0.0, 1.0, 1.0), False, EXACT, '1'),
]
GEOMETRY_WARNING = "doesn't match the output"
# the plug-in's override of the host's multi-resolution support: as on a host without it (see
# check_no_multires; render_plugins.render()'s extra_env)
MULTIRES_OFF = {'OFX_GMIC_MULTIRES': '0'}
# the plug-in's notes when it refines a prediction with thin probes, or confirms it with a probe at
# full size (see resultGeometry)
REFINED_NOTE = 'refined with thin probes'
CONFIRMED_NOTE = 'confirmed at full size'
# rendered on a large Source made by hostDemo: 1920x1080 canonical, 1800x1080 pixels (hostDemo's
# pixel aspect ratio is 16/15); the tests take the pixels from the report
LARGE_SOURCE = (1920, 1080)
ROTATE_PLUGIN = 'eu.gmic.UserFileRotate'
# refined with a 1-pixel margin: exact or 1 pixel off (45 in the plug-in's model), never a warning
ROTATE_ANGLES = (10.0, 20.0, 30.0, 45.0, 60.0, 70.0, 89.5)
AREA_PLUGIN = 'eu.gmic.UserFileAreaSize'
PRODUCT_PLUGIN = 'eu.gmic.UserFileProduct'
# on the large Source: exact fractions, predicted exactly, with neither note and no margin (plug-in
# id, size relative to Source's, position as in GEOMETRY_CASES, color)
LARGE_EXACT_CASES = [
    ('eu.gmic.UserFileLarger', (2.0, 1.5), AT_SOURCE_BOTTOM, (1.0, 0.0, 0.0, 1.0)),
    ('eu.gmic.UserFileSmaller', (0.5, 0.25), AT_SOURCE_BOTTOM, (0.0, 1.0, 0.0, 1.0)),
    ('eu.gmic.UserFileShifted', (1.0, 1.0), (10, 20), (0.0, 0.0, 1.0, 1.0)),
]
SLOW_PLUGIN = 'eu.gmic.UserFileSlow'
TIMEOUT_NOTE = 'took more than'
RECENT_TIMEOUT_NOTE = 'timed out less than'
PERCENT_PLUGIN = 'eu.gmic.UserFilePercent'
# not exact fractions of the input's size: refined with a margin, never a warning
PERCENTS = (10.0, 70.0)
FRAME_MAX_PLUGIN = 'eu.gmic.UserFileFrameMax'
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
PUSH_BUTTON = 'OfxParamTypePushButton'
# (the plug-in): follow the status's visibilities on any host, not only on Natron (#173)
STATUS_VISIBILITY_VAR = 'OFX_GMIC_TEST_STATUS_VISIBILITY'
STATUS_PROGRESS_ID = 'eu.gmic.status'  # (the plug-in) the progress of a run for the status
VISIBILITY_MISMATCH = 'visibilities, for'  # (the plug-in's log) a status's visibility count


class ButtonCase(collections.namedtuple(
        'ButtonCase', 'pid button args output values keys secret large exact',
        defaults=(False, {}))):
    """A render after the press of a push button (#174), and what it must give.

    Attributes:
        pid: the plug-in id.
        button: the push button that is pressed, with an optional time (name[@time]).
        args: hostDemo's arguments before the press (-s name=value...).
        output: the RGB value of every output pixel.
        values: the values of parameters after the press (in the report: canonical coordinates for
            a point, 0..1 for a color), SPOT for the point that the status moves.
        keys: the keys of parameters after the press ([[time, value]...]).
        secret: the secret flag of parameters after the press.
        large: on a LARGE_SOURCE Source made by hostDemo, not the 160x120 input.
        exact: values of parameters after the press, compared exactly (no TOLERANCE).
    """


class StatusCase(collections.namedtuple(
        'StatusCase', 'pid args visibility output values secret enabled runs expected_log',
        defaults=((),))):
    """A render after user edits of a filter that sets a status (#173), and what it must give.

    Attributes:
        pid: the plug-in id.
        args: hostDemo's arguments: the edits (-s name=value...), or none.
        visibility: run with STATUS_VISIBILITY_VAR=1, so that the plug-in applies the status's
            visibilities as on Natron.
        output: the RGB value of every output pixel.
        values: the values of parameters after the edits.
        secret: the secret flag of parameters after the edits.
        enabled: the enabled flag of parameters after the edits.
        runs: the number of runs for the status (progresses with STATUS_PROGRESS_ID).
        expected_log: the texts of log lines that the case must give (e.g. a status mismatch),
            which are then no problem; empty for none.
    """


SPOT = 'spot'  # User File Button's point: at 60%, 40% of the 160x120 input (y down in G'MIC)
BUTTON_CASES = (
    # a value of each type, past a separator; the render gets 0 for the button (green)
    ButtonCase('eu.gmic.UserFileButton', 'Press', [], (0.75, 0.0, 0.4),
               {'Level': 0.75, 'Steps': 4, 'Mode': 2, 'Flag': True,
                'Tint': [40 / 255.0, 50 / 255.0, 60 / 255.0], 'Spot': SPOT, 'Label': 'after'},
               {}, {}),
    # animated parameters, pressed at 5: Level gets a key there; Steps is 4 at 5 (between its keys
    # 3 and 5), the value that the status gives, so it is unchanged and gets no key. The render, at
    # 0, gets the keys' values there (Level 0.25, Steps 3)
    ButtonCase('eu.gmic.UserFileButton', 'Press@5',
               ['-s', 'Level@0=0.25', '-s', 'Level@10=0.5', '-s', 'Steps@0=3', '-s', 'Steps@10=5'],
               (0.25, 0.0, 0.3), {'Level': 0.25, 'Steps': 3},
               {'Level': [[0.0, 0.25], [5.0, 0.75], [10.0, 0.5]],
                'Steps': [[0.0, 3], [10.0, 5]]},
               {}),
    # "_0" hides the parameter in G'MIC-Qt: its value is set, and it stays shown on hosts other
    # than Natron (see STATUS_CASES for Natron's, #173); the status removes the removable point
    # Target (its checkbox goes off, the render gets nan,nan)
    ButtonCase('eu.gmic.UserFileStatusVisibility', 'Hide', [], (0.6, 0.0, 1.0),
               {'Hidden Level': 0.6, 'Target Enabled': False}, {}, {'Hidden Level': False}),
    # the press's image on a 1800x1080-pixel Source: scaled down so that it fits in 512 pixels
    # (512x307), also with Downsample 1/2 (1024x614, which the filter gets downsampled to 512x307);
    # a float status value is set as it is
    ButtonCase('eu.gmic.UserFilePressSize', 'Size', [], (0.0, 0.0, 1.0),
               {'Width': 512, 'Height': 307}, {}, {}, True, {'Exact': 0.3}),
    ButtonCase('eu.gmic.UserFilePressSize', 'Size', ['-s', 'Resize Mode=2'], (0.0, 0.0, 1.0),
               {'Width': 512, 'Height': 307}, {}, {}, True),
)
USER_FILE = 'gmic_ofx.gmic'  # (install_user_file): the user filter file

STATUS_EDIT = 'eu.gmic.UserFileStatusEdit'
STATUS_DOWN = 'eu.gmic.UserFileStatusDown'
STATUS_KEEP = 'eu.gmic.UserFileStatusKeep'
# (#231) User File Status Keep's edits of a parameter of each type but Steps, which its status
# sets to 7 (Flag keeps its default, on), and the values that the parameters must have after them
KEEP_EDITS = ['-s', 'Level=0.6', '-s', 'Pick=2', '-s', 'Tint=0.2,0.4,0.6', '-s', 'Spot=40,30',
              '-s', 'Label=edited', '-s', 'Path=edited.txt', '-s', 'Folder=edited']
KEEP_VALUES = {'Level': 0.6, 'Steps': 7, 'Pick': 2, 'Flag': True, 'Tint': [0.2, 0.4, 0.6],
               'Spot': [40.0, 30.0], 'Label': 'edited', 'Path': 'edited.txt', 'Folder': 'edited'}
STATUS_CASES = (
    # the initial states: Amount and the note before it hidden, Echo disabled; other hosts show
    # and enable every parameter of such a filter
    StatusCase(STATUS_EDIT, [], True, (0.2, 0.0, 0.3), {},
               {'Amount': True, 'note': True, 'Echo': False, 'Tail': False}, {'Echo': False}, 0),
    StatusCase(STATUS_EDIT, [], False, (0.2, 0.0, 0.3), {},
               {'Amount': False, 'note': False, 'Echo': False}, {'Echo': True}, 0),
    # Mode On: the status shows Amount (and its note) and enables Echo, gives Echo Amount's value
    # and Tail 7
    StatusCase(STATUS_EDIT, ['-s', 'Mode=1'], True, (0.2, 0.2, 0.7),
               {'Echo': 0.2, 'Tail': 7},
               {'Amount': False, 'note': False, 'Echo': False, 'Tail': False}, {'Echo': True}, 1),
    # the values without the visibilities on other hosts
    StatusCase(STATUS_EDIT, ['-s', 'Mode=1'], False, (0.2, 0.2, 0.7),
               {'Echo': 0.2, 'Tail': 7}, {'Amount': False, 'note': False}, {'Echo': True}, 1),
    # Mode Off again: a status without visibilities gives every parameter its initial state; the
    # values stay
    StatusCase(STATUS_EDIT, ['-s', 'Mode=1', '-s', 'Mode=0'], True, (0.2, 0.2, 0.7),
               {'Echo': 0.2, 'Tail': 7},
               {'Amount': True, 'note': True, 'Echo': False}, {'Echo': False}, 2),
    # "+": Level gives its visibility to the note after it; "_1" disables Other
    StatusCase(STATUS_DOWN, ['-s', 'Mode=1'], True, (0.5, 0.0, 0.1), {},
               {'Level': True, 'note': True, 'Other': False}, {'Other': False}, 1),
    # then 2 values and visibilities for 3 parameters: ignored (both logged), the states stay
    StatusCase(STATUS_DOWN, ['-s', 'Mode=1', '-s', 'Mode=0'], True, (0.5, 0.0, 0.1), {},
               {'Level': True, 'note': True, 'Other': False}, {'Other': False}, 2,
               (render_plugins.STATUS_MISMATCH, VISIBILITY_MISMATCH)),
    # visibility markers in the main command only: every parameter shown, as #170
    StatusCase('eu.gmic.UserFileStatusMainOnly', ['-s', 'A=0.3'], True, (0.3, 0.0, 0.0), {},
               {'A': False, 'B': False}, {'B': True}, 1),
    # a status list held in a variable (status=\{...}, u $status)
    StatusCase('eu.gmic.UserFileStatusVariable', ['-s', 'A=0.3'], False, (0.3, 0.0, 0.7),
               {'B': 7}, {}, {}, 1),
    # "u {expr}" is no status list: an edit runs nothing
    StatusCase('eu.gmic.UserFileStatusExpression', ['-s', 'A=0.3'], True, (0.3, 0.0, 0.0),
               {}, {}, {}, 0),
    # no edit: only the render applies the status of its preview, in its own copy of the values
    # (Steps 7, Flag on); the parameters keep theirs
    StatusCase(STATUS_KEEP, [], True, (0.25, 1.0, 0.7), {'Steps': 3, 'Flag': True}, {}, {}, 0),
    # Mode Keep, after each edit: "{}" keeps each parameter's value (the edits'), "{7}" sets Steps;
    # every parameter keeps its initial state (shown and enabled)
    StatusCase(STATUS_KEEP, KEEP_EDITS, True, (0.6, 1.0, 0.7), dict(KEEP_VALUES, Mode=0),
               {p: False for p in KEEP_VALUES}, {p: True for p in list(KEEP_VALUES) + ['Mode']},
               len(KEEP_EDITS) // 2),
    # Mode Visibility: the values stay, and every other parameter is hidden or disabled, but the
    # point Spot, which stays shown and enabled (Parameter::followsStatus)
    StatusCase(STATUS_KEEP, KEEP_EDITS + ['-s', 'Mode=1'], True, (0.6, 1.0, 0.7),
               dict(KEEP_VALUES, Mode=1),
               {'Level': True, 'Pick': True, 'Tint': True, 'Label': True, 'Folder': True,
                'Steps': False},
               {'Steps': False, 'Flag': False, 'Spot': True, 'Path': False},
               len(KEEP_EDITS) // 2 + 1),
    # a press's status hides Hidden Level ("_0")
    StatusCase('eu.gmic.UserFileStatusVisibility', ['-s', 'Hide'], True, (0.6, 0.0, 1.0),
               {'Hidden Level': 0.6}, {'Hidden Level': True}, {}, 1),
)


def update_file_name():
    """Returns the name of G'MIC's update file for gmic_sources.txt's GMICVER (gmic_version.sh).

    Returns:
        update<major><minor><patch>.gmic, e.g. update408.gmic for G'MIC 4.0.8.

    Raises:
        subprocess.CalledProcessError: gmic_version.sh found no GMICVER in gmic_sources.txt.
        ValueError: GMICVER is not <major>.<minor>.<patch>.
    """
    sources = os.path.join(HERE, '..', '..', 'gmic_sources.txt')
    version = subprocess.run(['sh', os.path.join(HERE, 'gmic_version.sh'), sources],
                             stdout=subprocess.PIPE, check=True, universal_newlines=True).stdout.strip()
    match = re.fullmatch(r'(\d+)\.(\d+)\.(\d+)', version)
    if not match:
        raise ValueError('GMICVER is "%s", not <major>.<minor>.<patch>' % version)
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


def first_render(report):
    """The first render of a parsed report.json (hostDemo --report), an empty dict if none."""
    renders = (report or {}).get('renders') or []
    return renders[0] if renders else {}


def expected_rod(src, par, width, height, pos):
    """The canonical region of definition of a result of width x height pixels.

    Args:
        src: Source's region of definition [x1, y1, x2, y2].
        par: the pixel aspect ratio.
        width: the result's width in pixels.
        height: the result's height in pixels.
        pos: the result's position, as in GEOMETRY_CASES: the pos(x,y) of a layout's result, in
            pixels from Source's top-left corner (y down), or AT_SOURCE_BOTTOM (Source's
            bottom-left corner).

    Returns:
        [x1, y1, x2, y2].
    """
    if pos == AT_SOURCE_BOTTOM:
        x1, y1 = src[0], src[1]
    else:
        x1, y1 = src[0] + pos[0] * par, src[3] - pos[1] - height
    return [x1, y1, x1 + width * par, y1 + height]


def check_split(report, directory):
    """Checks an output whose top half is opaque red and bottom half opaque green (SPLIT).

    Args:
        report: the parsed report.json of the render.
        directory: the render's directory, with its output files.

    Returns:
        A list of problems, empty if every row of the output file (bottom row first) has its color.
    """
    output = first_render(report).get('output') or {}
    files = [f for f in output.get('files') or [] if not f.endswith('.alpha.pfm')]
    image = render_plugins.read_pfm(os.path.join(directory, files[0])) if files else None
    if not image or image[2] != 3:
        return ['no RGB output file (%s)' % output.get('files')]
    width, height, _, values = image
    problems = []
    for row in range(height):
        # (PFM: bottom row first)
        expected = (0.0, 1.0, 0.0) if row < height // 2 else (1.0, 0.0, 0.0)
        pixels = values[row * width * 3:(row + 1) * width * 3]
        if any(abs(v - expected[k % 3]) > TOLERANCE for k, v in enumerate(pixels)):
            problems.append('row %d from the bottom (of %d) is not %s' % (row, height, expected))
            break
    lows, highs = output.get('min') or [], output.get('max') or []
    if len(lows) < 4 or len(highs) < 4 or None in (lows[3], highs[3]) or min(lows[3], highs[3]) < 1.0 - TOLERANCE:
        problems.append('not opaque (alpha in %s, %s)' % (lows[3:4], highs[3:4]))
    return problems


def check_output(report, expected):
    """Checks that the first output of a render has every pixel of the expected color.

    Args:
        report: the parsed report.json of the render (hostDemo --report).
        expected: the RGB value of every output pixel.

    Returns:
        A list of problems, empty if the output is right.
    """
    output = first_render(report).get('output') or {}
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


def check_padded(report, color):
    """Checks an output with a part of an opaque color and the rest transparent black.

    Args:
        report: the parsed report.json of the render.
        color: the RGBA value of the result's pixels.

    Returns:
        A list of problems, empty if every channel goes from 0 to its value in color.
    """
    renders = report.get('renders') or []
    output = (renders[0].get('output') if renders else None) or {}
    lows, highs = output.get('min') or [], output.get('max') or []
    if len(lows) < 4 or len(highs) < 4 or None in lows[:4] or None in highs[:4]:
        return ['no RGBA output (%s, %s)' % (lows, highs)]
    expected = [(0.0, v) for v in color]
    return ['channel %d is in [%g, %g], expected [%g, %g]' % (c, lows[c], highs[c], lo, hi)
            for c, (lo, hi) in enumerate(expected)
            if abs(lows[c] - lo) > TOLERANCE or abs(highs[c] - hi) > TOLERANCE]


def large_source_render(host, plugins, work, pid, args):
    """Renders a plug-in on a LARGE_SOURCE Source made by hostDemo.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.
        pid: the plug-in id.
        args: more hostDemo arguments (-s name=value).

    Returns:
        (problem, log, first render of the report, Source's RoD, Source's size in pixels, PAR);
        problem is None if it rendered.
    """
    sw, sh = LARGE_SOURCE
    result = render_plugins.render(host, ['-i', 'Source=rod:0,0,%d,%d' % (sw, sh)] + args, plugins, work,
                                   TIMEOUT, (pid, CONTEXT, []))
    problem, errors, log = result.problem, result.errors, result.log
    if problem:
        return '%s %s' % (problem, ' '.join(errors)), log, {}, [], (0, 0), 1.0
    with open(os.path.join(work, pid[len('eu.gmic.'):], 'report.json'), encoding='utf-8') as f:
        report = json.load(f)
    first = (report.get('renders') or [{}])[0]
    src = ((first.get('inputs') or {}).get('Source') or {}).get('regionOfDefinition') or []
    par = ((((report.get('clipPreferences') or {}).get('clips') or {}).get('Source') or {})
           .get('pixelAspectRatio') or 1.0)
    if len(src) != 4:
        return 'no region of definition of Source', log, first, [], (0, 0), par
    return None, log, first, src, (round((src[2] - src[0]) / par), round(src[3] - src[1])), par


def check_max_color(first, color):
    """Checks that the output's maximum is color (a result in a transparent margin).

    Args:
        first: the first render of the report.
        color: the RGBA value of the result's pixels.

    Returns:
        A list of problems.
    """
    highs = (first.get('output') or {}).get('max') or []
    if len(highs) < 4 or None in highs[:4]:
        return ['no RGBA output']
    return ['channel %d: maximum %g, expected %g' % (c, highs[c], v)
            for c, v in enumerate(color) if abs(highs[c] - v) > TOLERANCE]


def check_large_sources(host, plugins, work):
    """Renders ROTATE_PLUGIN, PERCENT_PLUGIN and FRAME_MAX_PLUGIN on a LARGE_SOURCE Source.

    The rotations (ROTATE_ANGLES) and percentages (PERCENTS) are refined with thin probes, within
    a pixel, with a 1-pixel margin on the right and top sides: the output is the result's size
    plus 0 to 2 pixels, at Source's bottom-left corner (they have no position of their own), with
    no warning. The frame in percent of
    max(w,h) and the size in sqrt(W*H) (AREA_PLUGIN) are confirmed at full size: exact.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if every render is right.
    """
    problems = []
    for angle in ROTATE_ANGLES:
        name = '%s Angle=%g' % (ROTATE_PLUGIN, angle)
        problem, log, first, src, (width, height), par = large_source_render(
            host, plugins, work, ROTATE_PLUGIN, ['-s', 'Angle=%g' % angle])
        if problem:
            problems.append('%s: %s' % (name, problem))
            continue
        if GEOMETRY_WARNING in log:
            problems.append('%s: the result does not match the output' % name)
        if REFINED_NOTE not in log:
            problems.append('%s: no refinement with thin probes' % name)
        problems += ['%s: %s' % (name, p) for p in check_max_color(first, (1.0, 128 / 255.0, 0.0, 1.0))]
        # CImg's get_rotate, on Source's pixels
        c, s = abs(math.cos(math.radians(angle))), abs(math.sin(math.radians(angle)))
        rw = round(1 + c * (width - 1) + s * (height - 1))
        rh = round(1 + s * (width - 1) + c * (height - 1))
        output = first.get('output') or {}
        w, h = output.get('w') or 0, output.get('h') or 0
        # (rw, rh in double precision: CImg's float may round 1 pixel the other way)
        if not (rw - 1 <= w <= rw + 2 and rh - 1 <= h <= rh + 2):
            problems.append('%s: the output is %sx%s, expected %dx%d plus at most 2 pixels'
                            % (name, w, h, rw, rh))
        rod = (first.get('regionOfDefinition') or {}).get('rod') or []
        # (no position of its own: at Source's bottom-left corner, the margin at the top, #238)
        if len(rod) != 4 or abs(rod[0] - src[0]) > ROD_TOLERANCE or abs(rod[1] - src[1]) > ROD_TOLERANCE:
            problems.append("%s: the region of definition %s doesn't start at Source's bottom-left corner %s"
                            % (name, rod, src))
    for percent in PERCENTS:
        name = '%s Percent=%g' % (PERCENT_PLUGIN, percent)
        problem, log, first, src, (width, height), par = large_source_render(
            host, plugins, work, PERCENT_PLUGIN, ['-s', 'Percent=%g' % percent])
        if problem:
            problems.append('%s: %s' % (name, problem))
            continue
        if GEOMETRY_WARNING in log:
            problems.append('%s: the result does not match the output' % name)
        problems += ['%s: %s' % (name, p) for p in check_max_color(first, (128 / 255.0, 0.0, 1.0, 1.0))]
        pw, ph = width * percent / 100, height * percent / 100
        output = first.get('output') or {}
        w, h = output.get('w') or 0, output.get('h') or 0
        if not (math.floor(pw) <= w <= math.ceil(pw) + 2 and math.floor(ph) <= h <= math.ceil(ph) + 2):
            problems.append('%s: the output is %sx%s, expected %gx%g plus at most 2 pixels' % (name, w, h, pw, ph))
        rod = (first.get('regionOfDefinition') or {}).get('rod') or []
        if len(rod) != 4 or abs(rod[0] - src[0]) > ROD_TOLERANCE or abs(rod[1] - src[1]) > ROD_TOLERANCE:
            problems.append("%s: the region of definition %s doesn't start at Source's bottom-left corner %s"
                            % (name, rod, src))
    for pid, (sx, sy), pos, color in LARGE_EXACT_CASES:
        problem, log, first, src, (width, height), par = large_source_render(host, plugins, work, pid, [])
        if problem:
            problems.append('%s on the large Source: %s' % (pid, problem))
            continue
        ew, eh = round(width * sx), round(height * sy)
        output = first.get('output') or {}
        if (output.get('w'), output.get('h')) != (ew, eh):
            problems.append('%s on the large Source: the output is %sx%s, expected %dx%d exactly (no margin)'
                            % (pid, output.get('w'), output.get('h'), ew, eh))
        rod = (first.get('regionOfDefinition') or {}).get('rod') or []
        expected = expected_rod(src, par, ew, eh, pos)
        if len(rod) != 4 or any(abs(r - e) > ROD_TOLERANCE for r, e in zip(rod, expected)):
            problems.append('%s on the large Source: the region of definition is %s, expected %s' % (pid, rod, expected))
        if GEOMETRY_WARNING in log or REFINED_NOTE in log or CONFIRMED_NOTE in log:
            problems.append('%s on the large Source: a warning, refined or confirmed (expected exact)' % pid)
        problems += ['%s on the large Source: %s' % (pid, p) for p in check_output({'renders': [first]}, color)]
    # not affine (sqrt(W*H)), and a weak product (W*H/1024): confirmed at full size, exact
    for pid, size_of in ((AREA_PLUGIN, lambda w, h: round(math.sqrt(w * h))), (PRODUCT_PLUGIN, lambda w, h: round(w * h / 1024))):
        problem, log, first, src, (width, height), par = large_source_render(host, plugins, work, pid, [])
        if problem:
            problems.append('%s: %s' % (pid, problem))
            continue
        aw = size_of(width, height)
        output = first.get('output') or {}
        if (output.get('w'), output.get('h')) != (aw, height):
            problems.append('%s: the output is %sx%s, expected %dx%d' % (pid, output.get('w'), output.get('h'), aw, height))
        if GEOMETRY_WARNING in log or CONFIRMED_NOTE not in log:
            problems.append('%s: a warning, or not confirmed at full size' % pid)
    problem, log, first, src, (width, height), par = large_source_render(host, plugins, work, FRAME_MAX_PLUGIN, [])
    if problem:
        return problems + ['%s: %s' % (FRAME_MAX_PLUGIN, problem)]
    s = round(0.1 * max(width + 2, height + 2))
    fw, fh = width + 2 + 2 * s, height + 2 + 2 * s
    output = first.get('output') or {}
    if (output.get('w'), output.get('h')) != (fw, fh):
        problems.append('%s: the output is %sx%s, expected %dx%d'
                        % (FRAME_MAX_PLUGIN, output.get('w'), output.get('h'), fw, fh))
    if GEOMETRY_WARNING in log:
        problems.append('%s: the result does not match the output' % FRAME_MAX_PLUGIN)
    if CONFIRMED_NOTE not in log:
        problems.append('%s: not confirmed at full size' % FRAME_MAX_PLUGIN)
    report = {'renders': [first]}
    problems += ['%s: %s' % (FRAME_MAX_PLUGIN, p) for p in check_output(report, (0.0, 128 / 255.0, 1.0, 1.0))]
    return problems


def check_timeout(host, plugins, work):
    """Renders SLOW_PLUGIN at frames 0 and 1: the prediction times out once, then is skipped.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if both frames have Source's size, the first prediction timed
        out, and the second query didn't try again (the timeout is remembered).
    """
    result = render_plugins.render(host, [], plugins, work, TIMEOUT, (SLOW_PLUGIN, CONTEXT, ['Source']), frame='0,1')
    problem, errors, log = result.problem, result.errors, result.log
    if problem:
        return ['%s: %s %s' % (SLOW_PLUGIN, problem, ' '.join(errors))]
    problems = []
    if TIMEOUT_NOTE not in log:
        problems.append('the prediction did not time out')
    if RECENT_TIMEOUT_NOTE not in log:
        problems.append('the timeout was not remembered for the next query')
    with open(os.path.join(work, SLOW_PLUGIN[len('eu.gmic.'):], 'report.json'), encoding='utf-8') as f:
        report = json.load(f)
    for r in report.get('renders') or []:
        output = r.get('output') or {}
        if (output.get('w'), output.get('h')) != (render_plugins.INPUT_WIDTH, render_plugins.INPUT_HEIGHT):
            problems.append('at time %s, the output is %sx%s, not Source\'s size' % (r.get('time'), output.get('w'), output.get('h')))
    return ['%s: %s' % (SLOW_PLUGIN, p) for p in problems]


def check_no_multires(host, plugins, work):
    """Renders results of another size with OFX_GMIC_MULTIRES=0, without multi-resolution.

    User File Larger (Resize Mode Dynamic) and User File Smaller (Fixed) must then be resized to
    Source's size, as before #175: Source's size and region of definition, a uniform output, and
    no warning or note of a refined or confirmed prediction.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems.
    """
    problems = []
    for pid, args, color in (('eu.gmic.UserFileLarger', [], (1.0, 0.0, 0.0, 1.0)),
                             ('eu.gmic.UserFileSmaller', ['-s', 'Resize Mode=0'], (0.0, 1.0, 0.0, 1.0))):
        name = '%s %s without multi-resolution' % (pid, ' '.join(args))
        result = render_plugins.render(host, args, plugins, work, TIMEOUT, (pid, CONTEXT, ['Source']),
                                       extra_env=MULTIRES_OFF)
        problem, errors, log = result.problem, result.errors, result.log
        if problem:
            problems.append('%s: %s %s' % (name, problem, ' '.join(errors)))
            continue
        with open(os.path.join(work, pid[len('eu.gmic.'):], 'report.json'), encoding='utf-8') as f:
            report = json.load(f)
        problems += ['%s: %s' % (name, p) for p in check_output(report, color)]
        first = (report.get('renders') or [{}])[0]
        output = first.get('output') or {}
        if (output.get('w'), output.get('h')) != (render_plugins.INPUT_WIDTH, render_plugins.INPUT_HEIGHT):
            problems.append('%s: the output is %sx%s, not Source\'s size' % (name, output.get('w'), output.get('h')))
        rod = (first.get('regionOfDefinition') or {}).get('rod')
        src = ((first.get('inputs') or {}).get('Source') or {}).get('regionOfDefinition')
        if rod != src:
            problems.append("%s: the region of definition is %s, not Source's %s" % (name, rod, src))
        if GEOMETRY_WARNING in log or REFINED_NOTE in log or CONFIRMED_NOTE in log:
            problems.append('%s: a warning, or a note of a refined or confirmed prediction' % name)
    return problems


def check_scaled_geometry(host, plugins, work):
    """Renders User File Larger and User File Shifted at render scales 1 and RENDER_SCALE.

    On a Source that hostDemo makes in canonical coordinates: at RENDER_SCALE, the output must be
    the scale-1 output times RENDER_SCALE. Larger (no position of its own) must be at Source's
    bottom-left corner, its canonical region of definition the scale-1 one; Shifted's pos(10,20) is
    in pixels of the image that G'MIC gets, so its offset from Source's top-left corner is twice the
    scale-1 one in canonical coordinates.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems.
    """
    problems = []
    for pid, pos_in_pixels in (('eu.gmic.UserFileLarger', False), ('eu.gmic.UserFileShifted', True)):
        renders = {}
        for scale in (1, RENDER_SCALE):
            name = '%s at scale %s' % (pid, scale)
            result = render_plugins.render(host, ['--scale', str(scale), '-i', 'Source=rod:0,0,160,120'], plugins,
                                           work, TIMEOUT, (pid, CONTEXT, []))
            problem, errors, log = result.problem, result.errors, result.log
            if problem:
                problems.append('%s: %s %s' % (name, problem, ' '.join(errors)))
                continue
            if GEOMETRY_WARNING in log:
                problems.append('%s: the result does not match the output' % name)
            with open(os.path.join(work, pid[len('eu.gmic.'):], 'report.json'), encoding='utf-8') as f:
                renders[scale] = (json.load(f).get('renders') or [{}])[0]
        if len(renders) != 2:
            continue
        full, scaled = renders[1], renders[RENDER_SCALE]
        out1, out2 = full.get('output') or {}, scaled.get('output') or {}
        expected = (round((out1.get('w') or 0) * RENDER_SCALE), round((out1.get('h') or 0) * RENDER_SCALE))
        if (out2.get('w'), out2.get('h')) != expected:
            problems.append('%s at scale %s: the output is %sx%s, expected %dx%d'
                            % (pid, RENDER_SCALE, out2.get('w'), out2.get('h'), expected[0], expected[1]))
        rod1 = (full.get('regionOfDefinition') or {}).get('rod') or []
        rod2 = (scaled.get('regionOfDefinition') or {}).get('rod') or []
        src = ((full.get('inputs') or {}).get('Source') or {}).get('regionOfDefinition') or []
        if len(rod1) != 4 or len(rod2) != 4 or len(src) != 4:
            problems.append('%s: no region of definition' % pid)
            continue
        if not pos_in_pixels and any(abs(a - b) > ROD_TOLERANCE for a, b in zip(rod1[:2], src[:2])):
            problems.append("%s at scale 1: the region of definition %s doesn't start at Source's bottom-left corner %s"
                            % (pid, rod1, src))
        # the same size in canonical coordinates; Shifted: the offset from Source's top-left corner
        # (the scale-1 pos) divided by RENDER_SCALE, pos being in pixels of the image that G'MIC gets
        want = list(rod1)
        if pos_in_pixels:
            dx1, dy1 = rod1[0] - src[0], src[3] - rod1[3]
            x1, y2 = src[0] + dx1 / RENDER_SCALE, src[3] - dy1 / RENDER_SCALE
            want = [x1, y2 - (rod1[3] - rod1[1]), x1 + (rod1[2] - rod1[0]), y2]
        if any(abs(a - b) > ROD_TOLERANCE for a, b in zip(rod2, want)):
            problems.append('%s at scale %s: the region of definition is %s, expected %s (%s at scale 1)'
                            % (pid, RENDER_SCALE, rod2, want, rod1))
    return problems


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
    for pid, args, geometry, color, warns, path, frame in GEOMETRY_CASES:
        name = '%s %s frame %s' % (pid, ' '.join(args), frame)
        clips = [] if args is OFFSET_SOURCE else ['Source']
        result = render_plugins.render(host, args, plugins, work, TIMEOUT, (pid, CONTEXT, clips), frame=frame)
        problem, errors, log = result.problem, result.errors, result.log
        if problem:
            problems.append('%s: %s %s' % (name, problem, ' '.join(errors)))
            continue
        if REFINED_NOTE in log:
            problems.append('%s: refined with thin probes (expected %s)' % (name, path))
        if (CONFIRMED_NOTE in log) != (path == CONFIRMED):
            problems.append('%s: %s' % (name, 'not confirmed at full size' if path == CONFIRMED
                                        else 'confirmed at full size (expected %s)' % path))
        if (GEOMETRY_WARNING in log) != warns:
            problems.append('%s: %s' % (name, 'no warning that the result does not match the output'
                                        if warns else 'the result does not match the output'))
        report_path = os.path.join(work, pid[len('eu.gmic.'):], 'report.json')
        with open(report_path, encoding='utf-8') as f:
            report = json.load(f)
        if isinstance(color, Padded):
            problems += ['%s: %s' % (name, p) for p in check_padded(report, color.color)]
        elif color == SPLIT:
            problems += ['%s: %s' % (name, p) for p in check_split(report, os.path.dirname(report_path))]
        else:
            problems += ['%s: %s' % (name, p) for p in check_output(report, color)]
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
        sx, sy, pos = geometry
        width, height = round(src_w * sx), round(src_h * sy)
        expected = expected_rod(src, par, width, height, pos)
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
            result = render_plugins.render(host, [], plugins, work, TIMEOUT, (pid, CONTEXT, ['Source']))
            if run == 0:
                logs.append(result.log)
            if result.problem:
                problems.append('%s: %s %s' % (pid, result.problem, ' '.join(result.errors)))
                break
            seconds[pid] = min(result.seconds, seconds.get(pid, result.seconds))
            problems += ['%s: %s' % (pid, p) for p in check_output(result.report, EXPECTED[pid])]
    return problems, logs, seconds


def same_value(value, expected):
    """Is a parameter value of the report the expected one (numbers within TOLERANCE)?"""
    if isinstance(expected, bool) or isinstance(value, bool):
        return value is expected
    if isinstance(expected, (int, float)):
        return isinstance(value, (int, float)) and abs(value - expected) <= TOLERANCE
    if isinstance(expected, list):
        return (isinstance(value, list) and len(value) == len(expected) and
                all(same_value(v, e) for v, e in zip(value, expected)))
    return value == expected


def render_report(host, plugins, work, pid, host_args, output, extra_env=None, expected_log=(),
                  clips=('Source',)):
    """Renders a plug-in of the user file, and checks that its output is uniform.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.
        pid: the plug-in id.
        host_args: hostDemo's arguments before the render (-s name=value...).
        output: the RGB value of every output pixel.
        extra_env: environment variables of the render, if any.
        expected_log: texts of log lines that are no problem (see render_plugins.render()).
        clips: the input clips that get the 160x120 input (none: hostDemo makes them).

    Returns:
        (problems, report, log): a list of problems, empty if the render gave the output, the
        parsed report.json (None if the render failed), and hostDemo's log.
    """
    result = render_plugins.render(host, host_args, plugins, work, TIMEOUT,
                                   (pid, CONTEXT, list(clips)), extra_env=extra_env,
                                   expected_log=expected_log)
    problem, errors, log = result[2], result[5], result[6]
    if problem:
        return ['%s %s' % (problem, ' '.join(errors))], None, log
    with open(os.path.join(work, pid[len('eu.gmic.'):], 'report.json'), encoding='utf-8') as f:
        report = json.load(f)
    return check_output(report, output), report, log


def check_buttons(host, plugins, work):
    """Renders each of BUTTON_CASES after the press of its button, and checks the result.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if every press gave the expected parameters and output.
    """
    problems = []
    for case in BUTTON_CASES:
        name = '%s, %s pressed' % (case.pid, case.button)
        button = case.button.split('@')[0]
        if case.large:
            name += ' on the large Source'
            source_args, clips = ['-i', 'Source=rod:0,0,%d,%d' % LARGE_SOURCE], []
        else:
            source_args, clips = [], ['Source']
        found, report, _ = render_report(host, plugins, work, case.pid,
                                         source_args + case.args + ['-s', case.button],
                                         case.output, clips=clips)
        problems += ['%s: %s' % (name, p) for p in found]
        if report is None:
            continue
        params = report.get('params') or {}
        if (params.get(button) or {}).get('type') != PUSH_BUTTON:
            problems.append('%s: %s is not a push button (%s)'
                            % (name, button, params.get(button)))
        par = (((report.get('clipPreferences') or {}).get('clips') or {}).get('Source') or {}).get(
            'pixelAspectRatio') or 1.0
        for param, expected in case.values.items():
            if expected == SPOT:
                expected = [0.6 * 160 * par, (1 - 0.4) * 120]
            value = (params.get(param) or {}).get('value')
            if not same_value(value, expected):
                problems.append('%s: %s is %r, expected %r' % (name, param, value, expected))
        for param, expected in case.exact.items():
            value = (params.get(param) or {}).get('value')
            if value != expected:
                problems.append('%s: %s is %r, expected exactly %r'
                                % (name, param, value, expected))
        for param, expected in case.keys.items():
            keys = (params.get(param) or {}).get('keys')
            if not same_value(keys, expected):
                problems.append('%s: the keys of %s are %r, expected %r'
                                % (name, param, keys, expected))
        for param, expected in case.secret.items():
            secret = (params.get(param) or {}).get('secret')
            if secret is not expected:
                problems.append('%s: %s has secret %r, expected %r'
                                % (name, param, secret, expected))
    return problems


def check_status(host, plugins, work):
    """Renders each of STATUS_CASES after its edits, and checks the result.

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if the edits gave the expected parameters and output.
    """
    problems = []
    for case in STATUS_CASES:
        name = '%s, %s%s' % (case.pid, ' '.join(case.args) or 'no edit',
                             ', %s=1' % STATUS_VISIBILITY_VAR if case.visibility else '')
        extra_env = {STATUS_VISIBILITY_VAR: '1'} if case.visibility else None
        found, report, log = render_report(host, plugins, work, case.pid, case.args,
                                           case.output, extra_env, case.expected_log)
        problems += ['%s: no log line with %r' % (name, e) for e in case.expected_log
                     if e not in log]
        problems += ['%s: %s' % (name, p) for p in found]
        if report is None:
            continue
        runs = sum(1 for p in report.get('progress') or [] if p.get('id') == STATUS_PROGRESS_ID)
        if runs != case.runs:
            problems.append('%s: %d runs for the status, expected %d' % (name, runs, case.runs))
        params = report.get('params') or {}
        for flag, expectations in (('value', case.values), ('secret', case.secret),
                                   ('enabled', case.enabled)):
            for param, expected in expectations.items():
                value = (params.get(param) or {}).get(flag)
                if not same_value(value, expected):
                    problems.append('%s: %s has %s %r, expected %r'
                                    % (name, param, flag, value, expected))
    if not problems:
        print('status after edits (#173): %d cases checked' % len(STATUS_CASES))
    return problems


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
    if result.problem:
        return ['%s: %s %s' % (CPUS_PLUGIN, result.problem, ' '.join(result.errors))]
    output = first_render(result.report).get('output') or {}
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
    if not result.problem:
        return ['%s: rendered, expected a failure (%s)' % (CHANNELS_PLUGIN, CHANNELS_ERROR)]
    if CHANNELS_ERROR not in result.log:
        return ['%s: %s, without the message %r' % (CHANNELS_PLUGIN, result.problem, CHANNELS_ERROR)]
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
    if result.problem:
        return '%s %s' % (result.problem, ' '.join(result.errors)), {}
    return None, first_render(result.report)


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


def check_normalised_defaults(host, plugins, work):
    """Renders SCALE_PLUGIN with its default point, with and without normalised defaults (#221).

    On hostDemo --no-normalised-defaults, the plug-in gets the point's normalised default as is,
    and must convert it as hostDemo does by default: the point's value and the output must be the
    same on both hosts, also after a user edit that sets the raw default again (a Reset there).
    Not covered: a project offset (hostDemo's is 0).

    Args:
        host: the path of hostDemo.
        plugins: the directory with GMIC.ofx.bundle.
        work: the work directory, with the user file installed.

    Returns:
        A list of problems, empty if both renders give the same point and output.
    """
    problems, results = [], []
    for args in ([], ['--no-normalised-defaults'],
                 ['--no-normalised-defaults', '-s', 'Spot=0.5,0.5']):
        name = '%s, default point%s' % (SCALE_PLUGIN, ', ' + ' '.join(args) if args else '')
        result = render_plugins.render(host, args, plugins, work, TIMEOUT,
                                       (SCALE_PLUGIN, CONTEXT, []))
        if result.problem:
            problems.append('%s: %s %s' % (name, result.problem, ' '.join(result.errors)))
            continue
        spot = (((result.report or {}).get('params') or {}).get('Spot') or {}).get('value')
        output = first_render(result.report).get('output') or {}
        results.append((name, spot, output.get('min'), output.get('max')))
    if len(results) == 3:
        (name0, *expected) = results[0]
        for name1, *found in results[1:]:
            for what, e, f in zip(('Spot', 'the output minimum', 'the output maximum'),
                                  expected, found):
                if e is None or not same_value(f, e):
                    problems.append('%s: %s is %r, %r with %s' % (name1, what, f, e, name0))
    if not problems:
        print('point defaults on a host without normalised defaults (#221): %r' % (results[0][1],))
    return problems


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
    problems += check_normalised_defaults(host, plugins, work)
    problems += check_cpus(host, plugins, work)
    problems += check_channels(host, plugins, work)
    problems += check_buttons(host, plugins, work)
    problems += check_status(host, plugins, work)
    if not any(KEPT_NOTE in log for log in logs):
        problems.append("no note that the file's own definition of 'star_tone' is kept")
    if name == USER_FILE:
        # (the result-size checks: only with the user file, see the module docstring)
        problems += check_no_multires(host, plugins, work)
        problems += check_geometry(host, plugins, work)
        problems += check_scaled_geometry(host, plugins, work)
        problems += check_large_sources(host, plugins, work)
        problems += check_timeout(host, plugins, work)
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
    # the renders inherit this environment (render_plugins.render() leaves out GMIC_PATH and
    # XDG_CONFIG_HOME, so that G'MIC's folder, where the plug-in looks for the update file, is
    # the HOME's .config/gmic): the update file only with --update-file, and hostDemo's own
    # multi-resolution support but where a check sets MULTIRES_OFF
    for var in ('OFX_GMIC_UPDATE_FILE', STATUS_VISIBILITY_VAR, 'OFX_GMIC_MULTIRES'):
        os.environ.pop(var, None)
    name = USER_FILE
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
