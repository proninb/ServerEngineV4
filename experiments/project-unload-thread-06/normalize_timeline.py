"""Strict common-order timeline normalization; never sort by UTC."""
import argparse,json,struct
from collections import Counter
from pathlib import Path
RECORD=struct.Struct('<BBHIQQQQq')
class Normalizer:
 def __init__(self,write):
  self.write=write;self.active={};self.seen=set();self.next_worker=0;self.live={};self.count=Counter();self.last_order=-1;self.last_etl=-1;self.next_id=0;self.boundaries=[]
 def emit(self,op,r,worker=0):self.write(RECORD.pack(op,r.get('owner',0),0,worker,r.get('id',0),r.get('size',0),r.get('heap',0),r.get('address',0),r['ticks']))
 def start(self,r,implicit=False):
  tid=r['tid']
  if tid in self.active:raise ValueError('duplicate active TID')
  self.next_worker+=1;self.active[tid]=self.next_worker;self.seen.add(tid);self.emit(5,r,self.next_worker);self.count['implicit_start' if implicit else 'start']+=1
 def feed(self,r):
  if r['order']<=self.last_order:raise ValueError('nonmonotonic common order')
  self.last_order=r['order'];etl=r.get('etl_ordinal')
  if etl is not None:
   if etl<=self.last_etl:raise ValueError('nonmonotonic ETL ordinal')
   self.last_etl=etl
  elif r['op']!='boundary':raise ValueError('native event lacks common ordinal')
  op=r['op'];tid=r['tid']
  if op=='boundary':
   if r['id']!=len(self.boundaries):raise ValueError('boundary index')
   self.boundaries.append({k:r[k] for k in ('id','stage','cycle','ticks')});self.emit(3,r);return
  if op=='start':self.start(r);return
  if tid not in self.active:
   if tid in self.seen:raise ValueError('event after stop without new start')
   self.start(r,True)
  worker=self.active[tid]
  if op=='stop':self.emit(6,r,worker);del self.active[tid];self.count['stop']+=1;return
  key=(r['heap'],r['address'])
  if op=='alloc':
   if key in self.live:raise ValueError('duplicate allocation')
   if r['id']!=self.next_id+1:raise ValueError('allocation id')
   self.next_id=r['id'];self.live[key]=(r['id'],r['size'],r['owner']);self.emit(1,r,worker);self.count['alloc']+=1
  elif op=='free':
   block=self.live.pop(key,None);self.count['free']+=1
   if block is None:
    if r['matched'] or r['id']!=0:raise ValueError('unmatched free identity')
    self.emit(4,r,worker);self.count['unmatched']+=1
   else:
    if not r['matched'] or block!=(r['id'],r['size'],r['owner']):raise ValueError('free mismatch')
    self.emit(2,r,worker)
  else:raise ValueError('operation')
def main():
 p=argparse.ArgumentParser();p.add_argument('root',type=Path);a=p.parse_args();q=json.loads((a.root/'quality.json').read_text())
 if q['events_lost'] or q['truncated'] or q['equal_boundary_events'] or q['identity']['pid']!=19288 or q['identity']['process_creation_filetime']!=134376721967437667:raise ValueError('quality gate')
 with (a.root/'strict-replay.bin').open('xb') as output:
  n=Normalizer(output.write)
  with (a.root/'timeline.jsonl').open(encoding='utf-8-sig') as stream:
   for line in stream:n.feed(json.loads(line))
 if (n.count['alloc'],n.count['free'],n.count['unmatched'],len(n.live),n.count['start'],n.count['stop'])!=(9536484,9536630,159,13,8776,8781):raise ValueError(dict(n.count))
 if n.active:raise ValueError('active threads at trace end')
 (a.root/'boundaries.json').write_text(json.dumps(n.boundaries))
 (a.root/'stages.bin').write_bytes(bytes(1 if b['stage']=='after_publish' else 2 if b['stage']=='after_unload_1' else 3 if b['stage']=='after_unload_2' else 4 for b in n.boundaries))
 result=dict(counts=dict(n.count),generations=n.next_worker,live_at_end=len(n.live),record_bytes=RECORD.size,common_order_verified=True,preexisting_thread_limit='Initial generations start lazily at first observed event; true pre-attach thread history unavailable.')
 (a.root/'normalization.json').write_text(json.dumps(result,indent=2));print(result)
if __name__=='__main__':main()
