import unittest
from control_stability import validate,phase,summarize,EXPECTED
class Contract(unittest.TestCase):
 def rows(self):
  cp=dict(boundary=0,stage_code=2,active_workers=3,live=17,private_bytes=120,drift_bytes=20,worker_stack_commit=0)
  return [dict(stage='initial',private_bytes=100),cp,dict(stage='complete',**EXPECTED)]
 def test_valid(self):r=self.rows();self.assertEqual(len(validate(r,[r[1]])),1)
 def test_missing(self):
  r=self.rows()
  with self.assertRaises(ValueError):validate(r[:-1],[r[1]])
 def test_signature(self):
  r=self.rows();ref=dict(r[1]);r[1]['boundary']=1
  with self.assertRaises(ValueError):validate(r,[ref])
 def test_counter(self):
  r=self.rows();r[-1]['alloc']=0
  with self.assertRaises(ValueError):validate(r,[r[1]])
 def test_drift(self):
  r=self.rows();r[1]['drift_bytes']=1
  with self.assertRaises(ValueError):validate(r,[r[1]])
 def test_single_stacks(self):
  r=self.rows();r[1]['worker_stack_commit']=1
  with self.assertRaises(ValueError):validate(r,[r[1]])
 def test_phase_variation(self):
  data=[{70:60,80:10,90:60},{70:10,80:60,90:10}];runs=[dict(selected_drift_bytes=x,phase=phase(x)) for x in data]
  self.assertFalse(summarize(runs)['selected_phase_repeatable'])
if __name__=='__main__':unittest.main()
