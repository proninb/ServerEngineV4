"""Resolve the existing eight traces with validated OS symbols, without new capture."""
import csv
import itertools
import json
import pathlib
import subprocess
import sys

repo = pathlib.Path(__file__).resolve().parents[2]
capture = repo / 'build/publish-critical-path-02/capture-20261023-144348'
root = repo / 'build/runtime-shm-faults-01'
output = root / 'analysis'
output.mkdir(exist_ok=True)
with (capture / 'measurements.csv').open() as stream:
    allowed = {(int(r['block']), int(r['pid'])) for r in csv.DictReader(stream) if r['capture'] == 'sampled'}
metadata = []
for block in sorted({b for b, _ in allowed}):
    target = output / f'b{block}-samples.jsonl'
    if not target.exists():
        pids = ','.join(str(pid) for b, pid in sorted(allowed) if b == block)
        subprocess.run(['dotnet', str(root / 'reader/KernelReader.dll'), str(capture / f'b{block}.etl'),
                        str(block), pids, str(repo / 'build/publish-critical-path-02/frozen'),
                        str(root / 'symbols/manifest.json'), str(target), str(capture / 'manifest.json')], check=True)
    meta = json.loads(pathlib.Path(str(target) + '.meta.json').read_text())
    previous = json.loads((capture / f'analysis/b{block}-samples.jsonl.meta.json').read_text())
    assert meta['samples'] == previous['samples'] and meta['missing_stack_samples'] == previous['missing_stack_samples']
    assert meta['events_lost'] == 0 and not meta['truncated'] and meta['sample_interval_ms'] == previous['sample_interval_ms']
    # Adding OS names must not change event selection, application attribution or stack shape.
    with target.open() as current, (capture / f'analysis/b{block}-samples.jsonl').open() as original:
        for new_line, old_line in itertools.zip_longest(current, original):
            assert new_line is not None and old_line is not None
            new, old = json.loads(new_line), json.loads(old_line)
            for key in ('block', 'pid', 'tid', 'time_ms', 'count', 'weight_ms'):
                assert new[key] == old[key], (block, key)
            assert len(new['frames']) == len(old['frames'])
            keep = lambda frames: [f for f in frames if not f.startswith(('ntoskrnl', 'ntdll'))]
            assert keep(new['frames']) == keep(old['frames']), (block, new, old)
    metadata.append(meta)
    print('verified block', block, 'samples', meta['samples'], 'kernel unresolved', meta['unresolved_kernel_samples'], flush=True)
(output / 'quality.json').write_text(json.dumps({'blocks': metadata, 'application_stack_parity': True,
    'source_capture': str(capture), 'os_symbols': json.loads((root / 'symbols/manifest.json').read_text())}, indent=2))
