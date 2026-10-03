#!/usr/bin/env python3
"""Inventory literal SP cvar registrations/uses; generate reviewed reference pages.

This is a lexical inventory, not a C++ preprocessor or runtime-default resolver.
Balanced token parsing preserves expressions and reports computed names separately.
"""
import argparse
import ast
from collections import defaultdict
from dataclasses import dataclass
import json
from pathlib import Path
import re
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
DOC = ROOT / "OpenJK/docs/cvars"
ROOTS = ("OpenJK/JKXR", "OpenJK/code", "OpenJK/codeJK2", "OpenJK/shared")
GROUPS = {
    "vr": "VR controls and comfort", "vulkan": "Vulkan rendering",
    "graphics": "Other graphics and effects", "gameplay": "Gameplay and weapons",
    "hud": "HUD and camera", "audio": "Audio and cinematics",
    "input": "Input and console", "ui": "Menus and customization",
    "engine": "Engine, files and networking", "diagnostics": "Diagnostics",
    "legacy": "Legacy renderer only",
}
TOKEN = re.compile(r'//[^\n]*|/\*[\s\S]*?\*/|(?:u8|[LuU])?"(?:\\.|[^"\\])*"|'
                   r"'(?:\\.|[^'\\])*'|[A-Za-z_]\w*|\d+(?:\.\d+)?|[^\s]", re.M)
STRING = re.compile(r'(?:u8|[LuU])?"(?:\\.|[^"\\])*"\Z')
GET = {"Cvar_Get", "cvar", "Cvar_Create", "G_Cvar_Create", "UI_Cvar_Create"}
REGISTER = {"Cvar_Register", "cgi_Cvar_Register", "trap_Cvar_Register"}
READ = {"Cvar_VariableValue", "Cvar_VariableIntegerValue", "Cvar_VariableString",
        "Cvar_VariableStringBuffer", "Cvar_String", "Cvar_VariableInteger",
        "cgi_Cvar_VariableValue", "cgi_Cvar_VariableStringBuffer",
        "Cvar_Set", "Cvar_SetValue", "Cvar_Set2", "Cvar_ForceSet",
        "Cvar_Reset", "Cvar_SetValueSafe", "cgi_Cvar_Get", "cgi_Cvar_Set",
        "cvar_set", "trap_Cvar_Set", "trap_Cvar_VariableValue",
        "UI_Cvar_VariableString", "Cvar_FindVar", "Cvar_Flags", "Cvar_ForceReset"}


@dataclass
class Token:
    text: str
    line: int


def tokenize(source):
    # Remove directives from the token stream, retaining their line positions.
    source = re.sub(r'^\s*#[^\n]*(?:\\\n[^\n]*)*',
                    lambda m: '\n' * m[0].count('\n'), source, flags=re.M)
    result = []
    line, previous = 1, 0
    for match in TOKEN.finditer(source):
        line += source.count('\n', previous, match.start())
        value = match[0]
        if not value.startswith(('//', '/*')):
            result.append(Token(value, line))
        line += value.count('\n')
        previous = match.end()
    return result


def expression(tokens):
    return ' '.join(t.text for t in tokens)


def literal(tokens):
    if not tokens or any(not STRING.fullmatch(t.text) for t in tokens):
        return None
    try:
        return ''.join(ast.literal_eval(re.sub(r'^(?:u8|[LuU])', '', t.text)) for t in tokens)
    except (SyntaxError, ValueError):
        return None


def balanced(tokens, start):
    """Split top-level comma-separated arguments/initializers, respecting nesting."""
    pairs = {'(': ')', '[': ']', '{': '}'}
    stack = [pairs[tokens[start].text]]
    fields, begin = [], start + 1
    for i in range(start + 1, len(tokens)):
        text = tokens[i].text
        if text in pairs:
            stack.append(pairs[text])
        elif text in pairs.values():
            if text != stack[-1]:
                raise ValueError(f'unbalanced source near line {tokens[i].line}')
            stack.pop()
            if not stack:
                fields.append(tokens[begin:i])
                return fields, i
        elif text == ',' and len(stack) == 1:
            fields.append(tokens[begin:i])
            begin = i + 1
    raise ValueError(f'unclosed expression near line {tokens[start].line}')


