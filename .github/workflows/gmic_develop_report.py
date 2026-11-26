#!/usr/bin/env python3
"""Compares the CI test results of G'MIC develop with those of the pinned G'MIC release (#226).

Reads the results directories of gmic_develop_tests.sh for the release and for develop (a
missing file reads as empty: the test table then says "not run"), and prints a Markdown summary:
- the sources (the report of gmic_develop_sources.sh) and each test's exit status;
- the plug-ins that develop adds or removes, and those whose menu category or inputs change;
- the describe problems that only develop has, and the outcome of each hot fix on both sides;
- the renders that differ: failures with their first G'MIC error (crashes and timeouts
  included), expected failures that render (XPASS), NaN outputs and results that don't match
  their region of definition; a render log cut short is flagged;
- the plug-ins that render on both sides with different outputs (largest mean difference
  first). With --baseline, the release has rendered those plug-ins again: one whose output
  changes from one release render to the next is listed apart, as not reproducible;
- the commands that the release defines, develop doesn't, and develop still calls, with the
  plug-ins that call them (directly or not);
- the parameter changes of the plug-ins of both sides, as the plug-in reads them (type,
  default, range, labels, removable, visibility): default, range and type changes first; the
  changes of the #@gui text alone are counted by parameter type;
- the new failure lines of the other tests.
--json also writes the differences as JSON, with every list in full. --write-differing only
writes the ids of the plug-ins whose outputs differ (comma-separated, for the --baseline renders).

usage: gmic_develop_report.py --release <dir> --develop <dir> [--release-renders <dir>]
                              [--develop-renders <dir>] [--baseline <dir>] [--sources <report.md>]
                              [--json <file>] [--write-differing <file>]
"""
import argparse
import array
import collections
import json
import math
import os
import re

import render_plugins  # (next to this script: its directory is on sys.path)

# the describe test's problems: its own FAIL lines, and the host's and the plug-in's; not the
# plug-in count, which is compared on its own
_DESCRIBE_PROBLEM = re.compile(
    r'^FAIL|DESCRIBE_FAILED|DUPLICATE_(PARAM|CLIP)|has an unknown type|openfx-gmic: Warning')
_DESCRIBE_COUNT = re.compile(r'^FAIL: (expected at least \d+ plug-ins|\d+ plug-ins, expected exactly)')
# the plug-in's log of each hot fix (gmic_plugin.cpp, gmicGetHotfixes()): applied, not needed, or
# not applied (a warning)
_HOTFIX = re.compile(r"^openfx-gmic: (?:Warning: )?hot fix for the G'MIC command '(\w+)'(.*)$")
_RENDER_FAIL = re.compile(r'^FAIL (eu\.gmic\.\w+): (.*)$')
_RENDER_XPASS = re.compile(r'^XPASS (eu\.gmic\.\w+): ')
_RENDER_NAN = re.compile(r'^WARNING: (eu\.gmic\.\w+): (\d+) NaN values')
_RENDER_MISMATCH = re.compile(r'^WARNING: \d+ plug-ins whose result does not match .*: (.*)$')
_RENDER_DONE = re.compile(r'^\d+ rendered in \d+s')
_FAIL_LINE = re.compile(r'^(FAIL|Error|.*\bError:)')
# a path in the release or develop checkout (in the other tests' lines)
_TREE_PATH = re.compile(r'\S*/(release|develop)/')
_GUI_FILTER = re.compile(r'^#@gui ([^:_][^:]*?)\s*:\s*(.*)$')
_GUI_PARAM = re.compile(r'^#@gui\s*:\s*(.*)$')
_PARAM = re.compile(r'^([^=]*?)\s*=\s*(.*)$')
# a parameter's definition: flags ('_': no preview update, '~': randomizable), type, arguments
# between (), [] or {}, then the visibility state and its propagation
_TYPEDEF = re.compile(r'^([_~]*)([A-Za-z_]+)\s*[(\[{](.*)[)\]}]\s*(?:_([0-2]))?\s*[-+*]?\s*$', re.DOTALL)
_NO_VALUE_TYPES = ('note', 'link', 'separator')
_DEFINITION = re.compile(r'^\+?([A-Za-z_]\w*)\s*:(?!=)')
# a command call: a word at the start of an item (after a blank, a quote, a brace or ';'), maybe
# with '+' or '-', then the end, a blank, a selection or a quote; long quoted strings (texts, e.g.
# the reference, not commands such as 'ac "cmd ${^0}"') are left out first
_CALL = re.compile(r'(?:^|(?<=[\s"{;]))[+-]?([A-Za-z_]\w*)(?=$|[\s\[."};])', re.MULTILINE)
_LONG_STRING = re.compile(r'"[^"\n]{80,}"')
_TESTS = ('build', 'describe', 'fft_smoke', 'render', 'user_file', 'update_file', 'user_source')
_OTHER_TESTS = ('fft_smoke', 'user_file', 'update_file', 'user_source')
_MAX_ITEMS = 100  # the longest list printed in the summary (the JSON has them all)
_OUTPUT_TOLERANCE = 1e-4  # the largest difference of two outputs that counts as none

