#!/usr/bin/env python3
"""Compares the CI test results of G'MIC develop with those of the pinned G'MIC release (#226).

Reads the results directories of gmic_develop_tests.sh for the release and for develop (a
missing file reads as empty: the test table then says "not run"), and prints a Markdown summary:
- the sources (the report of gmic_develop_sources.sh, with the outcome of each patch) and each
  test's exit status; a render log cut short is flagged;
- the plug-ins that develop adds or removes, and those whose menu category or inputs change;
- the describe problems that only develop has, and the outcome of each hot fix on both sides;
- the renders that differ: failures with their first G'MIC error (crashes and timeouts
  included), expected failures that render (XPASS), NaN outputs and results that don't match
  their region of definition;
- the plug-ins that render on both sides with different outputs (largest mean difference
  first). With --release-again and --develop-again, each side has rendered those plug-ins a
  second time: one whose output then changes is listed apart, as not reproducible on that side;
- the commands that the release defines and develop doesn't, that develop may still call (a
  heuristic), with the plug-ins that may reach them;
- the parameters of the plug-ins of both sides as the host sees them (describe_plugins.cpp's
  PARAM lines: type, label, default, range, choice options, secret): type, default and range
  changes first; the plug-ins whose #@gui text changed with nothing the host sees are counted;
- the new failure lines of the other tests (the checkout paths left out).
--json also writes the differences as JSON, with every list in full. --write-differing only
writes the ids of the plug-ins whose outputs differ (comma-separated, for the second renders).

usage: gmic_develop_report.py --release <dir> --develop <dir> [--release-renders <dir>]
                              [--develop-renders <dir>] [--release-again <dir>]
                              [--develop-again <dir>] [--sources <report.md>] [--json <file>]
                              [--write-differing <file>]
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
_GUI_FILTER = re.compile(r'^#@gui ([^:_][^:]*?)\s*:\s*(.*)$')
_GUI_PARAM = re.compile(r'^#@gui\s*:\s*(.*)$')
# describe_plugins.cpp's line for each parameter: id, name, type, label, then property=values
_PARAM_LINE = re.compile(r'^PARAM (\S+)\t([^\t\n]*)\t([^\t\n]*)\t([^\t\n]*)((?:\t[^\t\n]*)*)$', re.MULTILINE)
_PARAM_FIELDS = ('type', 'OfxParamPropDefault', 'OfxParamPropMin', 'OfxParamPropMax', 'label',
                 'OfxParamPropChoiceOption', 'OfxParamPropSecret')
_VALUE_FIELDS = ('type', 'OfxParamPropDefault', 'OfxParamPropMin', 'OfxParamPropMax')
# the name that the plug-in gives a parameter without a label: its position (notes, separators),
# which shifts whenever a parameter is added or removed before it; left out of the comparison
_POSITION_NAME = re.compile(r'^__\d+$')
# a row of gmic_develop_sources.sh's patch table: patch, file, outcome
_PATCH_ROW = re.compile(r'^\| `(patches/[^`]+)` \| [^|]+ \| (.*) \|$', re.MULTILINE)
_PFM_HEADER = re.compile(rb'(P[Ff])\s+(\d+)\s+(\d+)\s+([-+0-9.eE]+)\s')
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
    'Results', 'status plugins groupings inputs params describe hotfixes renders xpasses nans '
               'mismatches render_cut filters text logs')


def _read(path):
    """Returns the text of a file, or '' if there is none (the summary shows what is missing)."""
    try:
        with open(path, encoding='utf-8', errors='replace') as stream:
            return stream.read().replace('\r', '')
    except OSError:
        return ''


def gui_filters(text):
    """Returns the filters of a G'MIC standard library's text, by plug-in id.

    Args:
        text: the text of the library (render_plugins.stdlib_text(), decoded).

    Returns:
        {id: [(commands, [parameter text...])...]}: one entry per filter with that id
        (render_plugins.plugin_id() of its name; two filters may have the same), with the text
        after the name in its '#@gui <name> :' line and the text of its '#@gui : ' lines.
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
            params.append(param.group(1))
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
    params = collections.defaultdict(collections.OrderedDict)
    for pid, name, kind, label, props in _PARAM_LINE.findall(host_log):
        fields = {'type': kind, 'label': label}
        fields.update(prop.split('=', 1) for prop in props.split('\t')[1:] if '=' in prop)
        if not _POSITION_NAME.match(name):
            params[pid][name] = fields
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
    # a render log without its summary line: the render was cut short (an exception, a timeout)
    render_cut = bool(render_lines) and not any(map(_RENDER_DONE.match, render_lines))
    return Results(status, plugins, groupings, inputs, dict(params), describe, hotfixes, renders, xpasses,
                   nans, mismatches, render_cut, gui_filters(text), text, logs)


