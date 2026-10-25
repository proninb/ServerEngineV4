"""Full-operation CPU ranking from the validated real SampledProfile export."""
import argparse
import collections
import csv
import json
import pathlib
import statistics

RULES = (
    ('source_physical', ('source_preparation::lexical_lane_entry',)),
    ('header_physical', ('prepared_include_builder_v2::', 'prepare_header_include_closure_v2')),
    ('header_semantic', ('header_parser_v2::',)),
    ('source_semantic', ('source_parser_v2::', 'source_endpoint_resolver::', 'parse_source_roots_v2', 'source_predecode_batch::')),
    ('type_plan', ('prepare_shm_type_batch_inline64', 'shm_type_batch_builder::')),
    ('runtime_plan', ('prepare_shm_runtime_v2_physical_plan',)),
    ('abi_layout', ('prepare_shm_layout',)),
    ('persistence_other', ('persist_compiled', 'physical_plan_job::', 'encode_compiled_project_image', 'encode_runtime_project_image')),
    ('resident_other', ('create_resident_project', 'prepare_runtime_bindings')),
    ('runtime_materialization', ('shm_type_batch_executor::', 'materialize_shm_runtime_v2_', 'runtime_object_job::', 'runtime_link_mark_job::')),
)


def classify(frames):
    # Deepest recognizable stage wins; unmatched worker/kernel stacks stay other.
    for frame in reversed(frames):
        for category, patterns in RULES:
            if any(p in frame for p in patterns):
                return category
    if any('tokenize_impl' in f or 'lexer::tokenize' in f for f in frames):
        return 'frontend_unspecified'
    return 'other'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('capture', type=pathlib.Path)
    args = parser.parse_args()
    root = args.capture.resolve()
    manifest = json.loads((root / 'manifest.json').read_text())
    audit = json.loads((root / 'analysis/cpu-summary.json').read_text())
    with (root / 'measurements.csv').open() as stream:
        measurements = list(csv.DictReader(stream))
    allowed = {(int(r['block']), int(r['pid'])) for r in measurements if r['capture'] == 'sampled'}
    exclusive = collections.Counter()
    inclusive = collections.Counter()
    phases = collections.Counter()
    per_block = collections.defaultdict(collections.Counter)
    callers = collections.Counter()
    target = missing = 0
    for block in sorted({b for b, _ in allowed}):
        with (root / f'analysis/b{block}-samples.jsonl').open() as stream:
            for line in stream:
                row = json.loads(line)
                if (row['block'], row['pid']) not in allowed:
                    continue
                n, frames = row['count'], row['frames']
                target += n
                category = classify(frames) if frames else 'missing_stack'
                phases[category] += n
                per_block[block][category] += n
                if not frames:
                    missing += n
                    continue
                exclusive[frames[-1]] += n
                inclusive.update({frame: n for frame in set(frames)})
                if len(frames) > 1:
                    # Recursion/ICF names are preserved, never reinterpreted.
                    caller = next((f for f in reversed(frames[:-1]) if f != frames[-1]), '<recursive-only>')
                    callers[(frames[-1], caller)] += n
    assert target == audit['quality']['target_samples']
    assert missing == audit['quality']['missing_stack_samples']
    assert sum(phases.values()) == target
    metrics = [k for k in measurements[0] if k.endswith('_ms')]
    wall = {}
    for mode in ('control', 'sampled'):
        rows = [r for r in measurements if r['capture'] == mode and int(r['block']) >= 0]
        wall[mode] = {k: {'median_ms': statistics.median(float(r[k]) for r in rows),
                          'mean_ms': statistics.mean(float(r[k]) for r in rows)} for k in metrics}
    result = {
        'analysis_tool': 'PUBLISH-CRITICAL-PATH-02', 'capture': str(root),
        'build_description': manifest.get('build_snapshot', {}).get('description'),
        'captured_symbols': manifest['symbols'], 'target_samples': target,
        'quality': audit['quality'],
        'note': 'CPU counts are not wall milliseconds; inclusive function counts overlap. Phase attribution uses recognizable stacks only. IPO/ICF and missing stacks limit interpretation. No timing outliers excluded.',
        'phase_samples': dict(phases.most_common()), 'phase_samples_by_block': dict(per_block),
        'functions': [{'function': f, 'exclusive_samples': n, 'inclusive_samples': inclusive[f]}
                      for f, n in exclusive.most_common()],
        'top_leaf_callers': [{'leaf': f, 'caller': caller, 'samples': n}
                            for (f, caller), n in callers.most_common(100)],
        'wall': wall, 'observer_deltas': audit['observer_deltas'],
    }
    destination = root / 'analysis/full-publish-summary.json'
    destination.write_text(json.dumps(result, indent=2))
    print(json.dumps({'report': str(destination), 'quality': result['quality'],
                      'phase_samples': result['phase_samples'], 'top_functions': result['functions'][:12]}, indent=2))


if __name__ == '__main__':
    main()
