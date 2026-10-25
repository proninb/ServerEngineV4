"""Analyze actual ordered Source events, separating roots and physical pages."""
import argparse
from collections import Counter, defaultdict
import csv
from dataclasses import dataclass, field
import json
import math
from pathlib import Path
import statistics


def distribution(values):
    values = sorted(values)
    if not values:
        return {'count': 0}
    return {'count': len(values), 'sum': sum(values), 'mean': statistics.mean(values),
            'median': statistics.median(values), 'p95': values[math.ceil(len(values) * .95) - 1],
            'p99': values[math.ceil(len(values) * .99) - 1], 'max': values[-1]}


def event_rows(path):
    with Path(path).open(encoding='utf-8') as stream:
        yield from csv.DictReader(stream)


@dataclass
class Unit:
    definitions: set = field(default_factory=set)
    externs: set = field(default_factory=set)
    targets: set = field(default_factory=set)
    reads: set = field(default_factory=set)
    types: set = field(default_factory=set)
    assignments: int = 0
    writes: int = 0
    links: int = 0
    external_links: int = 0
    def working_set(self):
        return self.definitions | self.externs | self.targets | self.reads


def analyze(directory, structural=False):
    directory = Path(directory)
    roots, pages = defaultdict(Unit), defaultdict(Unit)
    target_roots, target_pages = defaultdict(set), defaultdict(set)
    parameter_roots, parameter_pages = defaultdict(set), defaultdict(set)
    definitions = {}
    reads = []
    counts = Counter()
    categories = Counter()
    runs = []
    run_key, run_length = None, 0
    previous, seeded = None, None
    last_endpoint_object = None
    def finish_run():
        nonlocal run_key, run_length
        if run_length:
            runs.append(run_length)
        run_key, run_length = None, 0
    for ordinal, row in enumerate(event_rows(directory / 'events.csv')):
        kind = row['kind']
        e = {k: int(v) for k, v in row.items() if k != 'kind'}
        root, file, who = e['root'], e['file'], e['who']
        counts[kind] += 1
        key = (root, file, e['scope'], e['name'], who)
        if kind in ('B', 'E', 'D', 'N'):
            finish_run()
            previous = None
            seeded = key if kind == 'D' else None
        if kind == 'B':
            roots[root]
        elif kind == 'D':
            for unit in (roots[root], pages[file]):
                (unit.externs if e['category'] else unit.definitions).add(who)
                unit.types.add(e['object_type'])
            if not e['category']:
                if who in definitions:
                    raise ValueError('Repeated object definition in successful capture')
                definitions[who] = (root, file, ordinal)
        elif kind == 'A':
            assert e['category'] in ((1, 2, 3, 4) if structural else (1, 2, 3)), 'Unfinished assignment'
            categories[e['category']] += 1
            for object_id in (who, e['source']) if e['source'] else (who,):
                counts['endpoint_object_requests'] += 1
                counts['generic_last_object_type_hits'] += object_id == last_endpoint_object
                last_endpoint_object = object_id
            counts['eligible_previous_target'] += previous == key
            counts['eligible_declaration_seeded_target'] += seeded == key
            previous = seeded = key
            this_run = (root, file, e['scope'], who)
            if this_run != run_key:
                finish_run()
                run_key = this_run
            run_length += 1
            target_roots[who].add(root)
            target_pages[who].add(file)
            for unit in (roots[root], pages[file]):
                unit.assignments += 1
                unit.targets.add(who)
                unit.types.add(e['object_type'])
                if e['source']:
                    unit.reads.add(e['source'])
                    unit.types.add(e['source_type'])
                    unit.links += 1
            if e['source']:
                reads.append((root, file, e['source'], ordinal))
        elif kind == 'W':
            assert who and e['endpoint'] != 0xffffffff
            target = (who, e['endpoint'])
            parameter_roots[target].add(root)
            parameter_pages[target].add(file)
            roots[root].writes += 1
            pages[file].writes += 1
    finish_run()
    assert counts['B'] == counts['E'], 'Incomplete Source root'
    for root, file, who, ordinal in reads:
        owner = definitions.get(who)
        if owner is None:
            counts['read_owner_not_defined_in_source'] += 1
            continue
        counts['cross_root_reference_operands'] += owner[0] != root
        counts['cross_file_reference_operands'] += owner[1] != file
        counts['reference_before_definition'] += owner[2] > ordinal
        roots[root].external_links += owner[0] != root
        pages[file].external_links += owner[1] != file
    def unit_summary(units):
        active = [u for u in units.values() if u.definitions or u.externs or u.assignments]
        return {'units': len(units), 'active_units': len(active),
                'definitions': distribution([len(u.definitions) for u in active]),
                'working_set_objects': distribution([len(u.working_set()) for u in active]),
                'target_objects': distribution([len(u.targets) for u in active]),
                'root_type_refs': distribution([len(u.types) for u in active]),
                'assignments': distribution([u.assignments for u in active]),
                'units_with_cross_unit_reads': sum(u.external_links > 0 for u in active)}
    summary = {'events': dict(counts), 'assignments': dict(categories), 'source_roots': unit_summary(roots),
               'physical_files': unit_summary(pages), 'contiguous_target_runs': distribution(runs),
               'run_histogram': dict(sorted(Counter(runs).items())),
               'targets_written_in_multiple_roots': sum(len(x) > 1 for x in target_roots.values()),
               'targets_written_in_multiple_files': sum(len(x) > 1 for x in target_pages.values()),
               'parameters_written_in_multiple_roots': sum(len(x) > 1 for x in parameter_roots.values()),
               'parameters_written_in_multiple_files': sum(len(x) > 1 for x in parameter_pages.values()),
               'unique_written_parameters': len(parameter_roots)}
    summary['eligible_previous_target_pct'] = 100 * counts['eligible_previous_target'] / counts['A'] if counts['A'] else 0
    summary['eligible_declaration_seeded_target_pct'] = 100 * counts['eligible_declaration_seeded_target'] / counts['A'] if counts['A'] else 0
    for label, units in (('roots', roots), ('physical-files', pages)):
        with (directory / (label + '-summary.csv')).open('w', newline='', encoding='utf-8') as stream:
            writer = csv.writer(stream)
            writer.writerow(['id', 'definitions', 'externs', 'working_set', 'types', 'target_objects', 'assignments', 'writes', 'links', 'external_links'])
            for key, u in sorted(units.items()):
                writer.writerow([key, len(u.definitions), len(u.externs), len(u.working_set()), len(u.types), len(u.targets), u.assignments, u.writes, u.links, u.external_links])
    (directory / 'summary.json').write_text(json.dumps(summary, indent=2), encoding='utf-8')
    return summary


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    print(json.dumps(analyze(args.directory), indent=2))