def conditions(source):
    stack, result = [], {}
    for number, line in enumerate(source.splitlines(), 1):
        match = re.match(r'\s*#\s*(if|ifdef|ifndef|elif|else|endif)\b(.*)', line)
        if match:
            kind, value = match.groups()
            if kind in ('if', 'ifdef', 'ifndef'):
                stack.append([f'#{kind} {value.strip()}'])
            elif kind in ('else', 'elif') and stack:
                stack[-1].append(f'#{kind} {value.strip()}'.strip())
            elif kind == 'endif' and stack:
                stack.pop()
        result[number] = [' -> '.join(branch) for branch in stack]
    return result


def scope(path):
    if '/rd-vanilla/' in path or '/rd-gles/' in path:
        return 'legacy renderer source (not Vulkan)'
    if path.startswith('OpenJK/codeJK2/'):
        return 'JKO source'
    if path.startswith(('OpenJK/code/game/', 'OpenJK/code/cgame/')):
        return 'JKA source'
    return 'shared source; conditional availability'


def scan_source(source, path):
    tokens = tokenize(source)
    guards = conditions(source)
    lines = source.splitlines()
    records, unresolved = [], []

    def add(name_tokens, default, flags, kind, line, end_line):
        name = literal(name_tokens)
        if name is None:
            unresolved.append(dict(path=path, line=line, kind=kind,
                                   name_expression=expression(name_tokens)))
            return
        if not name or re.search(r'\s', name):
            return
        note = ''
        tail = lines[end_line-1] if end_line <= len(lines) else ''
        # Source comments are quoted as hints, never promoted to verified behavior.
        if '//' in tail:
            note = tail.split('//', 1)[1].strip()
        row = dict(name=name, path=path, line=line, kind=kind, scope=scope(path),
                   conditions=guards.get(line, []))
        if default is not None:
            value = literal(default)
            branches = {tuple(guards.get(t.line, [])) for t in default}
            if len(branches) > 1:
                value = None
                row['argument_conditions'] = [list(b) for b in sorted(branches)]
            row['default'] = value if value is not None else expression(default)
            row['default_is_literal'] = value is not None
            row['flags'] = expression(flags)
        if note:
            row['source_comment'] = note
        records.append(row)

    for i, token in enumerate(tokens):
        api = token.text
        if api in GET | REGISTER | READ and i+1 < len(tokens) and tokens[i+1].text == '(':
            args, end = balanced(tokens, i+1)
            # Function definitions/prototypes are not invocations.
            if end+1 < len(tokens) and tokens[end+1].text == '{':
                continue
            if api in GET and len(args) >= 3:
                # Only treat lower-case cvar as the game import API.
                if api == 'cvar' and (i == 0 or tokens[i-1].text not in ('.', '>')):
                    continue
                add(args[0], args[1], args[2], 'registration', token.line, tokens[end].line)
            elif api in REGISTER and len(args) >= 4:
                add(args[1], args[2], args[3], 'registration', token.line, tokens[end].line)
            elif api in READ and args:
                # Includes setters: registration elsewhere is not assumed.
                add(args[0], None, None, 'read/set reference', token.line, tokens[end].line)
        if api == 'cvarTable' and i+1 < len(tokens) and tokens[i+1].text == '[':
            _, end_array = balanced(tokens, i+1)
            if [t.text for t in tokens[end_array+1:end_array+3]] != ['=', '{']:
                continue
            rows, _ = balanced(tokens, end_array+2)
            for row in rows:
                if not row or row[0].text != '{':
                    continue
                fields, end = balanced(row, 0)
                if len(fields) in (3, 4, 5):
                    flags = fields[-1] if len(fields) > 3 else [Token('0', row[0].line)]
                    add(fields[1], fields[2], flags, 'registration table',
                        row[0].line, row[end].line)
                else:
                    raise ValueError(f'{path}:{row[0].line}: unknown cvar table layout')
    return records, unresolved


