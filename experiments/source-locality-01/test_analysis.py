import csv
import json
from pathlib import Path
import sys
import tempfile
import unittest

repo = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'scripts'))
from prepare_unitproxl_project import prepare
from analyze import analyze
from structural import FIELDS, structural


class LocalityTests(unittest.TestCase):
    def test_cross_page_writes_forward_reads_and_seeded_reuse(self):
        with tempfile.TemporaryDirectory(dir=repo / 'build') as temp:
            path = Path(temp)
            rows = []
            def add(kind, root, **kw):
                event = {key: 0 for key in FIELDS}
                event.update(kind=kind, root=root, file=root, **kw)
                rows.append(event)
            add('B', 1)
            add('D', 1, scope=1, name=11, who=1, object_type=100)
            add('A', 1, scope=1, name=11, who=1, object_type=100, category=1)
            add('W', 1, who=1, endpoint=5, category=1)
            add('A', 1, scope=1, name=11, who=1, object_type=100, category=2, source=2, source_type=200)
            add('W', 1, who=1, endpoint=6, category=2)
            add('E', 1)
            add('B', 2)
            add('D', 2, scope=1, name=12, who=2, object_type=200)
            add('A', 2, scope=1, name=11, who=1, object_type=100, category=1)
            add('W', 2, who=1, endpoint=5, category=1)
            add('E', 2)
            with (path / 'events.csv').open('w', newline='') as stream:
                writer = csv.DictWriter(stream, fieldnames=FIELDS)
                writer.writeheader()
                writer.writerows(rows)
            result = analyze(path)
            self.assertEqual(result['events']['eligible_previous_target'], 1)
            self.assertEqual(result['events']['eligible_declaration_seeded_target'], 2)
            self.assertEqual(result['events']['cross_root_reference_operands'], 1)
            self.assertEqual(result['events']['reference_before_definition'], 1)
            self.assertEqual(result['targets_written_in_multiple_roots'], 1)
            self.assertEqual(result['parameters_written_in_multiple_roots'], 1)
            self.assertEqual(result['contiguous_target_runs']['sum'], 3)

    def test_scope_shadowing_and_string_semicolon(self):
        with tempfile.TemporaryDirectory(dir=repo / 'build') as temp:
            root = Path(temp)
            (root / 'a.page').write_text('T a; a.N = "x;y"; namespace N { T a; a.P = 1; } a.P = 2;')
            project = root / 'project.json'
            project.write_text(json.dumps({'project': [{'type': 'source', 'path': 'a.page'}]}))
            quality, result = structural(project, root / 'out')
            self.assertEqual(quality['quality']['assignments'], 3)
            self.assertEqual(result['source_roots']['working_set_objects']['max'], 2)
            self.assertEqual(result['events']['eligible_previous_target'], 0)
            self.assertFalse(quality['examples'])

    def test_chapter_expansion_preserves_order_and_page_bytes(self):
        with tempfile.TemporaryDirectory(dir=repo / 'build') as temp:
            root = Path(temp)
            source = root / 'original'
            source.mkdir()
            (source / 'main.book').write_bytes(b'#include "a.chap"\n')
            (source / 'a.chap').write_bytes(b'T a;\n#include "b.page"\na.P = 2;\n')
            page = b'T b; b.P = 1;\r\n'
            (source / 'b.page').write_bytes(page)
            old = root / 'old'
            _, count_old = prepare(source / 'main.book', old, 'Types/test.h')
            self.assertEqual(count_old, 1)
            new = root / 'new'
            _, count_new = prepare(source / 'main.book', new, 'Types/test.h', expand_chapters=True)
            self.assertEqual(count_new, 3)
            records = json.loads((new / 'source-transfer.json').read_text())
            self.assertEqual([Path(r['path']).name for r in records],
                             ['a.chap.part1.source', 'b.page', 'a.chap.part2.source'])
            self.assertEqual((new / 'Sources/b.page').read_bytes(), page)
            self.assertEqual((source / 'b.page').read_bytes(), page)


if __name__ == '__main__':
    unittest.main()
