"""Read-only analysis of TASK-PERFORMANCE-01 stage CSVs.

Reports measured cycle interval, task barrier and tick work distributions.
Residual interval minus tick duration is a proxy, NOT measured wake-up latency.
"""
import argparse
import csv
import math
import itertools
from pathlib import Path
from statistics import mean


def percentile(values, pct):
    if not values:
        return None
    s = sorted(values)
    pos = (len(s) - 1) * pct / 100
    lo = int(math.floor(pos))
    hi = int(math.ceil(pos))
    return s[lo] + (s[hi] - s[lo]) * (pos - lo)


def load_samples(path):
    with path.open(newline='', encoding='utf-8-sig') as handle:
        reader = csv.DictReader(handle)
        columns = reader.fieldnames or []
        rows = list(reader)
    return columns, rows


def parse_stages(path):
    # PERF02 emits headerless rows: stage,cycle,duration_ms.
    # Also accept a named-header format for compatibility.
    stages = {}
    with path.open(newline='', encoding='utf-8-sig') as handle:
        reader = csv.reader(handle)
        first = next(reader, None)
        if first is None:
            raise ValueError(f'{path}: empty CSV')
        normalized = [c.strip().lower() for c in first]
        stage_keys = ('stage', 'metric', 'name')
        duration_keys = ('duration_ms', 'elapsed_ms', 'ms', 'value_ms')
        if any(k in normalized for k in stage_keys) and any(k in normalized for k in duration_keys):
            stage_index = next(normalized.index(k) for k in stage_keys if k in normalized)
            duration_index = next(normalized.index(k) for k in duration_keys if k in normalized)
            data = reader
        elif len(first) == 3:
            stage_index, duration_index = 0, 2
            data = itertools.chain([first], reader)
        else:
            raise ValueError(f'{path}: unsupported CSV layout: {first!r}')
        valid = 0
        for row in data:
            if len(row) <= max(stage_index, duration_index):
                continue
            stage = row[stage_index].strip()
            try:
                value = float(row[duration_index]) / 1000.0
            except (ValueError, TypeError):
                continue
            if stage and math.isfinite(value) and value >= 0:
                stages.setdefault(stage, []).append(value)
                valid += 1
        if not valid:
            raise ValueError(f'{path}: no valid stage samples')
    return stages


def summary(stages, label, count, period):
    result = {'mode': label, 'tasks': count, 'period_requested_ms': period}
    for stage in ('cycle_interval', 'server_tick', 'manager_tick', 'barrier_wait', 'ipc_roundtrip'):
        values = stages.get(stage, [])
        result[f'{stage}_n'] = len(values)
        for pct in (50, 95, 99):
            v = percentile(values, pct)
            result[f'{stage}_p{pct}_ms'] = '' if v is None else round(v, 4)
        if stage == 'cycle_interval':
            result['interval_over_period_pct'] = round(100 * sum(v > period for v in values) / len(values), 2) if values else ''
            result['interval_over_2period_pct'] = round(100 * sum(v > 2 * period for v in values) / len(values), 2) if values else ''
            result['interval_mean_ms'] = round(mean(values), 4) if values else ''
    a = stages.get('cycle_interval', [])
    b = stages.get('server_tick', [])
    # Percentile difference is NOT a percentile of per-cycle residual.
    result['p50_interval_minus_tick_proxy_ms'] = round(percentile(a, 50) - percentile(b, 50), 4) if a and b else ''
    return result


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--off', type=Path, required=True, help='Scheduler OFF benchmark directory')
    p.add_argument('--on', type=Path, required=True, help='Scheduler ON benchmark directory')
    p.add_argument('--out', type=Path, required=True)
    p.add_argument('--period-ms', type=float, default=2.0)
    p.add_argument('--counts', type=int, nargs='+', default=[1, 2, 4, 8])
    a = p.parse_args()
    a.out.mkdir(parents=True, exist_ok=True)
    results = []
    for mode, directory in (('off', a.off), ('on', a.on)):
        for count in a.counts:
            path = directory / f'v2-{count}-stages.csv'
            if not path.is_file():
                raise SystemExit(f'Missing stage file: {path}')
            stages = parse_stages(path)
            if not stages.get('cycle_interval'):
                raise SystemExit(f'No cycle_interval samples: {path}')
            results.append(summary(stages, mode, count, a.period_ms))
    destination = a.out / 'jitter-summary.csv'
    with destination.open('w', newline='', encoding='utf-8') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(results[0]))
        writer.writeheader()
        writer.writerows(results)
    for r in results:
        print(f"{r['mode']:>3} {r['tasks']:>2} Tasks | cycle p50/p95/p99 "
              f"{r['cycle_interval_p50_ms']}/{r['cycle_interval_p95_ms']}/{r['cycle_interval_p99_ms']} ms "
              f"| barrier p99 {r['barrier_wait_p99_ms']} ms "
              f"| interval > 4ms {r['interval_over_2period_pct']}%")
    print(f'OUTPUT {destination.resolve()}')
    print('LIMIT: no deadline timestamps; wake-up lateness and true missed-deadline count are not measured.')


if __name__ == '__main__':
    main()