Results = collections.namedtuple(
    'Results', 'status plugins groupings inputs describe hotfixes renders xpasses nans mismatches '
               'render_done filters text logs')


def _read(path):
    """Returns the text of a file, or '' if there is none (the summary shows what is missing)."""
    try:
        with open(path, encoding='utf-8', errors='replace') as stream:
            return stream.read().replace('\r', '')
    except OSError:
        return ''


def _split_arguments(text):
    """Splits a parameter's arguments at the commas outside quotes and brackets."""
    args, depth, current, quoted = [], 0, '', False
    for char in text:
        if char == '"':
            quoted = not quoted
        elif not quoted and char in '([{':
            depth += 1
        elif not quoted and char in ')]}':
            depth -= 1
        if char == ',' and depth == 0 and not quoted:
            args.append(current.strip())
            current = ''
        else:
            current += char
    args.append(current.strip())
    return args


def _number(text):
    """Returns text as a float, or text itself if it is not a number."""
    try:
        return float(text)
    except ValueError:
        return text


def exposed(definition):
    """Returns what the plug-in reads of a parameter definition, or None for a no-value one.

    Args:
        definition: the text after '=' in a '#@gui : <name> = <definition>' line.

    Returns:
        A dict: the type, the visibility and, depending on the type, the default, the range,
        the labels or removable, as Helpers/gmic_parser.cpp reads them (e.g. only X, Y and
        removable of a point). An unknown syntax gives {'type': '?', 'text': definition}.
    """
    match = _TYPEDEF.match(definition.strip())
    if not match:
        return {'type': '?', 'text': definition}
    kind = match.group(2).lower()
    if kind in _NO_VALUE_TYPES:
        return None
    args = _split_arguments(match.group(3))
    result = {'type': kind, 'visibility': match.group(4) or '2'}
    if kind in ('float', 'int'):
        result['default'] = _number(args[0])
        result['range'] = tuple(_number(arg) for arg in args[1:3])
    elif kind == 'bool':
        result['default'] = args[0].lower() in ('1', 'true')
    elif kind == 'choice':
        if re.fullmatch(r'\d+', args[0]):
            result['default'], labels = int(args[0]), args[1:]
        else:
            result['default'], labels = 0, args
        result['labels'] = tuple(label.strip('"') for label in labels)
    elif kind == 'point':
        result['default'] = tuple(_number(arg) for arg in args[:2])
        removable = args[2] if len(args) > 2 else '0'
        result['removable'] = removable if removable in ('-1', '1') else '0'
    elif kind == 'color':
        result['default'] = tuple(args)
    else:  # text, file, folder, value, button...
        result['default'] = match.group(3).strip()
    return result


def gui_filters(text):
    """Returns the filters of a G'MIC standard library's text, by plug-in id.

    Args:
        text: the text of the library (render_plugins.stdlib_text(), decoded).

    Returns:
        {id: [(commands, [(name, definition)...])...]}: one entry per filter with that id
        (render_plugins.plugin_id() of its name; two filters may have the same), with the text
        after the name in its '#@gui <name> :' line and its '#@gui : <name> = <definition>'
        lines.
    """
    found, params = collections.defaultdict(list), None
    for line in text.split('\n'):
        header = _GUI_FILTER.match(line)
        if header:
            params = []
            found[render_plugins.plugin_id(header.group(1))].append((header.group(2), params))
            continue
        param = _GUI_PARAM.match(line)
        if param and params is not None:
            named = _PARAM.match(param.group(1))
            params.append((named.group(1), named.group(2)) if named else (param.group(1), ''))
        elif not line.startswith('#@gui_'):  # (the localized lines don't end a filter)
            params = None
    return found


