import unittest
from normalize_timeline import Normalizer,RECORD
def row(op,order=0,tid=1,**kw):return dict(op=op,order=order,etl_ordinal=order,ticks=order,tid=tid,id=0,size=0,heap=1,address=10,owner=0,**kw)
def allocation(order=0,tid=1):r=row('alloc',order,tid);r.update(id=1,size=64);return r
class Contract(unittest.TestCase):
 def make(self):return Normalizer(lambda _:None)
 def test_format(self):self.assertEqual(RECORD.size,48)
 def test_start_stop(self):n=self.make();n.feed(row('start'));n.feed(row('stop',1));self.assertFalse(n.active)
 def test_implicit(self):n=self.make();n.feed(allocation());self.assertEqual(n.count['implicit_start'],1)
 def test_reuse(self):n=self.make();n.feed(row('start'));n.feed(row('stop',1));n.feed(row('start',2));self.assertEqual(n.next_worker,2)
 def test_duplicate_start(self):
  n=self.make();n.feed(row('start'))
  with self.assertRaises(ValueError):n.feed(row('start',1))
 def test_after_stop(self):
  n=self.make();n.feed(row('start'));n.feed(row('stop',1))
  with self.assertRaises(ValueError):n.feed(allocation(2))
 def test_equal_time_order(self):n=self.make();a=row('start');b=row('stop',1);b['ticks']=0;n.feed(a);n.feed(b);self.assertFalse(n.active)
 def test_order_rejected(self):
  n=self.make();n.feed(row('start',1))
  with self.assertRaises(ValueError):n.feed(row('stop',0))
 def test_duplicate_alloc(self):
  n=self.make();n.feed(allocation());r=allocation(1);r['id']=2
  with self.assertRaises(ValueError):n.feed(r)
 def test_unmatched(self):n=self.make();r=row('free');r['matched']=False;n.feed(r);self.assertEqual(n.count['unmatched'],1)
 def test_cross_thread_free(self):n=self.make();n.feed(allocation());r=row('free',1,tid=2);r.update(id=1,size=64,matched=True);n.feed(r);self.assertFalse(n.live)
 def test_owner_mismatch(self):
  n=self.make();n.feed(allocation());r=row('free',1);r.update(id=1,size=64,matched=True,owner=1)
  with self.assertRaises(ValueError):n.feed(r)
if __name__=='__main__':unittest.main()
