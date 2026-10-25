"""Read real SampledProfile events through Microsoft's installed TraceEvent.

No timed workload is run here. Context-switch stacks are never mixed into samples.
"""
import argparse
import csv
import hashlib
import json
import pathlib
import re
import subprocess
from analyze import aggregate
from symbols import verify

def main():
    p=argparse.ArgumentParser();p.add_argument('capture',type=pathlib.Path);p.add_argument('reader',type=pathlib.Path);p.add_argument('symbols',type=pathlib.Path)
    args=p.parse_args();root=args.capture.resolve();out=root/'analysis';out.mkdir(exist_ok=True)
    expected=json.loads((root/'manifest.json').read_text())
    identity=verify(args.symbols/'ServerEngineV4PublishBenchmark.exe',args.symbols/'ServerEngineV4PublishBenchmark.pdb')
    assert identity==expected['symbols'],'frozen symbols differ from capture'
    with (root/'measurements.csv').open() as f:measurements=list(csv.DictReader(f))
    allowed={(int(r['block']),int(r['pid'])) for r in measurements if r['capture']=='sampled'}
    blocks=sorted({b for b,_ in allowed});metadata=[]
    xperf=pathlib.Path('C:/Program Files (x86)/Windows Kits/10/Windows Performance Toolkit/xperf.exe')
    for block in blocks:
        pids=','.join(str(pid) for b,pid in sorted(allowed) if b==block)
        dest=out/f'b{block}-samples.jsonl'
        run=subprocess.run(['dotnet',str(args.reader.resolve()),str(root/f'b{block}.etl'),str(block),pids,str(args.symbols.resolve()),str(dest),str(root/'manifest.json')],capture_output=True,text=True)
        (out/f'b{block}-reader.log').write_text(run.stdout+run.stderr)
        if run.returncode:raise RuntimeError((block,run.returncode,run.stderr))
        meta=json.loads(pathlib.Path(str(dest)+'.meta.json').read_text());metadata.append(meta)
        assert meta['sample_interval_ms']>0 and not meta['truncated']
        stats=out/f'b{block}-tracestats.txt'
        done=subprocess.run([str(xperf),'-i',str(root/f'b{block}.etl'),'-o',str(stats),'-a','tracestats'],capture_output=True,text=True)
        if done.returncode:raise RuntimeError(done.stderr)
        text=stats.read_text(errors='replace')
        losses=re.findall(r'Total # Lost (?:Buffers|Events)\s*:\s*(\d+)',text)
        assert len(losses)==2 and all(int(n)==0 for n in losses),text
        print('block',block,'samples',meta['samples'],'missing',meta['missing_stack_samples'],flush=True)
    def records():
        for block in blocks:
            with (out/f'b{block}-samples.jsonl').open() as f:
                for line in f:yield json.loads(line)
    result=aggregate(records(),allowed)
    result['blocks']=metadata;result['observer_deltas']=expected['observer_deltas']
    q=result['quality'];q['events_lost']=sum(m['events_lost'] for m in metadata)
    q['missing_stack_fraction']=q['missing_stack_samples']/q['target_samples']
    q['missing_worst_case_fraction_of_source']=q['missing_stack_samples']/q['source_samples']
    result['status']='samples_available_with_stack_coverage_limitations'
    result['note']='Actual CPU sample counts. Inclusive groups overlap; exclusive groups partition recognizable Source samples. IPO/ICF attribution and missing stacks prevent exact semantic decomposition.'
    for name in ('inclusive_samples','exclusive_samples'):
        result['groups']=dict(sorted(result['groups'].items(),key=lambda pair:-pair[1][name]))
    (out/'cpu-summary.json').write_text(json.dumps(result,indent=2))
    with (out/'functions.csv').open('w',newline='') as f:
        writer=csv.writer(f);writer.writerow(['function','inclusive_samples','exclusive_samples'])
        for name,row in sorted(result['functions'].items(),key=lambda p:-p[1]['exclusive_samples']):writer.writerow([name,row['inclusive_samples'],row['exclusive_samples']])
    with (out/'groups.csv').open('w',newline='') as f:
        writer=csv.writer(f);writer.writerow(['group','inclusive_samples','exclusive_samples','inclusive_ms','exclusive_ms'])
        for name,row in result['groups'].items():writer.writerow([name,row['inclusive_samples'],row['exclusive_samples'],row['inclusive_ms'],row['exclusive_ms']])
    print(json.dumps(q,indent=2))

if __name__=='__main__':main()