def definitions(text):
    """Returns {command: body} for the commands that a G'MIC command file defines."""
    bodies, current = {}, None
    for line in text.split('\n'):
        if line.startswith('#'):
            continue
        match = _DEFINITION.match(line.strip())
        if match:
            current = match.group(1)
            bodies[current] = [line[line.index(':') + 1:]]
        elif current:
            bodies[current].append(line)
    return {name: '\n'.join(lines) for name, lines in bodies.items()}


def read_results(directory):
    """Returns the Results in a results directory of gmic_develop_tests.sh."""
    status = {}
    for line in _read(os.path.join(directory, 'status.tsv')).splitlines():
        name, _, code = line.partition('\t')
        status[name] = code
    host_log = _read(os.path.join(directory, 'describe-host.log'))
    plugins = set(re.findall(r'^PLUGIN (\S+)', host_log, re.MULTILINE))
    groupings = dict(re.findall(r'^GROUPING (\S+)\t(.*)$', host_log, re.MULTILINE))
    inputs = dict(re.findall(r'^RENDERINFO (\S+)\t(.*)$', host_log, re.MULTILINE))
    describe_lines = (host_log + '\n' + _read(os.path.join(directory, 'describe.log'))).splitlines()
    describe = {line.strip() for line in describe_lines
                if _DESCRIBE_PROBLEM.search(line) and not _DESCRIBE_COUNT.match(line)}
    hotfixes = {}
    for match in map(_HOTFIX.match, host_log.splitlines()):
        if match:
            hotfixes[match.group(1)] = match.group(2).strip(' :')
    render_lines = _read(os.path.join(directory, 'render.log')).splitlines()
    renders, last = {}, None
    for line in render_lines:
        match = _RENDER_FAIL.match(line)
        if match:
            last = match.group(1)
            renders[last] = match.group(2)
        elif last and line.startswith('    '):
            if '\n' not in renders[last]:  # the first error line under the FAIL line
                renders[last] += '\n' + line.strip()
        else:
            last = None
    xpasses = {match.group(1) for match in map(_RENDER_XPASS.match, render_lines) if match}
    nans = {match.group(1): int(match.group(2)) for match in map(_RENDER_NAN.match, render_lines) if match}
    mismatches = set()
    for match in map(_RENDER_MISMATCH.match, render_lines):
        if match:
            mismatches.update(match.group(1).split())
    text = ''
    header = os.path.join(directory, 'gmic_stdlib_community.h')
    if os.path.exists(header):
        # (stdlib_text() decodes Latin-1, the files are UTF-8)
        text = render_plugins.stdlib_text(header).encode('latin-1').decode('utf-8', 'replace')
    logs = {test: _read(os.path.join(directory, test + '.log')) for test in _TESTS}
    return Results(status, plugins, groupings, inputs, describe, hotfixes, renders, xpasses, nans,
                   mismatches, any(map(_RENDER_DONE.match, render_lines)), gui_filters(text), text, logs)


def _shorten(text, size=60):
    """text, cut to size characters with '...'."""
    return text if len(text) <= size else text[:size - 3] + '...'


def _valued(params):
    """Returns {name: (exposed(), definition)} for the parameters that hold a value."""
    result = collections.OrderedDict()
    for name, definition in params:
        value = exposed(definition)
        if value is not None:
            while name in result:  # a repeated name gets primes
                name += "'"
            result[name] = (value, definition)
    return result


