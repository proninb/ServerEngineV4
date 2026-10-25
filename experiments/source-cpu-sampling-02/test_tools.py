import pathlib
import sys
import tempfile
import unittest
from analyze import aggregate
from symbols import pe_info,verify

class Accounting(unittest.TestCase):
    def test_recursion_and_inclusive_union(self):
        rows=[dict(block=0,pid=7,count=3,weight_ms=3,frames=['parse_source_roots_v2','source_parser_v2::read_endpoint','source_parser_v2::read_endpoint','source_map::add_dependency']),
              dict(block=0,pid=7,count=2,weight_ms=1,frames=['parse_source_roots_v2','source_parser_v2::advance']),
              dict(block=0,pid=8,count=100,weight_ms=100,frames=['source_parser_v2::advance'])]
        result=aggregate(rows,{(0,7)});g=result['groups']
        self.assertEqual(result['quality']['source_samples'],5)
        self.assertEqual(g['endpoint']['inclusive_samples'],3)
        self.assertEqual(g['endpoint']['exclusive_samples'],0)
        self.assertEqual(g['provenance']['exclusive_samples'],3)
        self.assertEqual(g['cursor']['exclusive_ms'],1)
        self.assertEqual(result['functions']['source_parser_v2::read_endpoint']['inclusive_samples'],3)
    def test_missing_non_source_and_unresolved(self):
        result=aggregate([dict(block=0,pid=7,count=1,weight_ms=1,frames=[]),
                          dict(block=0,pid=7,count=2,weight_ms=2,frames=['main','runtime_layout']),
                          dict(block=0,pid=7,count=3,weight_ms=3,frames=['parse_source_roots_v2','ServerEngineV4PublishBenchmark.exe+0x123'])],{(0,7)})
        self.assertEqual(result['quality']['target_samples'],6)
        self.assertEqual(result['quality']['source_samples'],3)
        self.assertEqual(result['quality']['missing_stack_samples'],1)
        self.assertEqual(result['quality']['unresolved_application_samples'],3)
    def test_real_pdb(self):
        exe=pathlib.Path(__file__).resolve().parents[2]/'build/source-cpu-sampling-02/symbols/Release/ServerEngineV4PublishBenchmark.exe'
        if not exe.exists():self.skipTest('build the optimized symbols executable first')
        self.assertTrue(verify(exe,exe.with_suffix('.pdb'))['match'])
        data=bytearray(exe.read_bytes());pos=data.index(b'RSDS');data[pos+4]^=1
        test_root=exe.parents[2]/'tests';test_root.mkdir(exist_ok=True)
        self.assertTrue(test_root.resolve().is_relative_to(pathlib.Path(__file__).resolve().parents[2]/'build'))
        with tempfile.TemporaryDirectory(dir=test_root) as folder:
            wrong=pathlib.Path(folder)/'wrong.bin';wrong.write_bytes(data)
            self.assertNotEqual(pe_info(wrong)['guid'],pe_info(exe)['guid'])
            with self.assertRaisesRegex(AssertionError,'PDB does not match'):verify(wrong,exe.with_suffix('.pdb'))

if __name__=='__main__':unittest.main()
