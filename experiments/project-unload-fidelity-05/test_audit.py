import unittest
from audit_heap_events import audit,quality_gate,EXPECTED
def a(tid=1,owner=0):return dict(op='alloc',ticks=1,heap=1,address=10,size=64,owner=owner,tid=tid,source=2)
def f(tid=1):return dict(op='free',ticks=2,heap=1,address=10,tid=tid,source=3)
class Contract(unittest.TestCase):
 def test_same_thread(self):
  r=audit([a(),f()]);self.assertEqual(r['captured_live_blocks'],0);self.assertEqual(r['counts'].get('cross_thread_free',0),0)
 def test_cross_thread(self):self.assertEqual(audit([a(),f(2)])['counts']['cross_thread_free'],1)
 def test_duplicate(self):
  with self.assertRaises(ValueError):audit([a(),a()])
 def test_unmatched(self):self.assertEqual(audit([f()])['counts']['unmatched_free'],1)
 def test_order(self):
  with self.assertRaises(ValueError):audit([f(),a()])
 def test_unknown_preserved(self):self.assertEqual(audit([a()])['owner_alloc_counts'],{0:1})
 def test_identity(self):
  q={**EXPECTED,'normalized_event_fields_verified':True};quality_gate(q);q['pid']=1
  with self.assertRaises(ValueError):quality_gate(q)
if __name__=='__main__':unittest.main()
