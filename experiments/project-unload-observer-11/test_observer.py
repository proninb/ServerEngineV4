import unittest
from collections import Counter
from run_observer import schedule,SELECTOR,MODES,check_marker,LABELS
from compare import stats
class Contract(unittest.TestCase):
 def test_count(self):self.assertEqual(len(schedule()),16)
 def test_balanced(self):self.assertEqual(Counter(schedule()),Counter({m:4 for m in MODES}))
 def test_positions(self):
  for pos in range(4):self.assertEqual(set(schedule()[pos::4]),set(MODES))
 def test_blocks(self):
  for block in range(4):self.assertEqual(set(schedule()[4*block:4*block+4]),set(MODES))
 def test_four_chars(self):self.assertTrue(all(len(x)==4 for x in SELECTOR.values()))
 def test_original_off_same(self):self.assertEqual(SELECTOR['original'],SELECTOR['off'])
 def test_marker(self):check_marker(dict(observer11='baseline',pid=1),'marker',0,1)
 def test_ack(self):check_marker(dict(observer11='b03',pid=1),'ack',4,1)
 def test_off_rejects(self):
  with self.assertRaises(ValueError):check_marker(dict(observer11='baseline',pid=1),'off',0,1)
 def test_order_rejects(self):
  with self.assertRaises(ValueError):check_marker(dict(observer11='b01',pid=1),'ack',0,1)
 def test_pid_rejects(self):
  with self.assertRaises(ValueError):check_marker(dict(observer11='baseline',pid=2),'ack',0,1)
 def test_stats(self):self.assertEqual(stats([1,3,2,4]),dict(n=4,min=1,median=2.5,max=4,spread=3))
if __name__=='__main__':unittest.main()
