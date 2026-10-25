"""Count resolved Runtime kernel paths; CPU samples are not page-fault events."""
import collections
import json
import pathlib

repo = pathlib.Path(__file__).resolve().parents[2]
root = repo / 'build/runtime-shm-faults-01/analysis'
quality = json.loads((root / 'quality.json').read_text())
leaves = collections.Counter()
kernel_inclusive = collections.Counter()
paths = collections.Counter()
per_block = collections.defaultdict(collections.Counter)
spin_callers = collections.Counter()
total = apply = kernel_leaf = 0
for path in sorted(root.glob('b*-samples.jsonl')):
    with path.open() as stream:
        for line in stream:
            row = json.loads(line)
            frames, n = row['frames'], row['count']
            total += n
            if not frames or not any('shm_type_batch_executor::apply_api_one' in f for f in frames):
                continue
            apply += n
            leaves[frames[-1]] += n
            if frames[-1].startswith('ntoskrnl!'):
                kernel_leaf += n
            for f in set(frames):
                if f.startswith('ntoskrnl!'):
                    kernel_inclusive[f] += n
            for name in ('KiPageFault', 'MmAccessFault', 'MiResolveProtoPteFault',
                         'MiResolveDemandZeroFault', 'MiResolveTransitionFault',
                         'MiWaitForInPageComplete', 'MiAddWorkingSetEntries'):
                if any(f == 'ntoskrnl!' + name for f in frames):
                    paths[name] += n
                    per_block[row['block']][name] += n
            if 'SpinLock' in frames[-1] and frames[-1].startswith('ntoskrnl!'):
                caller = next((f for f in reversed(frames[:-1]) if f != frames[-1]), '<none>')
                spin_callers[caller] += n
assert total == sum(b['samples'] for b in quality['blocks']) == 284572
assert apply == 120711 and kernel_leaf == 93821
assert sum(b['missing_stack_samples'] for b in quality['blocks']) == 305
assert all(b['unresolved_kernel_samples'] == 0 for b in quality['blocks'])
fault_event_inventory = {str(b['block']): {k: v for k, v in b['events'].items() if 'fault' in k.lower()}
                         for b in quality['blocks']}
result = {
    'experiment': 'RUNTIME-SHM-FAULTS-01', 'source_capture': quality['source_capture'],
    'target_cpu_samples': total, 'apply_api_inclusive_samples': apply,
    'kernel_leaf_under_apply': kernel_leaf, 'application_exclusive_apply': leaves[
        'serverenginev4publishbenchmark!cw::server::shm_type_batch_executor::apply_api_one'],
    'paths_inclusive': dict(paths), 'paths_by_block': dict(per_block),
    'top_exclusive_leaves': dict(leaves.most_common(30)),
    'top_kernel_inclusive': dict(kernel_inclusive.most_common(30)),
    'spin_lock_nearest_callers': dict(spin_callers),
    'fault_event_inventory': fault_event_inventory,
    'symbols': quality['os_symbols'], 'application_stack_parity': quality['application_stack_parity'],
    'quality': {'events_lost': sum(b['events_lost'] for b in quality['blocks']),
                'missing_stack_samples': 305, 'unresolved_kernel_samples': 0},
    'note': 'Inclusive paths overlap. CPU samples do not count page faults or measure per-fault latency. Fault paths in sampled stacks demonstrate demand-zero/working-set CPU work; no hard-fault or disk-IO rate is inferred. No production code or new timed run.',
}
(root / 'summary.json').write_text(json.dumps(result, indent=2))
print(json.dumps({k: v for k, v in result.items() if k not in ('symbols', 'paths_by_block', 'fault_event_inventory')}, indent=2))