def compare_filters(old, new, plugins):
    """Compares the parameters that the plug-in reads, for the plug-ins of both sides.

    Args:
        old, new: gui_filters() results.
        plugins: the ids that are plug-ins on both sides.

    Returns:
        (changes, syntax, ambiguous): {id: {'values': [...], 'parameters': [...],
        'other': [...]}} (default, range and type changes; parameters added or removed; labels,
        removable, visibility and command changes), {parameter type: number of definitions
        whose text alone changed}, and the ids that several filters share on a side (skipped).
    """
    changes, syntax, ambiguous = {}, collections.Counter(), []
    for pid in sorted(plugins & set(old) & set(new)):
        if len(old[pid]) > 1 or len(new[pid]) > 1:
            ambiguous.append(pid)
            continue
        (old_commands, old_params), (new_commands, new_params) = old[pid][0], new[pid][0]
        found = collections.defaultdict(list)
        if old_commands != new_commands:
            found['other'].append('commands: `%s` -> `%s`' % (old_commands, new_commands))
        before, after = _valued(old_params), _valued(new_params)
        for name, (value, definition) in before.items():
            if name not in after:
                found['parameters'].append('removed `%s` (%s)' % (name, value['type']))
                continue
            new_value, new_definition = after[name]
            if value == new_value:
                if definition != new_definition:
                    syntax[value['type']] += 1
                continue
            keys = [key for key in ('type', 'default', 'range') if value.get(key) != new_value.get(key)]
            for key in keys[:1] if 'type' in keys else keys:
                found['values'].append('`%s` %s: %s -> %s' % (name, key, value.get(key), new_value.get(key)))
            if not keys:
                for key in ('labels', 'removable', 'visibility', 'text'):
                    if value.get(key) != new_value.get(key):
                        found['other'].append('`%s` %s: %s -> %s' % (name, key, value.get(key), new_value.get(key)))
        found['parameters'] += ['added `%s = %s`' % (name, _shorten(definition))
                                for name, (_, definition) in after.items() if name not in before]
        if [name for name in before if name in after] != [name for name in after if name in before]:
            found['parameters'].append('parameter order changed')
        found = {section: lines for section, lines in found.items() if lines}
        if found:
            changes[pid] = found
    return changes, dict(syntax), ambiguous


def removed_commands(release, develop):
    """Returns the commands that the release defines, develop doesn't, and develop still calls.

    Args:
        release, develop: the texts of the two standard libraries.

    Returns:
        {command: {'callers': [...], 'plugins': [...]}}: the develop commands whose definition
        calls it (_CALL: a heuristic, which may also find a word of a short text), and the plug-ins whose
        command or preview command calls it, directly or not.
    """
    old, new = definitions(release), definitions(develop)
    removed = set(old) - set(new)
    callers = collections.defaultdict(set)  # command -> the develop commands that name it
    for name, body in new.items():
        for word in set(_CALL.findall(_LONG_STRING.sub('""', body))):
            if word in removed or (word in new and word != name):
                callers[word].add(name)
    entry_points = collections.defaultdict(set)  # command -> the plug-ins that run it
    for pid, filters in gui_filters(develop).items():
        for commands, _ in filters:
            for command in re.findall(r'[A-Za-z_]\w*', commands)[:2]:
                entry_points[command].add(pid)
    result = {}
    for name in sorted(removed & set(callers)):
        seen, todo = set(), list(callers[name])
        while todo:
            command = todo.pop()
            if command not in seen:
                seen.add(command)
                todo += callers.get(command, ())
        plugins = sorted(set().union(*(entry_points.get(command, set()) for command in seen)))
        result[name] = {'callers': sorted(callers[name]), 'plugins': plugins}
    return result


def _read_pfm(path):
    """Returns (width, height, channels, values) of a PFM file, or None if it can't be read."""
    try:
        with open(path, 'rb') as stream:
            data = stream.read()
        magic, width, height, scale, pixels = data.split(maxsplit=4)
        channels = {b'PF': 3, b'Pf': 1}[magic]
        values = array.array('f')
        values.frombytes(pixels[:int(width) * int(height) * channels * 4])
        if float(scale) > 0:  # big-endian
            values.byteswap()
        return int(width), int(height), channels, values
    except (OSError, ValueError, KeyError):
        return None


def _outputs(render_dir):
    """Returns the output files of a render (from its report.json), or None if it has none."""
    try:
        with open(os.path.join(render_dir, 'report.json'), encoding='utf-8') as stream:
            report = json.load(stream)
    except (OSError, ValueError):
        return None
    return [name for render in report.get('renders', []) for name in (render.get('output') or {}).get('files', [])]


def _size(image):
    """'<width>x<height>x<channels>' of a _read_pfm() image ('unreadable' for None)."""
    return '%dx%dx%d' % image[:3] if image else 'unreadable'