def category(name, entries):
    n = name.lower()
    if all(e['scope'].startswith('legacy') for e in entries): return 'legacy'
    if n.startswith('vr_'): return 'vr'
    if n.startswith(('r_vulkan', 'rd_vulkan')): return 'vulkan'
    if n.startswith('ui_'): return 'ui'
    if n.startswith(('s_', 's_music', 'sfx', 'cl_avidemo', 'cl_ingamevideo', 'saberanim')): return 'audio'
    if n.startswith(('in_', 'm_', 'joy_', 'con_', 'cl_mouse', 'sensitivity', 'cl_run', 'cl_yaw', 'cl_pitch')): return 'input'
    if n.startswith('cg_'): return 'hud'
    if n.startswith('r_'): return 'graphics'
    if n.startswith(('g_', 'timescale', 'helpusobi', 'player', 'npc_', 'd_npc', 'd_saber')): return 'gameplay'
    if n.startswith(('debug', 'developer', 'd_', 'show', 'r_show', 'cm_')): return 'diagnostics'
    return 'engine'


def normalize_names(grouped):
    folded = defaultdict(list)
    for name, entries in grouped.items():
        for entry in entries:
            folded[name.lower()].append(dict(entry, spelling=name))
    result = {}
    for entries in folded.values():
        registrations = [e for e in entries if 'default' in e]
        current = [e for e in registrations if not e['scope'].startswith('legacy')]
        name = (current or registrations or entries)[0]['spelling']
        result[name] = dict(group=category(name, entries),
                            spellings=sorted({e['spelling'] for e in entries}),
                            occurrences=entries)
    return dict(sorted(result.items(), key=lambda v: v[0].lower()))


def inventory(root=ROOT):
    grouped, unresolved = defaultdict(list), []
    for directory in ROOTS:
        for path in sorted((root / directory).rglob('*')):
            if path.suffix not in ('.c', '.cpp', '.h', '.hpp'): continue
            records, unknown = scan_source(path.read_text(errors='replace'), path.relative_to(root).as_posix())
            for record in records:
                grouped[record.pop('name')].append(record)
            unresolved.extend(unknown)
    return dict(schema=1, source_roots=list(ROOTS),
                variables=normalize_names(grouped),
                unresolved=unresolved)


def code(value):
    return '`' + str(value).replace('`', "'").replace('|', '&#124;').replace('\n', '\\n') + '`'


