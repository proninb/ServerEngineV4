"""Prepare read-only legacy inputs as isolated V4 fixtures; preserve Source bytes."""
import collections
import hashlib
import json
import pathlib
import re
import sys

repo = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'scripts'))
from prepare_unitproxl_headers import strip_annotations
from prepare_unitproxl_project import prepare


def prepare_corpus(source, name, book):
    source = pathlib.Path(source).resolve()
    destination = repo / 'build/source-locality-01/fixtures' / name
    assert not destination.exists()
    destination.mkdir(parents=True)
    records = []
    removed = collections.Counter()
    declarations = b''
    originals = []
    source_inputs = {path: hashlib.sha256(path.read_bytes()).hexdigest()
                     for path in (source / book).parent.rglob('*') if path.is_file() and
                     path.suffix.lower() in ('.book', '.chap', '.page', '.ogd')}
    for path in sorted((source / 'Types').rglob('*')):
        if not path.is_file():
            continue
        data = path.read_bytes()
        originals.append(data)
        cleaned, counts = strip_annotations(data) if path.suffix.lower() in ('.h', '.chap') else (data, {})
        target = destination / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(cleaned)
        removed.update(counts)
        records.append({'path': str(path), 'source_sha256': hashlib.sha256(data).hexdigest(),
                        'output_sha256': hashlib.sha256(cleaned).hexdigest(), 'removed': dict(counts)})
    for symbol in (b'SimObject', b'SimLink'):
        if not any(re.search(rb'\b(?:struct|class)\s+' + symbol + rb'\s*\{', data) for data in originals):
            declarations += b'struct ' + symbol + b' {};\n'
    if not any(re.search(rb'\btypedef\s+unsigned\s+char\s+byte\b', data) for data in originals):
        declarations += b'typedef unsigned char byte;\n'
    (destination / 'Types/source_locality_sdk.h').write_bytes(b'#pragma once\n' + declarations)
    types = (source / 'Books/Project/Types.chap').read_bytes()
    includes = re.findall(rb'^\s*#include\s*([<"][^\r\n]+?[>"])', types, re.M)
    assert includes
    umbrella = b'#pragma once\n#include "source_locality_sdk.h"\n' + b''.join(b'#include ' + x + b'\n' for x in includes)
    (destination / 'Types/source_locality_root.h').write_bytes(umbrella)
    inputs, roots = prepare(source / book, destination, 'Types/source_locality_root.h', expand_chapters=True)
    report = {'source': str(source), 'fixture': str(destination), 'book': book,
              'inputs': inputs, 'source_roots': roots, 'removed': dict(removed),
              'source_inputs': {str(path): digest for path, digest in source_inputs.items()},
              'sdk_declarations': declarations.decode('ascii'), 'types': records}
    (destination / 'preparation.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    # Verify that preparation did not change any original Type or Source bytes.
    for record in records:
        assert hashlib.sha256(pathlib.Path(record['path']).read_bytes()).hexdigest() == record['source_sha256']
    for record in json.loads((destination / 'source-transfer.json').read_text()):
        assert record['sha256'] == record['original_sha256']
    for path, digest in source_inputs.items():
        assert hashlib.sha256(path.read_bytes()).hexdigest() == digest
    print(json.dumps({k: v for k, v in report.items() if k != 'types'}, indent=2))


if __name__ == '__main__':
    prepare_corpus(r'C:\Boris\CW\UnitProXL_V2\Project', 'unitproxl-v2', 'Books/UnityProXL/UnityProXL.book')
    prepare_corpus(r'C:\Boris\CW\ABB\Project\Project', 'abb', 'Books/ABB/ABB.book')
