"""Source-only locality. Names/scopes are symbolic; no Header type/WHO validation."""
import argparse
from collections import Counter
import csv
import json
from pathlib import Path
import re
import sys

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'scripts'))
from prepare_unitproxl_headers import TOKEN
from analyze import analyze

IDENT = re.compile(rb'^[A-Za-z_][A-Za-z_0-9]*$')
FIELDS = 'kind root file scope name who object_type leaf_type source source_type endpoint category offset length'.split()


def structural(project, output):
    project, output = Path(project).resolve(), Path(output).resolve()
    output.mkdir(exist_ok=False, parents=True)
    def source_nodes(nodes):
        for node in nodes:
            if node['type'] == 'group':
                yield from source_nodes(node['children'])
            elif node['type'] == 'source':
                yield project.parent / node['path']
    paths = list(source_nodes(json.loads(project.read_text(encoding='utf-8'))['project']))
    ids, bindings, endpoint_ids = {}, {}, {}
    quality = Counter()
    examples = []
    def intern(value):
        if value not in ids:
            ids[value] = len(ids) + 1
        return ids[value]
    def visible(scope, name):
        for depth in range(len(scope), -1, -1):
            if (scope[:depth], name) in bindings:
                return bindings[(scope[:depth], name)]
        return (0, 0)
    with (output / 'events.csv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS)
        writer.writeheader()
        def emit(kind, root, **kw):
            row = {k: 0 for k in FIELDS}
            row.update(kind=kind, root=root, file=root, **kw)
            writer.writerow(row)
        def unknown(path, offset, tokens):
            quality['unclassified_statements'] += 1
            if len(examples) < 12:
                examples.append({'path': str(path), 'offset': offset,
                                 'tokens': b' '.join(tokens[:25]).decode('ascii', errors='replace')})
        for root, path in enumerate(paths, 1):
            emit('B', root)
            tokens = [(m.group(), m.start()) for m in TOKEN.finditer(path.read_bytes())
                      if not m.group().startswith((b'//', b'/*'))]
            stack, scope, i = [], (), 0
            while i < len(tokens):
                if tokens[i][0] == b'namespace':
                    names = []
                    i += 1
                    while i < len(tokens) and tokens[i][0] != b'{':
                        if IDENT.fullmatch(tokens[i][0]): names.append(tokens[i][0])
                        elif tokens[i][0] != b':': raise ValueError('Unsupported namespace syntax')
                        i += 1
                    if i == len(tokens): raise ValueError('Unclosed namespace')
                    stack.append(scope)
                    scope += tuple(names)
                    emit('N', root, scope=intern(scope))
                    i += 1
                    continue
                if tokens[i][0] == b'}':
                    if not stack: raise ValueError('Unmatched scope brace')
                    scope = stack.pop()
                    emit('N', root, scope=intern(scope))
                    i += 1
                    continue
                offset = tokens[i][1]
                statement = []
                braces = 0
                while i < len(tokens):
                    token = tokens[i][0]
                    i += 1
                    if token == b';' and braces == 0: break
                    braces += (token == b'{') - (token == b'}')
                    statement.append(token)
                if not statement:
                    continue
                quality['statements'] += 1
                s = statement
                if len(s) > 1 and IDENT.fullmatch(s[0]) and s[1] in (b'.', b'[', b'='):
                    if b'=' not in s:
                        unknown(path, offset, s)
                        continue
                    equals = s.index(b'=')
                    who, type_id = visible(scope, s[0])
                    if not who: quality['unresolved_targets'] += 1
                    rhs = s[equals + 1:]
                    source, source_type = 0, 0
                    category = 1
                    if rhs and rhs[0].startswith(b'"'):
                        category = 3
                    elif rhs and IDENT.fullmatch(rhs[0]) and rhs[0] not in (b'true', b'false', b'nullptr'):
                        source, source_type = visible(scope, rhs[0])
                        category = 2 if source else 4
                        if not source: quality['unresolved_identifier_operands'] += 1
                    if not rhs: quality['empty_operands'] += 1
                    endpoint = tuple(s[1:equals])
                    if endpoint not in endpoint_ids:
                        endpoint_ids[endpoint] = len(endpoint_ids) + 1
                    ep = endpoint_ids[endpoint]
                    emit('A', root, scope=intern(scope), name=intern(s[0]), who=who, object_type=type_id,
                         source=source, source_type=source_type, category=category, offset=offset)
                    emit('W', root, who=who, endpoint=ep, category=category, offset=offset)
                    quality['assignments'] += 1
                    continue
                pos = 0
                extern = s[0] == b'extern'
                if extern: pos += 1
                start = pos
                while pos < len(s) and s[pos] in (b'const', b'volatile'): pos += 1
                if pos >= len(s) or not IDENT.fullmatch(s[pos]):
                    unknown(path, offset, s)
                    continue
                intrinsic = s[pos] in (b'unsigned', b'signed', b'long', b'short')
                pos += 1
                if intrinsic:
                    while pos < len(s) and s[pos] in (b'int', b'char', b'long', b'short', b'double'):
                        pos += 1
                else:
                    while pos + 2 < len(s) and s[pos:pos + 2] == [b':', b':'] and IDENT.fullmatch(s[pos + 2]):
                        pos += 3
                while pos < len(s) and s[pos] in (b'*', b'&', b'const', b'volatile'): pos += 1
                if pos >= len(s) or not IDENT.fullmatch(s[pos]):
                    unknown(path, offset, s)
                    continue
                name = s[pos]
                if b',' in s[pos + 1:]:
                    unknown(path, offset, s)
                    continue
                type_id = intern(tuple(s[start:pos]))
                key = (scope, name)
                if key in bindings:
                    who, old_type = bindings[key]
                    if type_id != old_type: quality['nominal_declaration_type_changes'] += 1
                else:
                    who = len(bindings) + 1
                bindings[key] = who, type_id
                emit('D', root, scope=intern(scope), name=intern(name), who=who, object_type=type_id,
                     category=int(extern), offset=offset)
                quality['declarations'] += 1
            if stack: raise ValueError('Unclosed namespace at Source root boundary')
            emit('E', root)
    with (output / 'files.csv').open('w', newline='', encoding='utf-8') as stream:
        writer = csv.writer(stream)
        writer.writerow(['file', 'path'])
        writer.writerows(enumerate(map(str, paths), 1))
    report = {'representation': 'Source-only symbolic names/scopes; no Header type checking or semantic WHO parity',
              'project': str(project), 'source_roots': len(paths), 'quality': dict(quality), 'examples': examples,
              'write_keys': 'syntactic parameter selectors; string writes are not expanded into character elements'}
    (output / 'quality.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if quality['unresolved_targets']:
        raise ValueError('Unresolved targets: structural results are incomplete; see quality.json')
    summary = analyze(output, structural=True)
    return report, summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('project', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    report, summary = structural(args.project, args.output)
    print(json.dumps({'quality': report, 'eligible_previous_target_pct': summary['eligible_previous_target_pct']}, indent=2))