def render(data, notes, sources):
    unknown = set(notes) - set(data['variables'])
    if unknown:
        raise ValueError('review notes without inventory entry: ' + ', '.join(sorted(unknown)))
    pages = {}
    for group, title in GROUPS.items():
        text = [f'# {title}', '', '[Reference guide](../console-variables.md) | [Index](README.md)', '',
                'Generated by `tools/cvar_reference.py`; edit `reviewed.json`, not this file.', '',
                'Defaults below are registration arguments, not necessarily effective runtime defaults.',
                'Source scope is not proof of availability in a particular compiled game. Conditions are unevaluated.', '']
        for name, entry in data['variables'].items():
            if entry['group'] != group: continue
            text += [f'## {name}', '']
            if len(entry['spellings']) > 1:
                text += ['Case-insensitive source spellings: ' + ', '.join(map(code, entry['spellings'])), '']
            note = notes.get(name)
            if note:
                text += [f"**{note.get('status', 'Source reviewed')}**: {note['description']}", '']
                if note.get('values'): text += [f"Values/units: {note['values']}", '']
                if note.get('caution'): text += [f"Caveat: {note['caution']}", '']
                evidence_paths = note.get('evidence', sources.get(group, []))
                if not evidence_paths:
                    raise ValueError('review without sources: ' + name)
                for evidence in evidence_paths:
                    if not (ROOT / evidence).is_file():
                        raise ValueError('missing review evidence: ' + evidence)
                text += ['Review sources: ' + ', '.join(f'[{p}](../../{p.removeprefix("OpenJK/")})'
                           if p.startswith('OpenJK/') else f'[{p}](../../../{p})'
                           for p in evidence_paths), '']
            else:
                text += ['**Unverified behavior**: inventoried from source; purpose, accepted values, '
                         'units and effect in Vulkan have not yet been reviewed. Do not infer a safe range from the default.', '']
            registrations = [o for o in entry['occurrences'] if 'default' in o]
            if not registrations:
                text += ['No literal registration was found in the scanned sources; default and flags are unknown.', '']
            for o in entry['occurrences']:
                label = f"{o['path']}:{o['line']}"
                link = '../../' + o['path'].removeprefix('OpenJK/') + f"#L{o['line']}"
                fact = f"- [{label}]({link}) ({o['scope']}; {o['kind']})"
                if 'default' in o:
                    fact += f": default {code(o['default'])}"
                    if not o['default_is_literal']: fact += ' (expression, not resolved)'
                    fact += f"; flags {code(o['flags'])}"
                if o['conditions']: fact += '; guards ' + code(' / '.join(o['conditions']))
                if o.get('argument_conditions'):
                    fact += '; argument branches ' + code(o['argument_conditions'])
                text.append(fact)
                if o.get('source_comment'):
                    text.append('  Source comment (unverified): ' + code(o['source_comment']))
            text.append('')
        pages[f'{group}.md'] = '\n'.join(text).rstrip() + '\n'
    index = ['# Console variable index', '', '[Start with the reference guide](../console-variables.md).', '',
             'Generated source inventory. No game configuration is changed by these tools.', '',
             f"{len(data['variables'])} distinct literal names; {len(notes)} reviewed entries; "
             f"{len(data['variables'])-len(notes)} entries awaiting behavior review.", '',
             '## Categories', '']
    for group, title in GROUPS.items():
        count = sum(v['group'] == group for v in data['variables'].values())
        index.append(f'- [{title}]({group}.md): {count}')
    index += ['', '## Alphabetical index', '', '| Variable | Category | Review |', '| --- | --- | --- |']
    for name, value in data['variables'].items():
        index.append(f"| [{name}]({value['group']}.md#{name.lower()}) | {GROUPS[value['group']]} | "
                     f"{notes[name].get('status', 'Source reviewed') if name in notes else 'Unverified'} |")
    index += ['', '## Coverage limits', '',
              'The scan includes both SP games, shared code, VR and legacy renderer sources. '
              'It excludes multiplayer (`codemp`), third-party libraries, assets/menu scripts, '
              'launcher arguments and user configuration. C/C++ directives are recorded, not evaluated. '
              'A registration or reference is not a guarantee the variable is used in the running build.', '',
              'Computed names and wrapper calls below are not silently counted as fully covered. '
              'Some are declarations or generic APIs rather than missing variables. '
              'Runtime `cvarlist` is the complementary check for the loaded game/mod.', '',
              '### Unresolved names', '']
    for row in data['unresolved']:
        index.append(f"- {code(row['path'] + ':' + str(row['line']))}: {row['kind']} {code(row['name_expression'])}")
    pages['README.md'] = '\n'.join(index) + '\n'
    pages['inventory.json'] = json.dumps(data, indent=2, ensure_ascii=True) + '\n'
    return pages


def stale_files(directory, pages):
    return [name for name, content in pages.items()
            if not (directory / name).is_file() or (directory / name).read_text() != content]


def validate_links(pages):
    targets = {DOC / name for name in pages}
    texts = [(DOC / name, content) for name, content in pages.items() if name.endswith('.md')]
    guide = DOC.parent / 'console-variables.md'
    texts.append((guide, guide.read_text()))
    for page, content in texts:
        for link in re.findall(r'\]\(([^)]+)\)', content):
            if '://' in link or link.startswith('#'):
                continue
            target = (page.parent / link.split('#', 1)[0]).resolve()
            if target not in targets and not target.is_file():
                raise ValueError(f'{page}: broken link {link}')


