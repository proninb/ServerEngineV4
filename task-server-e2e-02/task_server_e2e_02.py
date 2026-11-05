"""Real Server TCP + Task.exe callbacks, isolated staging directory; Windows stdlib."""
import argparse
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import tempfile
import time
import uuid

# Reuse the already validated TCP protocol implementation.
BASE = Path(__file__).resolve().parent.parent / 'task-server-e2e-01'
if not (BASE / 'task_server_e2e.py').is_file():
    raise SystemExit('Missing task-server-e2e-01/task_server_e2e.py beside this package')
sys.path.insert(0, str(BASE))
from task_server_e2e import Client, connect


def require(value, message):
    if not value:
        raise AssertionError(message)


def wait_for_state(client, expected, seconds=10):
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        result = client.request('GET_STATE')
        state = result.get('payload', {}).get('state', '').upper()
        if state == expected:
            return
        time.sleep(0.05)
    raise AssertionError(f'Expected state {expected}')


def read_events(path):
    if not path.exists():
        return []
    return [tuple(line.split()) for line in path.read_text(encoding='ascii').splitlines()]


def make_tasks(fixture_names, fail=False):
    def task(id, exe, deps=None, frozen=False):
        return {'id': id, 'executable': exe, 'dependencies': deps or [],
                'settings': {'calls': 1, 'cycles': 1, 'starting_from_cycle': 1,
                             'run_in_freeze': frozen}}
    if fail:
        return {'version': 1, 'tasks': [task('FAIL', fixture_names[2])]}
    return {'version': 1, 'tasks': [task('A', fixture_names[0]),
                                   task('B', fixture_names[1], ['A'])]}


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--server', type=Path, required=True)
    p.add_argument('--config', type=Path, required=True)
    p.add_argument('--project', type=Path, required=True)
    p.add_argument('--fixture-dir', type=Path, required=True)
    p.add_argument('--run-seconds', type=float, default=0.15)
    p.add_argument('--timeout', type=float, default=15)
    args = p.parse_args()
    for f in (args.server, args.config, args.project):
        require(f.is_file(), f'Missing: {f}')
    compiled = args.project.parent / '.serverengine' / args.project.name / 'compiled.bin'
    require(compiled.is_file(), f'Missing compiled.bin: {compiled}')
    fixture_paths = [args.fixture_dir / f'task_server_e2e_fixture_{i}.exe' for i in range(3)]
    for f in fixture_paths:
        require(f.is_file(), f'Missing fixture: {f}')
    with tempfile.TemporaryDirectory(prefix='server-task-e2e-02-') as directory:
        stage = Path(directory)
        project_dir = stage / 'project'
        project_dir.mkdir()
        project = project_dir / args.project.name
        data = json.loads(args.project.read_text(encoding='utf-8-sig'))
        require('tasks' not in data, 'Refusing to override existing Project tasks')
        data['tasks'] = 'Tasks.json'
        project.write_text(json.dumps(data, ensure_ascii=False), encoding='utf-8')
        dest_compiled = project_dir / '.serverengine' / project.name / 'compiled.bin'
        dest_compiled.parent.mkdir(parents=True)
        shutil.copy2(compiled, dest_compiled)
        runtime = compiled.parent / 'runtime.bin'
        if not runtime.is_file():
            raise RuntimeError(f'Missing runtime.bin: {runtime}')
        shutil.copy2(runtime, dest_compiled.parent / 'runtime.bin')
        # Preserve optional IC resource for a Project that references one.
        if data.get('ic'):
            ic = Path(data['ic'])
            require(not ic.is_absolute(), 'Absolute IC path needs a dedicated isolated fixture')
            src = args.project.parent / ic
            require(src.is_file(), f'Missing IC resource: {src}')
            dst = project_dir / ic
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)
        names = []
        for i, src in enumerate(fixture_paths):
            name = f'task_probe_{i}.exe'
            shutil.copy2(src, project_dir / name)
            names.append(name)
        config = json.loads(args.config.read_text(encoding='utf-8-sig'))
        endpoints = config['communication']['endpoints']
        require(len(endpoints) == 1 and endpoints[0]['transport'] == 'tcp',
                'Requires one TCP endpoint')
        require(config['authentication']['mode'] == 'none', 'Requires authentication none')
        require(config['settings']['shm']['mode'] == 'fixed_direct', 'Requires fixed_direct SHM')
        config['settings']['shm']['name'] = 'CW.TaskServerE2E02.' + uuid.uuid4().hex
        with socket.socket() as s:
            s.bind(('127.0.0.1', 0))
            port = s.getsockname()[1]
        endpoints[0]['address'] = '127.0.0.1'
        endpoints[0]['port'] = port
        # Stage Server credentials beside the temporary server.json.
        for name in ('server.license', 'server.lease'):
            source = args.config.resolve().parent / name
            if source.is_file():
                shutil.copy2(source, stage / name)
        server_config = stage / 'server.json'
        server_config.write_text(json.dumps(config), encoding='utf-8')
        event_log = stage / 'events.log'
        server_log = stage / 'server.log'
        env = os.environ.copy()
        env['TASK_SERVER_E2E_LOG'] = str(event_log)
        def run_scenario(fail):
            (project_dir / 'Tasks.json').write_text(json.dumps(make_tasks(names, fail)), encoding='utf-8')
            with server_log.open('ab') as output:
                process = subprocess.Popen([str(args.server.resolve()), str(server_config)],
                    cwd=stage, env=env, stdout=output, stderr=subprocess.STDOUT)
                try:
                    with connect('127.0.0.1', port, args.timeout, process) as sock:
                        client = Client(sock)
                        client.request('LOGIN', {'name': 'TaskServerE2E02'})
                        client.state('UNLOADED')
                        for iteration in range(2 if not fail else 1):
                            before = len(read_events(event_log))
                            client.request('LOAD', {'path': str(project)})
                            client.state('LOADED')
                            client.request('RUN')
                            if fail:
                                wait_for_state(client, 'FREEZE')
                                events = read_events(event_log)[before:]
                                require(any(x[:2] == ('2', 'INIT') for x in events), 'FAIL INIT missing')
                                require(any(x[:2] == ('2', 'MAIN') for x in events), 'FAIL MAIN missing')
                                client.request('UNLOAD')
                                client.state('UNLOADED')
                            else:
                                client.state('RUN')
                                time.sleep(args.run_seconds)
                                client.request('FREEZE')
                                client.state('FREEZE')
                                first = read_events(event_log)
                                time.sleep(args.run_seconds)
                                second = read_events(event_log)
                                require(sum(x[:2] == ('0', 'MAIN') for x in first) ==
                                        sum(x[:2] == ('0', 'MAIN') for x in second),
                                        'Normal Task ran in FREEZE')
                                client.request('RUN')
                                client.state('RUN')
                                time.sleep(args.run_seconds)
                                client.request('FREEZE')
                                client.state('FREEZE')
                                client.request('UNLOAD')
                                client.state('UNLOADED')
                                events = read_events(event_log)[before:]
                                empty_main = os.environ.get('TASK_EMPTY_MAIN') == '1'
                                for task_id in ('0', '1'):
                                    require(sum(x[:2] == (task_id, 'INIT') for x in events) == 1,
                                            f'Task {task_id} INIT count')
                                    require(sum(x[:2] == (task_id, 'DONE') for x in events) == 1,
                                            f'Task {task_id} DONE count')
                                    if not empty_main:
                                        require(sum(x[:2] == (task_id, 'MAIN') for x in events) > 0,
                                                f'Task {task_id} MAIN missing')
                                if not empty_main:
                                    # Each dependent MAIN must follow a preceding A MAIN.
                                    a = b = 0
                                    for item in events:
                                        if item[:2] == ('0', 'MAIN'): a += 1
                                        if item[:2] == ('1', 'MAIN'):
                                            b += 1
                                            require(b <= a, 'Dependency B overtook A')
                                    require(a == b, 'Dependency A/B call counts differ')
                            print('PASS server task cycle', iteration + 1, 'failure' if fail else 'normal')
                        client.request('SHUTDOWN')
                    process.wait(timeout=args.timeout)
                    require(process.returncode == 0, f'Server exited {process.returncode}')
                finally:
                    if process.poll() is None:
                        process.kill()
                        process.wait(timeout=5)
        try:
            run_scenario(False)
            run_scenario(True)
            print('TASK-SERVER-E2E-02 PASS: TCP / INIT / MAIN / DONE / dependency / FREEZE / FAIL')
        except Exception:
            print('Server log:\n', server_log.read_text(encoding='utf-8', errors='replace')[-8000:],
                  file=sys.stderr)
            raise

if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('TASK-SERVER-E2E-02 FAIL:', error, file=sys.stderr)
        sys.exit(1)
