"""Stream audit of actual exported HeapAlloc/Free, not inferred replay records."""
import argparse
import bisect
from collections import Counter,defaultdict
import json
from pathlib import Path

EXPECTED=dict(pid=19288,process_creation_filetime=134376721967437667,
 executable_sha256='dac808b7dd4f5a72e8eec7ce5e6f8172461d5b3cf2612c38fa15f0e53b50af0d',
 pdb_guid='b99e77e4-59f3-434a-abf9-39dc6920444c',pdb_age=1,events_lost=0,truncated=False)
SOURCES={1:'lookaside',2:'LFH',3:'main_path',4:'slow_path',5:'invalid',6:'reserved_segment_value'}
OWNERS={0:'Unknown',1:'Construction_subset',2:'Resident_origin',3:'Server_origin'}
def quality_gate(q):
    for key,value in EXPECTED.items():
        if q.get(key)!=value:raise ValueError('quality mismatch: '+key)
    if q.get('normalized_event_fields_verified') is not True:raise ValueError('export verification absent')

def audit(rows,thread_starts=None):
    starts=thread_starts or {}
    def generation(tid,ticks):
        seq=starts.get(tid,[]);i=bisect.bisect_right(seq,ticks)-1
        return seq[i] if i>=0 else 0
    live={};count=Counter();sizes=Counter();buckets=Counter();paths=Counter();freepaths=Counter()
    owner_counts=Counter();owner_bytes=Counter();heap_counts=Counter();pairs=Counter();cross_owner=Counter();cross_path=Counter()
    live_owner=Counter();blocks_owner=Counter();phases=defaultdict(Counter);checkpoints=[]
    tids=set();peak=0;peak_blocks=0;current=0;cross_bytes=0;matched_bytes=0;phase='before_first_boundary';last=-1
    for row in rows:
        ticks=row['ticks']
        if ticks<last:raise ValueError('nonmonotonic export')
        last=ticks;op=row['op']
        if op=='boundary':
            checkpoints.append(dict(stage=row['stage'],cycle=row['cycle'],ticks=ticks,live_bytes=current,
                live_blocks=len(live),owner_live_bytes=dict(live_owner),owner_live_blocks=dict(blocks_owner)))
            phase={'after_publish':'runtime_to_unload1','after_unload_1':'load_to_unload2',
                   'after_unload_2':'publish','before_first_publish':'publish'}.get(row['stage'],'other')
            continue
        if op not in ('alloc','free'):raise ValueError('unsupported operation')
        key=(row['heap'],row['address']);tid=row['tid'];source=row['source'];tids.add(tid)
        if not isinstance(source,int):raise ValueError('missing SourceID')
        count[op]+=1;phases[phase][op]+=1
        if op=='alloc':
            if key in live:raise ValueError('duplicate live key')
            size=row['size'];owner=row['owner']
            if owner not in OWNERS or not isinstance(size,int) or size<0:raise ValueError('size/owner')
            live[key]=(size,tid,generation(tid,ticks),owner,source)
            current+=size;live_owner[owner]+=size;blocks_owner[owner]+=1
            sizes[size]+=1;buckets['0' if size==0 else '2^'+str((size-1).bit_length())]+=1
            paths[source]+=1;owner_counts[owner]+=1;owner_bytes[owner]+=size;heap_counts[row['heap']]+=1
            peak=max(peak,current);peak_blocks=max(peak_blocks,len(live))
        else:
            freepaths[source]+=1
            block=live.pop(key,None)
            if block is None:count['unmatched_free']+=1;continue
            size,alloc_tid,alloc_generation,owner,alloc_path=block
            count['matched_free']+=1;matched_bytes+=size;current-=size;live_owner[owner]-=size;blocks_owner[owner]-=1
            pairs[(alloc_path,source)]+=1
            if (tid,generation(tid,ticks))!=(alloc_tid,alloc_generation):
                count['cross_thread_free']+=1;cross_bytes+=size;cross_owner[owner]+=1;cross_path[alloc_path]+=1
    return dict(counts=dict(count),captured_live_blocks=len(live),captured_live_bytes=current,
        peak_captured_live_bytes=peak,peak_captured_live_blocks=peak_blocks,unique_raw_tids=len(tids),
        cross_thread_free_percent=100*count['cross_thread_free']/max(1,count['matched_free']),
        matched_free_bytes=matched_bytes,cross_thread_free_bytes=cross_bytes,
        cross_thread_free_byte_percent=100*cross_bytes/max(1,matched_bytes),
        allocation_sources=dict(paths),free_sources=dict(freepaths),
        alloc_to_free_source=[dict(alloc_source=a,free_source=b,count=n) for (a,b),n in pairs.items()],
        source_names=SOURCES,owner_names=OWNERS,owner_alloc_counts=dict(owner_counts),owner_alloc_bytes=dict(owner_bytes),
        cross_thread_by_owner=dict(cross_owner),cross_thread_by_alloc_source=dict(cross_path),
        size_distribution=dict(sizes),size_power_of_two_upper_bound_buckets=dict(buckets),heap_alloc_counts=dict(heap_counts),
        phase_counts={k:dict(v) for k,v in phases.items()},checkpoints=checkpoints,
        limitations=['Captured live excludes initial allocations before attach.',
            'AllocSize distribution cannot establish original CRT HeapAlloc requested sizes.',
            'No replay SourceID trace exists; original-vs-replay allocator path mismatch is not yet measured.',
            'SourceID values preserved independently for Alloc and Free; labels follow Microsoft documentation.',
            'Ownership labels are the conservative DOMAIN-04 subset, not exhaustive ownership.'])