class ScannerTests(unittest.TestCase):
    def test_calls(self):
        rows, _ = scan_source('// Cvar_Get("fake", "1", 0);\n'
            'ri.Cvar_Get("real" "Name", choose(1, 2), CVAR_ARCHIVE | CVAR_LATCH);', 'test.cpp')
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['name'], 'realName')
        self.assertEqual(rows[0]['line'], 2)
        self.assertEqual(rows[0]['default'], 'choose ( 1 , 2 )')

    def test_tables_and_conditions(self):
        rows, _ = scan_source('cvarTable_t cvarTable[] = {\n#ifdef JK2_MODE\n'
            '{ &x, "foo", "1", CVAR_ARCHIVE },\n#else\n'
            '{ &x, "foo", "2", NULL, 0 },\n#endif\n};', 'test.cpp')
        self.assertEqual([r['default'] for r in rows], ['1', '2'])
        self.assertEqual(rows[1]['conditions'], ['#ifdef JK2_MODE -> #else'])
        self.assertEqual(rows[0]['line'], 3)

    def test_dynamic_and_reads(self):
        rows, unresolved = scan_source('gi.cvar(va("test_%d", n), "0", 0); '
                                      'Cvar_VariableIntegerValue("known");', 'test.cpp')
        self.assertEqual(rows[0]['name'], 'known')
        self.assertEqual(len(unresolved), 1)

    def test_comments_strings_and_registration(self):
        rows, _ = scan_source('/* Cvar_Get("no", "1", 0); */\n'
            'const char *s="Cvar_Get(\\"no\\",...)";\n'
            'Cvar_Register(&x, "yes", "a,b", 0);', 'test.cpp')
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0]['default'], 'a,b')
        self.assertEqual(rows[0]['line'], 3)

    def test_implicit_flags(self):
        rows, _ = scan_source('cvarTable_t cvarTable[] = {{&x, "foo", "0"}};', 'test.cpp')
        self.assertEqual(rows[0]['flags'], '0')

    def test_case_insensitive_names(self):
        rows, _ = scan_source('Cvar_Get("SomeName", "1", 0); Cvar_Set("somename", "2");', 'test.cpp')
        grouped = defaultdict(list)
        for row in rows:
            grouped[row.pop('name')].append(row)
        result = normalize_names(grouped)
        self.assertEqual(list(result), ['SomeName'])
        self.assertEqual(result['SomeName']['spellings'], ['SomeName', 'somename'])

    def test_drift_detects_missing_and_changed_files(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp)
            pages = {'test.md': 'expected\n'}
            self.assertEqual(stale_files(path, pages), ['test.md'])
            (path / 'test.md').write_text('expected\n')
            self.assertEqual(stale_files(path, pages), [])
            (path / 'test.md').write_text('old\n')
            self.assertEqual(stale_files(path, pages), ['test.md'])

    def test_default_and_flags_changes_affect_inventory(self):
        a, _ = scan_source('Cvar_Get("test", "0", 0);', 'test.cpp')
        b, _ = scan_source('Cvar_Get("test", "1", CVAR_ARCHIVE);', 'test.cpp')
        self.assertNotEqual(a, b)

    def test_ui_create_and_get_wrappers(self):
        rows, _ = scan_source('ui.Cvar_Create("foo", "1", CVAR_ARCHIVE); '
                              'cgi_Cvar_Get("foo");', 'test.cpp')
        self.assertEqual(len(rows), 2)
        self.assertEqual(rows[0]['default'], '1')
        self.assertNotIn('default', rows[1])

    def test_conditional_default_is_not_string_concatenation(self):
        rows, _ = scan_source('Cvar_Get("foo",\n#ifdef JK2_MODE\n"1"\n#else\n"2"\n#endif\n,0);', 'test.cpp')
        self.assertFalse(rows[0]['default_is_literal'])
        self.assertEqual(len(rows[0]['argument_conditions']), 2)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--write', action='store_true')
    mode.add_argument('--check', action='store_true')
    mode.add_argument('--self-test', action='store_true')
    args = parser.parse_args()
    if args.self_test:
        suite = unittest.defaultTestLoader.loadTestsFromTestCase(ScannerTests)
        return 0 if unittest.TextTestRunner().run(suite).wasSuccessful() else 1
    reviews = json.loads((DOC / 'reviewed.json').read_text())
    notes = dict(reviews['variables'])
    data = inventory()
    for pattern, note in reviews.get('families', {}).items():
        matches = [name for name in data['variables'] if re.fullmatch(pattern, name)]
        if not matches:
            raise ValueError('review family without inventory matches: ' + pattern)
        for name in matches:
            notes.setdefault(name, note)
    pages = render(data, notes, reviews['sources'])
    validate_links(pages)
    stale = stale_files(DOC, pages) if args.check else []
    for filename, content in pages.items():
        path = DOC / filename
        if args.write:
            path.write_text(content)
    if stale:
        print('Stale cvar reference: ' + ', '.join(stale), file=sys.stderr)
        print('Review source/default changes and run python3 tools/cvar_reference.py --write', file=sys.stderr)
        return 1
    print(f"Cvar reference {'generated' if args.write else 'current'}: {len(pages)} files, {len(notes)} reviewed entries")
    return 0


if __name__ == '__main__':
    sys.exit(main())