def _same_bytes(path_a, path_b):
    """True if the two files have the same bytes."""
    with open(path_a, 'rb') as stream_a, open(path_b, 'rb') as stream_b:
        return stream_a.read() == stream_b.read()


def output_difference(old_dir, new_dir):
    """Returns how the outputs of two renders of a plug-in differ, or None if they don't.

    Args:
        old_dir, new_dir: the render directories (render_plugins.py's --work <dir>/<name>).

    Returns:
        None if a render has no report or if every output is the same within
        _OUTPUT_TOLERANCE, else a dict: 'mean' and 'max', the mean and largest absolute
        differences (NaN against a number counts as infinite), and 'note' for other files, sizes
        or channels (then both are infinite).
    """
    old_files, new_files = _outputs(old_dir), _outputs(new_dir)
    if old_files is None or new_files is None:
        return None
    if old_files != new_files:
        return {'mean': math.inf, 'max': math.inf, 'note': 'output files %s -> %s' % (old_files, new_files)}
    total, largest, count = 0.0, 0.0, 0
    for name in old_files:
        old_path, new_path = os.path.join(old_dir, name), os.path.join(new_dir, name)
        if _same_bytes(old_path, new_path):
            continue
        old, new = _read_pfm(old_path), _read_pfm(new_path)
        if old is None or new is None or old[:3] != new[:3]:
            return {'mean': math.inf, 'max': math.inf, 'note': '%s: %s -> %s' % (name, _size(old), _size(new))}
        for before, after in zip(old[3], new[3]):
            if before != after:
                if math.isnan(before) or math.isnan(after):
                    difference = 0.0 if math.isnan(before) and math.isnan(after) else math.inf
                else:
                    difference = abs(after - before)
                total += difference
                largest = max(largest, difference)
        count += len(old[3])
    if largest <= _OUTPUT_TOLERANCE:
        return None
    return {'mean': total / max(count, 1), 'max': largest}


def compare_outputs(old_root, new_root, plugins):
    """Returns {id: output_difference()} for the plug-ins whose outputs differ."""
    if not old_root or not new_root:
        return {}
    result = {}
    for pid in sorted(plugins):
        name = pid[len('eu.gmic.'):]
        difference = output_difference(os.path.join(old_root, name), os.path.join(new_root, name))
        if difference:
            result[pid] = difference
    return result


def _render_problem(results, pid):
    """A plug-in's render problem in results (None: rendered)."""
    if pid in results.renders:
        return results.renders[pid]
    if pid in results.xpasses:
        return 'rendered (XPASS: an expected failure)'
    return None if pid in results.plugins else 'not a plug-in'