def compare_parameters(old, new, old_gui, new_gui, plugins):
    """Compares the parameters as the host sees them (describe_plugins.cpp's PARAM lines).

    Args:
        old, new: {id: {name: fields}} of the two sides (read_results()).
        old_gui, new_gui: gui_filters() results, to count the plug-ins whose #@gui text alone
            changed.
        plugins: the ids that are plug-ins on both sides.

    Returns:
        (changes, text_only): {id: {'values': [...], 'parameters': [...], 'other': [...]}}
        (type, default and range changes; parameters added or removed, or their order;
        labels, choice options and secret flags), and the number of plug-ins whose #@gui text
        changed with nothing the host sees.
    """
    changes, text_only = {}, 0
    for pid in sorted(plugins):
        before, after = old.get(pid, {}), new.get(pid, {})
        found = collections.defaultdict(list)
        for name, fields in before.items():
            if name not in after:
                found['parameters'].append('removed `%s` (%s)' % (name, fields['type']))
                continue
            for key in _PARAM_FIELDS:
                if fields.get(key) != after[name].get(key):
                    section = 'values' if key in _VALUE_FIELDS else 'other'
                    found[section].append('`%s` %s: %s -> %s' % (name, key, fields.get(key), after[name].get(key)))
        found['parameters'] += ['added `%s` (%s)' % (name, fields['type'])
                                for name, fields in after.items() if name not in before]
        if [name for name in before if name in after] != [name for name in after if name in before]:
            found['parameters'].append('parameter order changed')
        found = {section: lines for section, lines in found.items() if lines}
        if found:
            changes[pid] = found
        elif old_gui.get(pid) != new_gui.get(pid):
            text_only += 1
    return changes, text_only


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
    except OSError:
        return None
    # one whitespace byte after the scale: the pixels may start with bytes that look like more
    header = _PFM_HEADER.match(data)
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


def _unstable(outputs, first, again):
    """{id: difference} of the plug-ins of outputs whose render in first differs from the one in again."""
    if not first or not again:
        return {}
    result = {}
    for pid in outputs:
        name = pid[len('eu.gmic.'):]
        difference = output_difference(os.path.join(first, name), os.path.join(again, name))
        if difference:
            result[pid] = difference
    return result


def _patch_outcomes(sources):
    """{patch: outcome} from gmic_develop_sources.sh's report (every patch applies to the release)."""
    return {patch: outcome for patch, outcome in _PATCH_ROW.findall(sources)}


