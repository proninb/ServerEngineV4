"""Untraced, freshly built production benchmark; disjoint coarse accounting."""
import csv
import hashlib
import json
import pathlib
import statistics
import subprocess
import sys

exe, project, output = (pathlib.Path(p).resolve() for p in sys.argv[1:])
output.mkdir(parents=True,exist_ok=True)
artifact=project.parent/'.serverengine'/project.name
golden={'compiled.bin':'a49506c8df1a999d68624f3a3fd0c317516015b7375deb992ced99736b75522d',
        'runtime.bin':'a0281b3bb48103a7cd339cccd072d24f3524db9932bc67b58a714906996b5ee6'}
rows=[]
for block in range(9):
    modes=('publish','publish-timeline') if block%2==0 else ('publish-timeline','publish')
    for mode in modes:
        p=subprocess.run([str(exe),mode,str(project),'24027'],capture_output=True,text=True)
        (output/f'b{block}-{mode}.stdout.txt').write_text(p.stdout)
        (output/f'b{block}-{mode}.stderr.txt').write_text(p.stderr)
        if p.returncode:raise RuntimeError((block,mode,p.returncode,p.stderr))
        line=next(line for line in p.stdout.splitlines() if line.startswith('mode='))
        row=dict(field.split('=',1) for field in line.split(','))
        row.update(block=str(block),warmup=str(int(block==0)))
        for name,expected in golden.items():
            actual=hashlib.sha256((artifact/name).read_bytes()).hexdigest()
            assert actual==expected,(block,mode,name,actual)
        if mode=='publish-timeline':
            assert row['phase_only']=='1' and row['source_token_count']=='0' and row['header_data_members']=='0',row
            assert abs(float(row['unaccounted_ms']))<0.5,row
        rows.append(row)
        print(block,mode,row['total_ms'],flush=True)
with (output/'measurements.csv').open('w',newline='') as f:
    fields=list(dict.fromkeys(key for row in rows for key in row))
    writer=csv.DictWriter(f,fieldnames=fields);writer.writeheader();writer.writerows(rows)
measured=[r for r in rows if r['warmup']=='0']
timeline=[r for r in measured if r['mode']=='publish-timeline']
plain=[r for r in measured if r['mode']=='publish']
keys=['total_ms','operation_ms','accounted_ms','unaccounted_ms','external_ms','cleanup_ms',
      'context_setup_ms','configuration_ms','lexical_ms','assign_ms','semantic_ms',
      'header_v2_physical_ms','header_ms','source_ms','source_finalize_ms','semantic_other_ms',
      'post_semantic_ms','persistence_ms','post_persistence_ms','resident_ms','transient_release_ms']
summary={'measured_pairs':8,'excluded_warmup_pairs':1,'production_source':'fresh root CMake build, default options',
 'ordinary_total_median_ms':statistics.median(float(r['total_ms']) for r in plain),
 'timeline_medians_ms':{k:statistics.median(float(r[k]) for r in timeline) for k in keys},
 'timeline_means_ms':{k:statistics.mean(float(r[k]) for r in timeline) for k in keys},
 'timeline_minus_plain_paired_ms':[float(next(r for r in timeline if r['block']==str(b))['total_ms'])-float(next(r for r in plain if r['block']==str(b))['total_ms']) for b in range(1,9)],
 'max_abs_unaccounted_ms':max(abs(float(r['unaccounted_ms'])) for r in timeline),
 'golden_hashes':golden,'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
 'note':'No candidate speedup claim. Header physical/header/source/finalize/semantic_other are nested within semantic_ms; do not add both levels.'}
(output/'summary.json').write_text(json.dumps(summary,indent=2))
print(json.dumps(summary,indent=2))
