"""ServerEngineV4 Task cadence benchmark (Windows, Python standard library).

Measures observed Task MAIN throughput, not internal barrier latency.
Requires existing TASK-SERVER-E2E-01/02 scripts and fixture executables.
"""
import argparse
import csv
import json
import os
from pathlib import Path
import shutil
import socket
import statistics
import subprocess
import sys
import tempfile
import time
import uuid

ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / 'task-server-e2e-01'))
from task_server_e2e import Client, connect


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--server', type=Path, required=True)
    ap.add_argument('--config', type=Path, required=True)
    ap.add_argument('--project', type=Path, required=True)
    ap.add_argument('--fixture-dir', type=Path, required=True)
    ap.add_argument('--duration', type=float, default=8)
    ap.add_argument('--warmup', type=float, default=2)
    ap.add_argument('--periods', type=int, nargs='+', default=[2])
    ap.add_argument('--counts', type=int, nargs='+', default=[1, 2, 4])
    ap.add_argument('--output', type=Path, default=Path('task-performance-01.csv'))
    args = ap.parse_args()
    if args.duration <= 0 or args.warmup < 0 or any(p < 1 for p in args.periods) or any(n < 1 for n in args.counts):
        ap.error('Invalid benchmark parameters')
    if args.periods != [2]:
        ap.error('Current ServerEngineV4 scheduler hardcodes 2 ms; only --periods 2 is valid. Do not label other periods as tested.')
    for path in (args.server, args.config, args.project):
        if not path.is_file():
            ap.error(f'Missing {path}')
    compiled = args.project.parent / '.serverengine' / args.project.name / 'compiled.bin'
    runtime = compiled.parent / 'runtime.bin'
    for path in (compiled, runtime):
        if not path.is_file():
            ap.error(f'Missing {path}')
    fixture = args.fixture_dir / 'task_server_e2e_fixture_0.exe'
    if not fixture.is_file():
        ap.error(f'Missing {fixture}')
    original = json.loads(args.project.read_text(encoding='utf-8-sig'))
    if 'tasks' in original:
        ap.error('Project already defines tasks; refusing to override')
    base_config = json.loads(args.config.read_text(encoding='utf-8-sig'))
    if base_config['authentication']['mode'] != 'none' or base_config['settings']['shm']['mode'] != 'fixed_direct':
        ap.error('Requires auth none and fixed_direct SHM')
    if len(base_config['communication']['endpoints']) != 1 or base_config['communication']['endpoints'][0]['transport'] != 'tcp':
        ap.error('Requires one TCP endpoint')
    rows = []
    with tempfile.TemporaryDirectory(prefix='task-performance-01-') as tmp:
        stage = Path(tmp)
        project_dir = stage / 'project'
        project_dir.mkdir()
        project = project_dir / args.project.name
        data = dict(original)
        data['tasks'] = 'Tasks.json'
        project.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
        target = project_dir / '.serverengine' / project.name
        target.mkdir(parents=True)
        shutil.copy2(compiled, target / 'compiled.bin')
        shutil.copy2(runtime, target / 'runtime.bin')
        if data.get('ic'):
            ic = Path(data['ic'])
            if ic.is_absolute():
                ap.error('Absolute IC path unsupported in isolated staging')
            destination = project_dir / ic
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(args.project.parent / ic, destination)
        shutil.copy2(fixture, project_dir / 'task_probe.exe')
        for name in ('server.license', 'server.lease'):
            src = args.config.resolve().parent / name
            if src.is_file():
                shutil.copy2(src, stage / name)
        event_path = stage / 'events.log'
        server_log = stage / 'server.log'
        env = dict(os.environ, TASK_SERVER_E2E_LOG=str(event_path))
        for period in args.periods:
            for count in args.counts:
                event_path.write_bytes(b'')
                tasks = [{'id': f'T{i}', 'executable': 'task_probe.exe', 'dependencies': [],
                          'settings': {'calls': 1, 'cycles': 1, 'starting_from_cycle': 1,
                                       'run_in_freeze': False}} for i in range(count)]
                (project_dir / 'Tasks.json').write_text(json.dumps({'version': 1, 'tasks': tasks}), encoding='utf-8')
                config = json.loads(json.dumps(base_config))
                config['settings']['shm']['name'] = 'CW.TaskPerf01.' + uuid.uuid4().hex
                # ServerEngineV4 currently hardcodes a 2 ms task tick.
                with socket.socket() as sock:
                    sock.bind(('127.0.0.1', 0))
                    port = sock.getsockname()[1]
                config['communication']['endpoints'][0]['address'] = '127.0.0.1'
                config['communication']['endpoints'][0]['port'] = port
                config_path = stage / 'server.json'
                config_path.write_text(json.dumps(config), encoding='utf-8')
                with server_log.open('ab') as log:
                    proc = subprocess.Popen([str(args.server.resolve()), str(config_path)], cwd=stage,
                                            env=env, stdout=log, stderr=subprocess.STDOUT)
                    try:
                        with connect('127.0.0.1', port, 20, proc) as sock:
                            client = Client(sock)
                            client.request('LOGIN', {'name': 'TaskPerformance01'})
                            client.request('LOAD', {'path': str(project)})
                            client.state('LOADED')
                            client.request('RUN')
                            client.state('RUN')
                            time.sleep(args.warmup)
                            # Snapshot by byte offset; fixture logs one MAIN per callback.
                            start_size = event_path.stat().st_size
                            start = time.perf_counter()
                            time.sleep(args.duration)
                            client.request('FREEZE')
                            client.state('FREEZE')
                            end = time.perf_counter()
                            with event_path.open('rb') as f:
                                f.seek(start_size)
                                chunk = f.read()
                            main_count = sum(1 for line in chunk.splitlines() if b' MAIN ' in line)
                            seconds = end - start
                            cycles = main_count / count
                            row = {'period_ms_requested': period, 'task_count': count,
                                   'elapsed_s': round(seconds, 6), 'main_calls': main_count,
                                   'cycles_observed': round(cycles, 3),
                                   'cycles_per_s': round(cycles / seconds, 2),
                                   'effective_cycle_ms': round(1000 * seconds / cycles, 3) if cycles else '',
                                   'requested_cycles_per_s': round(1000 / period, 2)}
                            rows.append(row)
                            print('RESULT', row, flush=True)
                            client.request('UNLOAD')
                            client.state('UNLOADED')
                            client.request('SHUTDOWN')
                        proc.wait(timeout=20)
                        if proc.returncode != 0:
                            raise RuntimeError(f'Server exit code {proc.returncode}')
                    except Exception:
                        print(server_log.read_text(encoding='utf-8', errors='replace')[-8000:], file=sys.stderr)
                        raise
                    finally:
                        if proc.poll() is None:
                            proc.kill()
                            proc.wait(timeout=5)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open('w', newline='', encoding='utf-8') as f:
        writer = csv.DictWriter(f, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    print('TASK-PERFORMANCE-01 measurement complete:', args.output)
    print('NOTE: effective_cycle_ms is an end-to-end throughput proxy, NOT barrier p95/p99.')

if __name__ == '__main__':
    try:
        main()
    except Exception as exc:
        print('TASK-PERFORMANCE-01 FAIL:', exc, file=sys.stderr)
        sys.exit(1)