def differences(release, develop, renders, baseline):
    """Returns the differences between the release's Results and develop's, as a JSON-able dict.

    Args:
        release, develop: read_results() of the two sides.
        renders: (release render dir, develop render dir), either may be None.
        baseline: the release's second render dir of the plug-ins whose outputs differ, or None.
    """
    both = release.plugins & develop.plugins
    render_changes = {}
    for pid in sorted(set(release.renders) | set(develop.renders) | release.xpasses | develop.xpasses):
        before, after = _render_problem(release, pid), _render_problem(develop, pid)
        if before != after and 'not a plug-in' not in (before, after):
            render_changes[pid] = {'release': before, 'develop': after}
    nans = {pid: {'release': release.nans.get(pid, 0), 'develop': develop.nans.get(pid, 0)}
            for pid in sorted((set(release.nans) | set(develop.nans)) & both)
            if release.nans.get(pid, 0) != develop.nans.get(pid, 0)}
    rendered = both - set(release.renders) - set(develop.renders)
    outputs = compare_outputs(renders[0], renders[1], rendered)
    unstable = {}
    if baseline:
        for pid in outputs:
            name = pid[len('eu.gmic.'):]
            again = output_difference(os.path.join(renders[0], name), os.path.join(baseline, name))
            if again:
                unstable[pid] = {'develop': outputs[pid], 'release_again': again}
    other = {}
    for test in _OTHER_TESTS:
        def fail_lines(results, test=test):
            return [_TREE_PATH.sub('<checkout>/', line)
                    for line in results.logs[test].splitlines() if _FAIL_LINE.match(line)]
        before = set(fail_lines(release))
        new_lines = [line for line in fail_lines(develop) if line not in before]
        if new_lines:
            other[test] = new_lines
    filter_changes, syntax, ambiguous = compare_filters(release.filters, develop.filters, both)
    return {
        'status': {test: {'release': release.status.get(test), 'develop': develop.status.get(test)}
                   for test in _TESTS},
        'render_done': {'release': release.render_done, 'develop': develop.render_done},
        'plugins': {
            'release': len(release.plugins), 'develop': len(develop.plugins),
            'added': sorted(develop.plugins - release.plugins),
            'removed': sorted(release.plugins - develop.plugins),
            'removed_hidden': sorted(pid for pid in release.plugins - develop.plugins if pid in develop.filters),
            'groupings': {pid: [release.groupings.get(pid), develop.groupings.get(pid)] for pid in sorted(both)
                          if release.groupings.get(pid) != develop.groupings.get(pid)},
            'inputs': {pid: [release.inputs.get(pid), develop.inputs.get(pid)] for pid in sorted(both)
                       if release.inputs.get(pid) != develop.inputs.get(pid)}},
        'describe': sorted(develop.describe - release.describe),
        'hotfixes': {name: {'release': release.hotfixes.get(name), 'develop': develop.hotfixes.get(name)}
                     for name in sorted(set(release.hotfixes) | set(develop.hotfixes))},
        'renders': render_changes,
        'nans': nans,
        'mismatches': {'added': sorted((develop.mismatches - release.mismatches) & both),
                       'removed': sorted((release.mismatches - develop.mismatches) & both)},
        'outputs': {pid: value for pid, value in outputs.items() if pid not in unstable},
        'outputs_unstable': unstable,
        'removed_commands': removed_commands(release.text, develop.text),
        'other_tests': other,
        'filters': filter_changes,
        'filters_syntax_only': syntax,
        'filters_ambiguous': ambiguous,
    }


def _items(lines):
    """Returns Markdown list lines for lines, cut at _MAX_ITEMS."""
    out = ['- ' + line for line in lines[:_MAX_ITEMS]]
    if len(lines) > _MAX_ITEMS:
        out.append('- ... (%d more, see summary.json)' % (len(lines) - _MAX_ITEMS))
    return out


def _problem(text):
    """A render problem (and its first error line, as code) on one Markdown line."""
    if not text:
        return 'rendered'
    problem, _, error = text.partition('\n')
    return '%s (`%s`)' % (problem, error) if error else problem


def _summary_lines(diff):
    """The Markdown lines of the plug-ins, describe, hot fixes and renders parts."""
    out = []
    plugins = diff['plugins']
    out += ['', '### Plug-ins: %d (release), %d (develop)' % (plugins['release'], plugins['develop']), '']
    out += ['Added: %d' % len(plugins['added'])] + _items(plugins['added'])
    out += ['', 'Removed: %d (in italics: still a filter of develop, that the plug-in leaves out)'
            % len(plugins['removed'])]
    out += _items(['*%s*' % pid if pid in plugins['removed_hidden'] else pid for pid in plugins['removed']])
    for key, title in (('groupings', 'Menu category changed'), ('inputs', 'Contexts or inputs changed')):
        out += ['', '%s: %d' % (title, len(plugins[key]))]
        out += _items(['%s: %s -> %s' % (pid, old, new) for pid, (old, new) in plugins[key].items()])
    out += ['', '### Describe problems (develop only): %d' % len(diff['describe']), '']
    out += _items(['`%s`' % line for line in diff['describe']])
    out += ['', '### Hot fixes', '', '| Command | Release | Develop |', '|---|---|---|']
    out += ['| %s | %s | %s |' % (name, value['release'] or '(not logged)', value['develop'] or '(not logged)')
            for name, value in diff['hotfixes'].items()]
    out += ['', '### Renders that differ: %d' % len(diff['renders']), '']
    out += _items(['%s: release: %s; develop: %s' % (pid, _problem(value['release']), _problem(value['develop']))
                   for pid, value in diff['renders'].items()])
    out += ['', '### NaN outputs that changed: %d' % len(diff['nans']), '']
    out += _items(['%s: %d -> %d NaN values' % (pid, value['release'], value['develop'])
                   for pid, value in diff['nans'].items()])
    mismatches = diff['mismatches']
    out += ['', '### Results that do not match their region of definition (#175): %d new, %d gone'
            % (len(mismatches['added']), len(mismatches['removed'])), '']
    out += _items(['new: ' + pid for pid in mismatches['added']] + ['gone: ' + pid for pid in mismatches['removed']])
    ranked = sorted(diff['outputs'].items(), key=lambda item: -item[1]['mean'])
    out += ['', '### Outputs that differ (rendered on both sides): %d, largest mean difference first'
            % len(ranked), '']
    out += _items(['%s: %s' % (pid, value.get('note') or 'mean %.3g, max %.3g' % (value['mean'], value['max']))
                   for pid, value in ranked])
    if diff['outputs_unstable']:
        out += ['', 'Not reproducible (another output when the release renders them again): %d'
                % len(diff['outputs_unstable'])]
        out += _items(sorted(diff['outputs_unstable']))
    return out


