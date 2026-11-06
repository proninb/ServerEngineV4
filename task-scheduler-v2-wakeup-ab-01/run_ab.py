"""WAKEUP-AB-02: A/B and B/A deadline measurement, no source changes."""
import argparse
import csv
from datetime import datetime
import json
import os
from pathlib import Path
import subprocess
import sys

ROOT = Path.cwd()


def percentile(values, q):
    if not values:
        raise ValueError('No samples')
    v = sorted(values)
    at = (len(v) - 1) * q
    lo = int(at)
    return round(v[lo] + (v[min(lo + 1, len(v) - 1)] - v[lo]) * (at - lo), 3)


def require_file(path, label):
    path = Path(path).resolve()
    if not path.is_file():
        raise FileNotFoundError(f'{label} not found: {path}')
    return str(path)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--server', required=True)
    parser.add_argument('--config', required=True)
    parser.add_argument('--project', required=True)
    parser.add_argument('--fixture-dir', required=True)
    parser.add_argument('--duration', type=float, default=8)
    parser.add_argument('--warmup', type=float, default=2)
    parser.add_argument('--orders', nargs='+', choices=('AB', 'BA'), default=['AB', 'BA'])
    parser.add_argument('--output-dir', default='build/task-scheduler-v2-wakeup-ab-02')
    args = parser.parse_args()
    if args.duration <= 0 or args.warmup < 0:
        parser.error('duration must be positive; warmup must be nonnegative')

    try:
        server = require_file(args.server, 'server')
        config = require_file(args.config, 'config')
        project = require_file(args.project, 'project')
        fixture = Path(args.fixture_dir).resolve()
        if not fixture.is_dir():
            raise FileNotFoundError(f'fixture-dir not found: {fixture}')
        runner = require_file(ROOT / 'task-runtime-v2-performance-01' / 'run.py', 'V2 performance runner')
        require_file(ROOT / 'task-performance-01' / 'task_performance_01.py', 'Task performance runner')
        require_file(ROOT / 'task-server-e2e-01' / 'task_server_e2e.py', 'Task E2E runner')
    except FileNotFoundError as exc:
        parser.error(str(exc) + '\nExtract the corresponding earlier test archive into the repository root.')

    base = Path(args.output_dir).resolve()
    base.mkdir(parents=True, exist_ok=True)
    out = base / datetime.now().strftime('%Y%m%d-%H%M%S-%f')
    out.mkdir()
    rows = []
    for pair_index, order in enumerate(args.orders, 1):
        for arm in order:
            mode = '0' if arm == 'A' else '1'
            arm_dir = out / f'{pair_index:02d}-{arm}'
            arm_dir.mkdir()
            deadline = arm_dir / 'deadline.csv'
            env = os.environ.copy()
            env.update(TASK_RUNTIME_V2='1', TASK_SCHEDULER_V2='1',
                       TASK_SCHEDULER_WAKEUP=mode, TASK_SCHEDULER_DEADLINE='1',
                       TASK_SCHEDULER_DEADLINE_CSV=str(deadline))
            cmd = [sys.executable, runner, '--server', server, '--config', config,
                   '--project', project, '--fixture-dir', str(fixture),
                   '--counts', '8', '--modes', 'v2', '--duration', str(args.duration),
                   '--warmup', str(args.warmup), '--output-dir', str(arm_dir)]
            print(f'RUN pair={pair_index} arm={arm} WAKEUP={mode}', flush=True)
            log = arm_dir / 'run.log'
            with log.open('w', encoding='utf-8') as handle:
                result = subprocess.run(cmd, cwd=ROOT, env=env, stdout=handle, stderr=subprocess.STDOUT)
            if result.returncode:
                print(log.read_text(encoding='utf-8', errors='replace')[-8000:], file=sys.stderr)
                raise RuntimeError(f'{arm}: benchmark exit={result.returncode}; log={log}')
            if not deadline.is_file():
                raise RuntimeError(f'{arm}: deadline CSV missing: {deadline}; check server instrumentation')
            with deadline.open(newline='', encoding='utf-8-sig') as handle:
                samples = list(csv.DictReader(handle))
            if not samples:
                raise RuntimeError(f'{arm}: empty deadline CSV: {deadline}')
            summary = arm_dir / 'summary.csv'
            with summary.open(newline='', encoding='utf-8-sig') as handle:
                perf = next(csv.DictReader(handle))
            def vals(key):
                return [float(item[key]) for item in samples]
            misses = sum(int(item['deadline_miss']) for item in samples)
            row = {
                'pair': pair_index, 'order': order, 'arm': arm, 'wakeup_enabled': mode,
                'cycles': len(samples), 'deadline_misses': misses,
                'deadline_miss_pct': round(100 * misses / len(samples), 3),
                'skipped_periods': sum(int(item['skipped_periods']) for item in samples),
                'wakeup_p50_us': percentile(vals('wakeup_lateness_us'), .5),
                'wakeup_p95_us': percentile(vals('wakeup_lateness_us'), .95),
                'wakeup_p99_us': percentile(vals('wakeup_lateness_us'), .99),
                'cycle_p99_us': percentile(vals('cycle_duration_us'), .99),
                'barrier_p99_us': percentile(vals('barrier_duration_us'), .99),
                'overrun_p99_us': percentile(vals('overrun_us'), .99),
                'effective_cycle_ms': float(perf['effective_cycle_ms']),
                'cycles_per_s': float(perf['cycles_per_s']),
            }
            rows.append(row)
            print('RESULT ' + json.dumps(row), flush=True)
            with (out / 'results.csv').open('w', newline='', encoding='utf-8') as handle:
                writer = csv.DictWriter(handle, fieldnames=list(row))
                writer.writeheader()
                writer.writerows(rows)
    print('RESULTS', out / 'results.csv')
    print('CPU not measured. V2 IPC path not independently proven by this runner.')
    print('No commit, push or adoption performed.')


if __name__ == '__main__':
    try:
        main()
    except (OSError, RuntimeError, ValueError, KeyError, StopIteration) as exc:
        print(f'ERROR: {exc}', file=sys.stderr)
        sys.exit(1)
