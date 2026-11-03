import io,unittest
from unittest.mock import patch
from pathlib import Path
from first_divergence import analyze,read_csv,validate_pair,COUNTERS
def row(i=0,value=100):return dict(boundary=i,cycle=i,stage='stage',private_bytes=value,drift_bytes=value-100,active_workers=3,handles=47,live=17)
class Contract(unittest.TestCase):
 def test_no_divergence(self):self.assertIsNone(analyze([[row()],[row()]],1)['first_private_spread_at_threshold'])
 def test_first_boundary(self):self.assertEqual(analyze([[row()],[row(value=102)]],2)['first_private_spread_at_threshold']['boundary'],0)
 def test_later(self):self.assertEqual(analyze([[row(),row(1)],[row(),row(1,103)]],2)['first_private_spread_at_threshold']['boundary'],1)
 def test_inclusive_threshold(self):self.assertIsNotNone(analyze([[row()],[row(value=101)]],1)['first_private_spread_at_threshold'])
 def test_negative_delta(self):self.assertEqual(analyze([[row(),row(1,50)],[row(),row(1,90)]],1)['ranked_transitions'][0]['delta_spread_bytes'],40)
 def test_threshold(self):
  with self.assertRaises(ValueError):analyze([[row()],[row()]],0)
 def test_schema(self):
  with patch.object(Path,'open',return_value=io.StringIO('unexpected\n1\n')):
   with self.assertRaises(ValueError):read_csv(Path('synthetic-schema.csv'))
 def test_missing_boundary(self):
  with self.assertRaises(ValueError):validate_pair([],[],1)
 def test_counters(self):
  r=row();raw=[dict(stage='initial',private_bytes=100),r,dict(stage='complete',**COUNTERS)];raw[-1]['free']=0
  with self.assertRaises(ValueError):validate_pair([r],raw,1)
 def test_csv_raw_mismatch(self):
  r=row();other=dict(r,live=18);raw=[dict(stage='initial',private_bytes=100),other,dict(stage='complete',**COUNTERS)]
  with self.assertRaises(ValueError):validate_pair([r],raw,1)
if __name__=='__main__':unittest.main()