def markdown(diff, sources):
    """Returns the Markdown summary of differences() (and of the sources' report, if any)."""
    out = ["## G'MIC develop vs the pinned release", '']
    if sources:
        out += [sources.strip(), '']
    out += ['### Tests (exit status)', '', '| Test | Release | Develop |', '|---|---|---|']
    out += ['| %s | %s | %s |' % (test, status['release'] or 'not run', status['develop'] or 'not run')
            for test, status in diff['status'].items()]
    for side, done in diff['render_done'].items():
        if not done and diff['status']['render'][side] is not None:
            out += ['', '**The %s render log has no summary line: the render was cut short.**' % side]
    out += _summary_lines(diff)
    out += ['', '### Commands that develop removed and still calls: %d' % len(diff['removed_commands']), '']
    out += _items(['`%s`: called by %s; plug-ins: %s'
                   % (name, ', '.join('`%s`' % caller for caller in value['callers']), ', '.join(value['plugins']) or 'none')
                   for name, value in diff['removed_commands'].items()])
    out += ['', '### Other tests: new failure lines', '']
    for test, lines in diff['other_tests'].items():
        out += ['%s:' % test] + _items(['`%s`' % line for line in lines]) + ['']
    changes = diff['filters']
    out += ['', '### Parameters, as the plug-in reads them: %d plug-ins changed' % len(changes), '']
    for section, title in (('values', 'Default, range or type changes'),
                           ('parameters', 'Parameters added or removed'),
                           ('other', 'Labels, removable points, visibility, commands')):
        rows = ['%s: %s' % (pid, '; '.join(value[section])) for pid, value in changes.items() if section in value]
        out += ['%s: %d plug-ins' % (title, len(rows))] + _items(rows) + ['']
    syntax = diff['filters_syntax_only']
    out.append('Changes of the #@gui text alone (not read by the plug-in), by parameter type: %s'
               % (', '.join('%s %d' % item for item in sorted(syntax.items())) or 'none'))
    if diff['filters_ambiguous']:
        out += ['', 'Not compared (several filters give this id): ' + ', '.join(diff['filters_ambiguous'])]
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--release', required=True, help='the results of the pinned release')
    parser.add_argument('--develop', required=True, help="the results of G'MIC develop")
    parser.add_argument('--release-renders', help="the release's render work directory")
    parser.add_argument('--develop-renders', help="develop's render work directory")
    parser.add_argument('--baseline', help='the second release renders of the plug-ins whose outputs differ')
    parser.add_argument('--sources', help="gmic_develop_sources.sh's report")
    parser.add_argument('--json', help='where to write the differences, as JSON')
    parser.add_argument('--write-differing', help='where to write the ids whose outputs differ (only)')
    args = parser.parse_args()
    release, develop = read_results(args.release), read_results(args.develop)
    if args.write_differing:
        rendered = (release.plugins & develop.plugins) - set(release.renders) - set(develop.renders)
        with open(args.write_differing, 'w', encoding='utf-8') as stream:
            stream.write(','.join(compare_outputs(args.release_renders, args.develop_renders, rendered)))
        return
    diff = differences(release, develop, (args.release_renders, args.develop_renders), args.baseline)
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as stream:
            json.dump(diff, stream, indent=1)
    print(markdown(diff, _read(args.sources) if args.sources else ''), end='')


if __name__ == '__main__':
    main()
