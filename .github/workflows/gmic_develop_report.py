#!/usr/bin/env python3
"""Compares the CI test results of G'MIC develop with those of the pinned G'MIC release (#226).

Reads the results directories of gmic_develop_tests.sh for the release and for develop, and
prints a Markdown summary: the sources (the report of gmic_develop_sources.sh), each test's exit
status, the plug-ins that develop adds or removes, its new describe problems, the render
failures (crashes and timeouts included) and NaN outputs that differ, the failures of the other
tests, and the filters whose command or parameters changed. --json also writes the differences
as JSON, with every list in full.

usage: gmic_develop_report.py --release <dir> --develop <dir> [--sources <report.md>]
                              [--json <file>]
"""
import argparse
import collections
import json
import os
import re

import render_plugins

# the describe test's problems: its own FAIL lines, and the host's and the plug-in's
_DESCRIBE_PROBLEM = re.compile(
    r'^FAIL|DESCRIBE_FAILED|DUPLICATE_(PARAM|CLIP)|has an unknown type|openfx-gmic: Warning')
_RENDER_FAIL = re.compile(r'^FAIL (eu\.gmic\.\w+): (.*)$')
_RENDER_NAN = re.compile(r'^WARNING: (eu\.gmic\.\w+): (\d+) NaN values')
_FAIL_LINE = re.compile(r'^(FAIL|Error|.*\bError:)')
_GUI_FILTER = re.compile(r'^#@gui ([^:_][^:]*?)\s*:\s*(.*)$')
_GUI_PARAM = re.compile(r'^#@gui\s*:\s*(.*)$')
_PARAM = re.compile(r'^([^=]*?)\s*=\s*(.*)$')
# parameters that hold no value (texts, links, separators): their changes are not listed
_NO_VALUE = re.compile(r'^_?(note|link|separator)\s*[({\[]')
_TESTS = ('build', 'describe', 'fft_smoke', 'render', 'user_file', 'update_file', 'user_source')
_OTHER_TESTS = ('fft_smoke', 'user_file', 'update_file', 'user_source')
_MAX_ITEMS = 100  # the longest list printed in the summary (the JSON has them all)

Results = collections.namedtuple('Results', 'status plugins describe renders nans filters logs')


def _read(path):
    """Returns the text of a file, or '' if there is none."""
    try:
        with open(path, encoding='utf-8', errors='replace') as f:
            return f.read().replace('\r', '')
    except OSError:
        return ''


def gui_filters(stdlib_header):
    """Returns the filters of a G'MIC standard library header, by plug-in id.

    Args:
        stdlib_header: a gmic_stdlib_community.h file (none: no filters).

    Returns:
        {id: (commands, {parameter: definition})}: the id as render_plugins.fft_plugins()
        derives it from the filter's name, the text after the name in its '#@gui <name> :'
        line, and the name and definition of each of its '#@gui : <name> = <definition>' lines,
        except those that hold no value (_NO_VALUE: notes, links, separators); a repeated name
        gets primes.
    """
    if not os.path.exists(stdlib_header):
        return {}
    found, params = {}, None
    # (stdlib_text() decodes Latin-1, the files are UTF-8)
    text = render_plugins.stdlib_text(stdlib_header).encode('latin-1').decode('utf-8', 'replace')
    for line in text.split('\n'):
        f = _GUI_FILTER.match(line)
        if f:
            params = collections.OrderedDict()
            found['eu.gmic.' + re.sub(r'[^A-Za-z0-9]', '', f.group(1))] = (f.group(2), params)
            continue
        p = _GUI_PARAM.match(line)
        if p and params is not None:
            m = _PARAM.match(p.group(1))
            name, definition = (m.group(1), m.group(2)) if m else (p.group(1), '')
            if _NO_VALUE.match(definition):
                continue
            while name in params:
                name += "'"
            params[name] = definition
        elif not line.startswith('#@gui_'):  # (the localized lines don't end a filter)
            params = None
    return found


def read_results(directory):
    """Returns the Results in a results directory of gmic_develop_tests.sh."""
    status = {}
    for line in _read(os.path.join(directory, 'status.tsv')).splitlines():
        name, _, code = line.partition('\t')
        status[name] = code
    host_log = _read(os.path.join(directory, 'describe-host.log'))
    plugins = set(re.findall(r'^PLUGIN (\S+)', host_log, re.MULTILINE))
    describe_lines = (host_log + '\n' + _read(os.path.join(directory, 'describe.log'))).splitlines()
    describe = {l.strip() for l in describe_lines if _DESCRIBE_PROBLEM.search(l)}
    render_lines = _read(os.path.join(directory, 'render.log')).splitlines()
    renders = dict(m.groups() for m in map(_RENDER_FAIL.match, render_lines) if m)
    nans = {m.group(1): int(m.group(2)) for m in map(_RENDER_NAN.match, render_lines) if m}
    logs = {t: _read(os.path.join(directory, t + '.log')) for t in _TESTS}
    return Results(status, plugins, describe, renders, nans,
                   gui_filters(os.path.join(directory, 'gmic_stdlib_community.h')), logs)