def main():
    p=argparse.ArgumentParser(description=__doc__)
    for name in ('input','quality','output'):p.add_argument('--'+name,type=Path,required=True)
    for name,default in [('alloc',9536484),('free',9536630),('unmatched',159),('live',13)]:p.add_argument('--expected-'+name,type=int,default=default)
    args=p.parse_args();q=json.loads(args.quality.read_text());quality_gate(q)
    thread_rows=[json.loads(x) for x in args.input.with_name('threads.jsonl').open(encoding='utf-8-sig')]
    starts=defaultdict(list);events=[]
    for row in thread_rows:
        if row['op']=='thread_start':starts[row['tid']].append(row['ticks'])
        events.append((row['ticks'],1 if row['op']=='thread_start' else -1))
    for seq in starts.values():seq.sort()
    active=0;minimum=0;peak=0
    for _,delta in sorted(events):active+=delta;minimum=min(minimum,active);peak=max(peak,active)
    with args.input.open(encoding='utf-8-sig') as stream:result=audit((json.loads(x) for x in stream),starts)
    actual=(result['counts']['alloc'],result['counts']['free'],result['counts']['unmatched_free'],result['captured_live_blocks'])
    expected=(args.expected_alloc,args.expected_free,args.expected_unmatched,args.expected_live)
    if actual!=expected:raise ValueError(('count gate',actual,expected))
    if (q['alloc'],q['free'])!=actual[:2]:raise ValueError('quality/export counts')
    result.update(quality=q,thread_start_events=sum(len(v) for v in starts.values()),
        thread_stop_events=sum(x['op']=='thread_stop' for x in thread_rows),
        peak_thread_balance_above_start=peak,minimum_thread_balance=minimum,
        thread_peak_note='Balance uses only captured start/stop; initial thread population is unknown.',
        decision='READ_ONLY PROFILE COMPLETE / NO PRIVATE-DOMAIN ADOPTION')
    args.output.write_text(json.dumps(result,indent=2))
    print(json.dumps({k:result[k] for k in ('counts','peak_captured_live_bytes','cross_thread_free_percent','allocation_sources')}))
if __name__=='__main__':main()
