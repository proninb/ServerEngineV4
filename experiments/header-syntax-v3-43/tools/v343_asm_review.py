#!/usr/bin/env python3
import argparse,hashlib,re,sys
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--asm',type=Path,required=True);p.add_argument('--out',type=Path,required=True);p.add_argument('--compiler',default='MSVC /O2 /GL- /FAcs');a=p.parse_args()
try:
 text=a.asm.read_text(encoding='utf-8',errors='replace')
 extracts={}
 for label in ('v343_probe_baseline','v343_probe_lazy'):
  m=re.search(r'^\s*_?'+label+r'\s+PROC\b.*?^\s*_?'+label+r'\s+ENDP\b',text,re.M|re.S|re.I)
  if not m:raise ValueError('missing assembly symbol '+label)
  lines=m.group().splitlines()
  instruction_lines=[line for line in lines if '\t' in line and not line.lstrip().startswith((';',';','?'))]
  extracts[label]={'all':lines,'instructions':instruction_lines}
 a.out.parent.mkdir(parents=True,exist_ok=True)
 with a.out.open('x',encoding='utf-8') as f:
  f.write('# V3-43 — MSVC code generation evidence\n\n')
  f.write('- Compiler flags: `'+a.compiler+'`\n')
  f.write('- Assembly SHA-256: `'+hashlib.sha256(a.asm.read_bytes()).hexdigest()+'`\n')
  f.write('- Scope: isolated escaped-object probe, **not** the full Header Graph producer.\n')
  f.write('- A store/zeroing instruction tally is *not* proof of application memory traffic. Inspect the listings.\n\n')
  for k,v in extracts.items():
   f.write('## '+k+'\n\n```asm\n')
   f.write('\n'.join(v['all'][:220])+'\n```\n\n')
  f.write('## Interpretation boundary\n\n')
  f.write('Compiler may optimize both paths differently because of escaping, register allocation, or namespace ordering. ')
  f.write('Only paired end-to-end UnitProXL plus parity controls a speed claim.\n')
 print('V343_ASM_REVIEW_PASS '+str(a.out))
except (ValueError,OSError) as e:sys.exit('V343_ASM_REVIEW_FAILED: '+str(e))
