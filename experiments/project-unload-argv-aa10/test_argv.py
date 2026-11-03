import os,unittest
from pathlib import Path
from run_argv import command,schedule,env_digest
from compare import summary
class Contract(unittest.TestCase):
 def setUp(self):self.cwd=Path.cwd().resolve();self.exe=self.cwd/'build/replay.exe';self.input=self.cwd/'build/input.bin';self.stages=self.cwd/'build/stages.bin'
 def cmd(self,arm):return command(arm,self.exe,self.input,self.stages,self.cwd)
 def test_runs(self):self.assertEqual(len(schedule()),16)
 def test_balance(self):self.assertEqual((schedule().count('A'),schedule().count('B')),(8,8))
 def test_abba(self):self.assertEqual(''.join(schedule()),'ABBA'*4)
 def test_absolute(self):self.assertTrue(Path(self.cmd('A')[1]).is_absolute())
 def test_relative(self):self.assertFalse(Path(self.cmd('B')[1]).is_absolute())
 def test_same_exe(self):self.assertEqual(self.cmd('A')[0],self.cmd('B')[0])
 def test_same_input(self):self.assertEqual((self.cwd/self.cmd('B')[1]).resolve(),Path(self.cmd('A')[1]))
 def test_same_stages(self):self.assertEqual((self.cwd/self.cmd('B')[2]).resolve(),Path(self.cmd('A')[2]))
 def test_mode(self):self.assertEqual(self.cmd('B')[3],'single')
 def test_invalid_arm(self):
  with self.assertRaises(ValueError):self.cmd('C')
 def test_env_order(self):self.assertEqual(env_digest({'B':'2','A':'1'}),env_digest({'A':'1','B':'2'}))
 def test_spread(self):self.assertEqual(summary([1,3,2]),dict(n=3,min=1,median=2,max=3,spread=2))
if __name__=='__main__':unittest.main()