def differences(release, develop, renders, again, trees, sources):
    """Returns the differences between the release's Results and develop's, as a JSON-able dict.

    Args:
        release, develop: read_results() of the two sides.
        renders: (release render dir, develop render dir), either may be None.
        again: (release, develop) render dirs of the plug-ins whose outputs differ, rendered a
            second time, either may be None.
        trees: the (release, develop) checkouts, whose paths are left out of the lines compared.
        sources: gmic_develop_sources.sh's report ('' if there is none).
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
    unstable = {'release': _unstable(outputs, renders[0], again[0]),
                'develop': _unstable(outputs, renders[1], again[1])}
    other = {}
    for test in _OTHER_TESTS:
        def fail_lines(results, test=test):
            lines = [line for line in results.logs[test].splitlines() if _FAIL_LINE.match(line)]
            for tree in trees:
                lines = [line.replace(tree, '<checkout>') for line in lines] if tree else lines
            return lines
        before = set(fail_lines(release))
        new_lines = [line for line in fail_lines(develop) if line not in before]
        if new_lines:
            other[test] = new_lines
    filter_changes, text_only = compare_parameters(release.params, develop.params, release.filters,
                                                   develop.filters, both)
    patches = _patch_outcomes(sources)
    return {
        'status': {test: {'release': release.status.get(test), 'develop': develop.status.get(test)}
                   for test in _TESTS},
        'render_cut': {'release': release.render_cut, 'develop': develop.render_cut},
        'random_expected': any('does not apply' in outcome for patch, outcome in patches.items()
                               if patch.endswith('cimg-omp-rand.sh')),
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
        'outputs': {pid: value for pid, value in outputs.items()
                    if pid not in unstable['release'] and pid not in unstable['develop']},
        'outputs_unstable': unstable,
        'removed_commands': removed_commands(release.text, develop.text),
        'other_tests': other,
        'parameters': filter_changes,
        'parameters_text_only': text_only,
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
    if diff['random_expected']:
        out += ['', '`cimg-omp-rand.sh` does not apply to develop: the random numbers of its OpenMP loops depend '
                'on the thread scheduling, so the outputs of filters that use them are expected to differ.']
    unstable = diff['outputs_unstable']
    groups = (('release (and maybe develop)', sorted(unstable['release'])),
              ('develop only', sorted(set(unstable['develop']) - set(unstable['release']))))
    for title, ids in groups:
        if ids:
            out += ['', 'Not reproducible with %s (another output when rendered again): %d' % (title, len(ids))]
            out += _items(ids)
    return out


def markdown(diff, sources):
    """Returns the Markdown summary of differences() (and of the sources' report, if any)."""
    out = ["## G'MIC develop vs the pinned release", '']
    if sources:
        out += [sources.strip(), '']
    out += ['### Tests (exit status)', '', '| Test | Release | Develop |', '|---|---|---|']
    out += ['| %s | %s | %s |' % (test, status['release'] or 'not run', status['develop'] or 'not run')
            for test, status in diff['status'].items()]
    for side, cut in diff['render_cut'].items():
        if cut:
            out += ['', '**The %s render log has no summary line: the render was cut short.**' % side]
    out += _summary_lines(diff)
    out += ['', '### Commands that develop removed and may still call: %d' % len(diff['removed_commands']), '']
    out += _items(['`%s`: named by %s; plug-ins that may call it: %s'
                   % (name, ', '.join('`%s`' % caller for caller in value['callers']), ', '.join(value['plugins']) or 'none')
                   for name, value in diff['removed_commands'].items()])
    out += ['', '### Other tests: new failure lines', '']
    for test, lines in diff['other_tests'].items():
        out += ['%s:' % test] + _items(['`%s`' % line for line in lines]) + ['']
    changes = diff['parameters']
    out += ['', '### Parameters, as the host sees them: %d plug-ins changed' % len(changes), '']
    for section, title in (('values', 'Type, default or range changes'),
                           ('parameters', 'Parameters added or removed'),
                           ('other', 'Labels, choice options, secret flags')):
        rows = ['%s: %s' % (pid, '; '.join(value[section])) for pid, value in changes.items() if section in value]
        out += ['%s: %d plug-ins' % (title, len(rows))] + _items(rows) + ['']
    out.append('Plug-ins whose #@gui text changed with nothing the host sees: %d' % diff['parameters_text_only'])
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--release', required=True, help='the results of the pinned release')
    parser.add_argument('--develop', required=True, help="the results of G'MIC develop")
    parser.add_argument('--release-renders', help="the release's render work directory")
    parser.add_argument('--develop-renders', help="develop's render work directory")
    parser.add_argument('--release-again', help='the second release renders of the plug-ins whose outputs differ')
    parser.add_argument('--develop-again', help='the second develop renders of the plug-ins whose outputs differ')
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
    trees = tuple(os.path.dirname(os.path.abspath(renders)) if renders else None
                  for renders in (args.release_renders, args.develop_renders))
    sources = _read(args.sources) if args.sources else ''
    diff = differences(release, develop, (args.release_renders, args.develop_renders),
                       (args.release_again, args.develop_again), trees, sources)
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as stream:
            json.dump(diff, stream, indent=1)
    print(markdown(diff, sources), end='')


if __name__ == '__main__':
    main()
