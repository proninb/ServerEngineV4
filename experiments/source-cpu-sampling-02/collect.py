"""Collect on the same optimized production EXE; no hot-path instrumentation."""
import argparse
import csv
import ctypes
import hashlib
import json
import os
import pathlib
import statistics
import subprocess
import sys
import time
import uuid
from symbols import verify

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--exe',type=pathlib.Path,required=True)
    p.add_argument('--project',type=pathlib.Path,required=True)
    p.add_argument('--out',type=pathlib.Path,required=True)
    p.add_argument('--blocks',type=int,default=8);p.add_argument('--repeats',type=int,default=6)
    action=p.add_mutually_exclusive_group(required=True);action.add_argument('--check',action='store_true');action.add_argument('--capture',action='store_true');action.add_argument('--control',action='store_true')
    args=p.parse_args();exe=args.exe.resolve();project=args.project.resolve();out=args.out.resolve()
    assert 1<=args.blocks<=16 and 1<=args.repeats<=16
    repo=pathlib.Path(__file__).resolve().parents[2]
    assert project.is_relative_to(repo/'build'),'use only the disposable build fixture'
    out.mkdir(parents=True,exist_ok=True)
    if not args.check and ((out/'measurements.csv').exists() or any(out.glob('*.etl'))):
        raise FileExistsError('choose a new output directory; existing results will not be overwritten')
    identity=verify(exe,exe.with_suffix('.pdb'))
    admin=bool(ctypes.windll.shell32.IsUserAnAdmin())
    manifest={'symbols':identity,'exe':str(exe),'project':str(project),'administrator':admin,
              'blocks':args.blocks,'repeats':args.repeats,'status':'preflight',
              'project_sha256':hashlib.sha256(project.read_bytes()).hexdigest()}
    snapshot=exe.parent/'build-snapshot.json'
    if snapshot.exists():
        manifest['build_snapshot']=json.loads(snapshot.read_text())
    (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
    if args.check:print(json.dumps(manifest,indent=2));return 0
    if args.capture and not admin:
        manifest.update(status='blocked',reason='ETW CPU sampling requires an elevated Windows token with SeSystemProfilePrivilege')
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2));print(manifest['reason'],file=sys.stderr);return 3
    wpr=pathlib.Path(os.environ['SystemRoot'])/'System32/wpr.exe'
    golden={'compiled.bin':'a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d',
            'runtime.bin':'a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6'}
    rows=[];owned=False;instance='CWSourceCpu02'+uuid.uuid4().hex[:10]
    os.environ['_NT_SYMBOL_PATH']=str(exe.parent)
    manifest.update(instance=instance,profile='CPU.Verbose.File',status='running',golden_hashes=golden)
    def save():
        (out/'manifest.json').write_text(json.dumps(manifest,indent=2))
        if rows:
            with (out/'measurements.csv').open('w',newline='') as f:
                writer=csv.DictWriter(f,fieldnames=list(rows[0]));writer.writeheader();writer.writerows(rows)
    def run(block,mode,repeat):
        before=time.perf_counter_ns()
        proc=subprocess.Popen([str(exe),'publish-timeline',str(project),'24027'],stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:stdout,stderr=proc.communicate()
        except BaseException:
            proc.wait()
            raise
        after=time.perf_counter_ns()
        stem=out/f'b{block}-{mode}-r{repeat}'
        stem.with_suffix('.stdout.txt').write_text(stdout);stem.with_suffix('.stderr.txt').write_text(stderr)
        if proc.returncode:raise RuntimeError((block,mode,repeat,proc.returncode,stderr))
        row=dict(field.split('=',1) for field in next(line for line in stdout.splitlines() if line.startswith('mode=')).split(','))
        assert row['phase_only']=='1' and row['source_token_count']=='0' and row['header_data_members']=='0'
        for name,expected in golden.items():
            assert hashlib.sha256((project.parent/'.serverengine'/project.name/name).read_bytes()).hexdigest()==expected,name
        # Coarse timeline fields only. Nested intervals are never summed into total.
        fields=('total_ms','header_ms','source_ms','unaccounted_ms','lexical_ms',
                'header_v2_physical_ms','persistence_ms','resident_ms',
                'compiled_encode_ms','compiled_plan_ms','transient_release_ms')
        rows.append({'block':block,'capture':mode,'repeat':repeat,'pid':proc.pid,'qpc_start_ns':before,'qpc_end_ns':after,
                     **{key:row[key] for key in fields}})
        save();print(block,mode,repeat,proc.pid,row['total_ms'],flush=True)
    try:
        save();run(-1,'warmup',0);run(-1,'warmup',1)
        for block in range(args.blocks):
            assert verify(exe,exe.with_suffix('.pdb'))==identity,'EXE/PDB changed'
            modes=('control',) if args.control else (('control','sampled') if block%2==0 else ('sampled','control'))
            for mode in modes:
                if mode=='sampled':
                    started=subprocess.run([str(wpr),'-start','CPU','-filemode','-recordtempto',str(out),'-instancename',instance],capture_output=True,text=True)
                    (out/f'b{block}-wpr-start.txt').write_text(started.stdout+started.stderr)
                    if started.returncode:raise RuntimeError(('WPR start',started.returncode,started.stdout,started.stderr))
                    owned=True
                try:
                    for repeat in range(args.repeats):run(block,mode,repeat)
                finally:
                    if owned:
                        stopped=subprocess.run([str(wpr),'-stop',str(out/f'b{block}.etl'),'-instancename',instance],capture_output=True,text=True)
                        (out/f'b{block}-wpr-stop.txt').write_text(stopped.stdout+stopped.stderr)
                        if stopped.returncode:raise RuntimeError(('WPR stop failed; session still owned',instance,stopped.returncode))
                        owned=False
        assert verify(exe,exe.with_suffix('.pdb'))==identity,'EXE/PDB changed during the last block'
        if args.control:
            manifest.update(status='control_only',
                medians={key:statistics.median(float(r[key]) for r in rows if r['block']>=0) for key in rows[0] if key.endswith('_ms')},
                note='No ETW session was started; no CPU distribution or sampling overhead claim')
            save();return 0
        deltas={}
        for key in (key for key in rows[0] if key.endswith('_ms')):
            ds=[statistics.mean(float(r[key]) for r in rows if r['block']==b and r['capture']=='sampled')-
                statistics.mean(float(r[key]) for r in rows if r['block']==b and r['capture']=='control') for b in range(args.blocks)]
            deltas[key]={'paired_mean_ms':statistics.mean(ds),'paired_median_ms':statistics.median(ds),'block_deltas_ms':ds}
        manifest.update(status='captured_not_analyzed',observer_deltas=deltas,
                        note='ETL sample/stack counts, symbol resolution and event loss must be audited before ranking functions')
        save();return 0
    except BaseException as error:
        manifest.update(status='failed',error=str(error),owned_session=instance if owned else None);save();raise

if __name__=='__main__':sys.exit(main())
