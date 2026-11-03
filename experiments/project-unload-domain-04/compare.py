"""Feasibility summary; never promotes a candidate when shared retention is absent."""
import hashlib
import json
from pathlib import Path
import struct
import subprocess

root=Path('build/project-unload-domain-04')
source=Path('build/project-unload-memory-01/lifetime-owners-03/trace-01')
boundaries=json.loads((root/'boundaries.json').read_text())
normal=json.loads((root/'normalization.json').read_text())
original=json.loads((source/'status.json').read_text())
arms={}
for arm in ('shared','private'):
    rows=[json.loads(x) for x in (root/(arm+'.jsonl')).read_text().splitlines()]
    assert rows[-1]['stage']=='complete'
    assert rows[-1]['alloc']==normal['alloc']
    assert rows[-1]['free']==normal['matched_free']
    assert rows[-1]['unmatched_free_excluded']==normal['unmatched_free']
    checkpoints=[{**row,**boundaries[row['boundary']]} for row in rows if 'boundary' in row]
    assert len(checkpoints)==normal['boundaries_written']
    assert all(row['construction_live']==0 for row in checkpoints if row['stage']=='after_publish')
    unload=[x for x in checkpoints if x['stage']=='after_unload_1']
    arms[arm]=dict(initial=rows[0],complete=rows[-1],max_unload_drift_bytes=max(x['drift_bytes'] for x in unload),
        selected=[x for x in unload if x['cycle'] in (70,80,90)])

total_bytes=sum(normal['ownerBytes'])
classification=[dict(owner=name,allocations=count,event_allocsize_bytes=size,
    count_percent=count/normal['alloc']*100,bytes_percent=size/total_bytes*100)
    for name,count,size in zip(normal['owner_names'],normal['ownerCounts'],normal['ownerBytes'])]
baseline=original['snapshots'][0]['checkpoint']['private_bytes']
reference=[dict(label=s['label'],cycle=s['checkpoint']['cycle'],private_bytes=s['checkpoint']['private_bytes'],
    drift_bytes=s['checkpoint']['private_bytes']-baseline) for s in original['snapshots']]
reproduced=arms['shared']['max_unload_drift_bytes']>128*2**20
result=dict(status='INCONCLUSIVE / NO ADOPTION' if not reproduced else 'REQUIRES_FURTHER_REPLAY_VALIDATION',
    release='BLOCKED',production_changed=False,classification=classification,reference=reference,arms=arms,
    shared_reproduced_over_128MiB_drift=reproduced,
    limitations=['Captured order and AllocSize reproduced, not original concurrent heap scheduling.',
        '159 startup/unobserved Free records are preserved but excluded from replay.',
        'Startup allocations and original heap state are absent.',
        'Provider AllocSize is not verified as exact underlying CRT HeapAlloc request size.',
        'No original worker/thread creation and teardown; recorded TIDs are metadata only.',
        'No application allocation contents or original timing replayed.',
        'Construction subset uses conservative source-reviewed stack families and zero-live boundary checks; Unknown is not promoted.'])

# Meaningful negative contract: destroying a domain with live Construction must fail.
test=root/'contract-live-construction.bin'
pack=struct.Struct('<BBHIQQQQq')
test.write_bytes(pack.pack(1,1,0,1,1,64,1,100,0)+pack.pack(3,0,0,0,0,0,0,0,1))
(root/'contract-stages.bin').write_bytes(b'\x01')
check=subprocess.run([str(root.resolve()/'replay.exe'),str(test),str(root/'contract-stages.bin'),'1','private'],capture_output=True,text=True)
assert check.returncode==2 and 'live Construction at P_after' in check.stderr
result['live_construction_destroy_negative_test']='PASS'
result['format_record_bytes']=pack.size
(root/'comparison.json').write_text(json.dumps(result,indent=2))
print(result['status'])
for arm in arms:print(arm,'max UNLOAD drift MiB',arms[arm]['max_unload_drift_bytes']/2**20)
print('classification',classification)
