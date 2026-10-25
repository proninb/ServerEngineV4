"""Summarize real Header stacks, caller attribution and wall-time drift."""
import collections
import csv
import json
import pathlib
import statistics
import sys

root = pathlib.Path(sys.argv[1]).resolve()
with (root / 'measurements.csv').open() as stream:
    measurements = list(csv.DictReader(stream))
allowed = {(int(r['block']), int(r['pid'])) for r in measurements if r['capture'] == 'sampled'}
header = collections.Counter()
inclusive = collections.Counter()
blocks = collections.Counter()
contexts = collections.Counter()
source_blocks = collections.Counter()
for path in sorted((root / 'analysis').glob('b*-samples.jsonl')):
    with path.open() as stream:
        for line in stream:
            row = json.loads(line)
            if (row['block'], row['pid']) not in allowed or not row['frames']:
                continue
            frames, count = row['frames'], row['count']
            is_source = any(any(p in f for p in ('parse_source_roots_v2', 'source_parser_v2::', 'source_endpoint_resolver::', 'source_predecode_batch::')) for f in frames)
            is_header = any('header_parser_v2::' in f or 'parse_header_roots_v2' in f for f in frames)
            if is_source:
                source_blocks[row['block']] += count
            if is_header:
                blocks[row['block']] += count
                header[frames[-1]] += count
                inclusive.update({f: count for f in set(frames)})
            if (is_source or is_header) and ('hash_derived' in frames[-1] or 'vector<cw::server::derived_type_record' in frames[-1]):
                # Preserve immediate caller; an ICF-shared leaf name is not a semantic claim.
                contexts[('Header' if is_header else 'Source', frames[-1], frames[-2] if len(frames) > 1 else '<none>')] += count
wall = {}
for mode in ('control', 'sampled'):
    rows = [r for r in measurements if r['capture'] == mode]
    wall[mode] = {key: {'mean_ms': statistics.mean(float(r[key]) for r in rows), 'median_ms': statistics.median(float(r[key]) for r in rows)} for key in ('total_ms', 'header_ms', 'source_ms')}
wall['blocks'] = {str(b): {mode: statistics.mean(float(r['total_ms']) for r in measurements if int(r['block']) == b and r['capture'] == mode) for mode in ('control', 'sampled')} for b in sorted({b for b, _ in allowed})}
report = {
    'note': 'Recognizable phase stacks only. Inclusive counts overlap. CPU samples are not wall milliseconds. Missing stacks and IPO/ICF limit attribution; no samples or timing outliers excluded.',
    'header_samples': sum(header.values()), 'header_blocks': dict(blocks), 'source_blocks': dict(source_blocks),
    'header_functions': [{'name': f, 'exclusive': n, 'inclusive': inclusive[f]} for f, n in header.most_common()],
    'aliased_leaf_callers': [{'phase': phase, 'leaf': leaf, 'caller': caller, 'samples': n} for (phase, leaf, caller), n in contexts.most_common()],
    'wall': wall,
}
destination = root / 'analysis/phase-report.json'
destination.write_text(json.dumps(report, indent=2))
print(json.dumps({**{k: v for k, v in report.items() if k != 'aliased_leaf_callers'}, 'header_functions': report['header_functions'][:12]}, indent=2))