def compare_filters(old, new):
    """Returns {id: [change...]} for the filters of both old and new that changed.

    Args:
        old, new: gui_filters() results.

    Returns:
        For each changed filter, lines describing the changes of its commands and parameters.
    """
    changes = {}
    for pid in sorted(set(old) & set(new)):
        (old_cmd, old_params), (new_cmd, new_params) = old[pid], new[pid]
        lines = []
        if old_cmd != new_cmd:
            lines.append('commands: `%s` -> `%s`' % (old_cmd, new_cmd))
        for name in old_params:
            if name not in new_params:
                lines.append('removed `%s = %s`' % (name, old_params[name]))
            elif old_params[name] != new_params[name]:
                lines.append('`%s`: `%s` -> `%s`' % (name, old_params[name], new_params[name]))
        lines += ['added `%s = %s`' % (n, d) for n, d in new_params.items() if n not in old_params]
        if [n for n in old_params if n in new_params] != [n for n in new_params if n in old_params]:
            lines.append('parameter order changed')
        if lines:
            changes[pid] = lines
    return changes


def differences(release, develop):
    """Returns the differences between the release's Results and develop's, as a JSON-able dict."""
    def problem(results, pid):
        if pid in results.renders:
            return results.renders[pid]
        return None if pid in results.plugins else 'not a plug-in'

    renders = {}
    for pid in sorted(set(release.renders) | set(develop.renders)):
        before, after = problem(release, pid), problem(develop, pid)
        if before != after and after != 'not a plug-in':
            renders[pid] = {'release': before, 'develop': after}
    nans = {pid: {'release': release.nans.get(pid, 0), 'develop': n}
            for pid, n in sorted(develop.nans.items()) if not release.nans.get(pid)}
    other = {}
    for test in _OTHER_TESTS:
        before = {l for l in release.logs[test].splitlines() if _FAIL_LINE.match(l)}
        new_lines = [l for l in develop.logs[test].splitlines()
                     if _FAIL_LINE.match(l) and l not in before]
        if new_lines:
            other[test] = new_lines
    return {
        'status': {t: {'release': release.status.get(t), 'develop': develop.status.get(t)}
                   for t in _TESTS},
        'plugins': {'release': len(release.plugins), 'develop': len(develop.plugins),
                    'added': sorted(develop.plugins - release.plugins),
                    'removed': sorted(release.plugins - develop.plugins)},
        'describe': sorted(develop.describe - release.describe),
        'renders': renders,
        'nans': nans,
        'other_tests': other,
        'filters': compare_filters(release.filters, develop.filters),
    }


def _items(lines):
    """Returns Markdown list lines for lines, cut at _MAX_ITEMS."""
    out = ['- ' + l for l in lines[:_MAX_ITEMS]]
    if len(lines) > _MAX_ITEMS:
        out.append('- ... (%d more, see summary.json)' % (len(lines) - _MAX_ITEMS))
    return out


def markdown(diff, sources):
    """Returns the Markdown summary of differences() (and of the sources' report, if any)."""
    out = ["## G'MIC develop vs the pinned release", '']
    if sources:
        out += [sources.strip(), '']
    out += ['### Tests (exit status)', '', '| Test | Release | Develop |', '|---|---|---|']
    out += ['| %s | %s | %s |' % (t, s['release'] or 'not run', s['develop'] or 'not run')
            for t, s in diff['status'].items()]
    plugins = diff['plugins']
    out += ['', '### Plug-ins: %d (release), %d (develop)' % (plugins['release'], plugins['develop']), '']
    out += ['Added: %d' % len(plugins['added'])] + _items(plugins['added'])
    out += ['', 'Removed: %d' % len(plugins['removed'])] + _items(plugins['removed'])
    out += ['', '### Describe problems (develop only): %d' % len(diff['describe']), '']
    out += _items(['`%s`' % l for l in diff['describe']])
    out += ['', '### Renders that differ: %d' % len(diff['renders']), '']
    out += _items(['%s: release: %s; develop: %s' % (pid, r['release'] or 'rendered', r['develop'] or 'rendered')
                   for pid, r in diff['renders'].items()])
    out += ['', '### New NaN outputs: %d' % len(diff['nans']), '']
    out += _items(['%s: %d NaN values' % (pid, n['develop']) for pid, n in diff['nans'].items()])
    out += ['', '### Other tests: new failure lines', '']
    for test, lines in diff['other_tests'].items():
        out += ['%s:' % test] + _items(['`%s`' % l for l in lines]) + ['']
    out += ['', '### Filters whose commands or parameters changed: %d' % len(diff['filters']), '']
    for pid, lines in list(diff['filters'].items())[:_MAX_ITEMS]:
        out += ['- %s: %s' % (pid, '; '.join(lines))]
    if len(diff['filters']) > _MAX_ITEMS:
        out.append('- ... (%d more, see summary.json)' % (len(diff['filters']) - _MAX_ITEMS))
    return '\n'.join(out) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__,
                                     formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument('--release', required=True, help='the results of the pinned release')
    parser.add_argument('--develop', required=True, help='the results of G\'MIC develop')
    parser.add_argument('--sources', help="gmic_develop_sources.sh's report")
    parser.add_argument('--json', help='where to write the differences, as JSON')
    args = parser.parse_args()
    diff = differences(read_results(args.release), read_results(args.develop))
    if args.json:
        with open(args.json, 'w', encoding='utf-8') as f:
            json.dump(diff, f, indent=1)
    print(markdown(diff, _read(args.sources) if args.sources else ''), end='')


if __name__ == '__main__':
    main()
