import unittest
from compare_maps import by_base,deltas,identity_gate
def region(base=1,size=4096,state=0x1000,kind=0x20000):return dict(allocation_base=base,size_bytes=size,state=state,type=kind)
class Contract(unittest.TestCase):
 def test_commit(self):self.assertEqual(by_base([region()])[1]['commit'],4096)
 def test_reserve(self):self.assertEqual(by_base([region(state=0x2000)])[1]['reserve'],4096)
 def test_other_types(self):self.assertFalse(by_base([region(kind=0x40000)]))
 def test_aggregate(self):self.assertEqual(by_base([region(),region()])[1]['commit'],8192)
 def test_zero_size(self):
  with self.assertRaises(ValueError):by_base([region(size=0)])
 def test_new_base(self):self.assertEqual(deltas({},by_base([region()]))[0]['commit_delta'],4096)
 def test_released(self):self.assertEqual(deltas(by_base([region()]),{})[0]['commit_delta'],-4096)
 def test_decommit(self):
  r=deltas(by_base([region()]),by_base([region(state=0x2000)]))[0];self.assertEqual((r['commit_delta'],r['reserve_delta']),(-4096,4096))
 def test_identity(self):
  with self.assertRaises(ValueError):identity_gate(dict(identity=dict(pid=1,creation_filetime=1)),dict(pid=1,creation_filetime=2))
if __name__=='__main__':unittest.main()
