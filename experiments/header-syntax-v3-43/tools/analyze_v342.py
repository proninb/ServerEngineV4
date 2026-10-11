#!/usr/bin/env python3
import argparse,csv,math,statistics
from pathlib import Path
p=argparse.ArgumentParser();p.add_argument('--directory',type=Path,required=True);p.add_argument('--pairs',type=int,required=True);p.add_argument('--warmups',type=int,required=True);p.add_argument('--no-allocations',action='store_true');a=p.parse_args()
def read(n):
 with (a.directory/n).open(newline='',encoding='utf-8-sig') as f:return list(csv.DictReader(f))
def valid_num(v):
 n=float(v)
 if not math.isfinite(n):raise ValueError('nonfinite value')
 return n
try:
 rows=read('v342-pairs.csv');s=read('v342-summary.csv');phases=read('v342-phases.csv');types=read('v342-type-samples.csv')
 if (len(rows),len(s),len(phases),len(types))!=(a.pairs+a.warmups,1,1,1):raise ValueError('report counts')
 if any(x['header_graph_gate']!='HEADER_GRAPH_PARITY_PASS' or x['full_publish_gate']!='PARTIAL' for x in rows):raise ValueError('parity')
 if any(x['header_graph_gate']!='HEADER_GRAPH_PARITY_PASS' for x in (s[0],phases[0],types[0])) or s[0]['full_publish_gate']!='PARTIAL':raise ValueError('summary parity')
 differences=[];ctrl=[];variant=[];orders={}
 for i,r in enumerate(rows):
  warm=i<a.warmups;pair=i+1 if warm else i-a.warmups+1
  if r['phase']!=('warmup' if warm else 'measure') or int(r['pair'])!=pair:raise ValueError('pair phase/number')
  x=valid_num(r['v338a_ms']);y=valid_num(r['v342_ms']);z=valid_num(r['saved_ms'])
  if min(x,y)<=0 or abs(x-y-z)>0.006:raise ValueError('pair delta')
  if not warm:
   differences.append(z);ctrl.append(x);variant.append(y);orders.setdefault(r['order'],[]).append(z)
 if len(orders)!=2:raise ValueError('missing orders')
 wins=sum(x>0 for x in differences);median=statistics.median(differences)
 if abs(median-valid_num(s[0]['median_paired_saved_ms']))>0.006 or wins!=int(s[0]['paired_wins']):raise ValueError('summary mismatch')
 if abs(statistics.median(ctrl)-valid_num(s[0]['median_v338a_ms']))>0.006 or abs(statistics.median(variant)-valid_num(s[0]['median_v342_ms']))>0.006:raise ValueError('median mismatch')
 if int(s[0]['pairs'])!=a.pairs or int(s[0]['warmups'])!=a.warmups:raise ValueError('run counts mismatch')
 if not a.no_allocations:
  al=read('v342-allocations.csv')
  if len(al)!=2 or [x['variant'] for x in al]!=['v338a','v342'] or any(x['header_graph_gate']!='HEADER_GRAPH_PARITY_PASS' for x in al):raise ValueError('allocations')
 else:al=[]
 nonzero=[x for x in differences if x!=0];n=len(nonzero);k=min(wins,n-wins)
 pval=min(1,2*sum(math.comb(n,i) for i in range(k+1))/2**n) if n else 1
 out=['# V3-42 — lazy type_spec A/B', '', 'Контроль: неизменённый V3-38A. Один алгоритмический вариант: не инициализировать неиспользуемые слои type_spec.', '',f'- Пар: {len(differences)}; побед V3-42: {wins}/{len(differences)}; sign test p={pval:.6f}',f'- Median V3-38A: {statistics.median(ctrl):.6f} ms',f'- Median V3-42: {statistics.median(variant):.6f} ms',f'- Median paired saved: {median:.6f} ms',f'- Median по порядкам: '+', '.join(f'{key}={statistics.median(values):.6f} ms' for key,values in sorted(orders.items())), '- Header Graph parity: PASS; Full PUBLISH parity: PARTIAL.','- Измерение относится к MSVC UnitProXL лишь после запуска на целевой машине.','']
 (a.directory/'v342-review.md').open('x',encoding='utf-8').write('\n'.join(out))
 print(f'V3-42 REVIEW PASS pairs={len(differences)} wins={wins} paired_saved={median:.6f} sign_p={pval:.6f}')
except (ValueError,OSError,KeyError,TypeError) as e:p.exit(1,'V3-42 REVIEW FAIL: '+str(e)+'\n')
