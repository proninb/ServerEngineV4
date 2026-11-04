"""ServerEngineV4 real TCP lifecycle smoke test; stdlib-only, no production edits."""
import argparse
import json
import pathlib
import socket
import struct
import subprocess
import sys
import time

MAX_FRAME = 1024 * 1024


def exact(sock, count):
    result = bytearray()
    while len(result) < count:
        chunk = sock.recv(count - len(result))
        if not chunk:
            raise RuntimeError('TCP closed during frame')
        result.extend(chunk)
    return bytes(result)


class Client:
    def __init__(self, sock):
        self.sock = sock
        self.sequence = 0

    def receive(self):
        size = struct.unpack('!I', exact(self.sock, 4))[0]
        if not 0 < size <= MAX_FRAME:
            raise RuntimeError(f'Invalid frame size: {size}')
        return json.loads(exact(self.sock, size))

    def request(self, command, arguments=None):
        self.sequence += 1
        request = {'request_id': self.sequence, 'command': command}
        if arguments is not None:
            request['arguments'] = arguments
        data = json.dumps(request, ensure_ascii=False).encode('utf-8')
        self.sock.sendall(struct.pack('!I', len(data)) + data)
        deadline = time.monotonic() + self.sock.gettimeout()
        while time.monotonic() < deadline:
            reply = self.receive()
            if reply.get('request_id') != self.sequence:
                if reply.get('action') == 'SERVER_STATE':
                    print('EVENT', reply.get('old'), '->', reply.get('new'))
                continue
            if reply.get('status') is not True:
                raise RuntimeError(f'{command} failed: {reply}')
            print('PASS', command, reply.get('payload', ''))
            return reply
        raise TimeoutError(command)

    def state(self, expected):
        reply = self.request('GET_STATE')
        actual = reply.get('payload', {}).get('state', '').upper()
        if actual != expected.upper():
            raise AssertionError(f'Expected {expected}, got {actual}')


def connect(host, port, timeout, process):
    until = time.monotonic() + timeout
    while time.monotonic() < until:
        if process.poll() is not None:
            raise RuntimeError(f'Server exited prematurely ({process.returncode})')
        try:
            sock = socket.create_connection((host, port), timeout=1)
            sock.settimeout(timeout)
            return sock
        except OSError:
            time.sleep(0.1)
    raise TimeoutError('Server TCP startup')


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--server', type=pathlib.Path, required=True)
    parser.add_argument('--config', type=pathlib.Path, required=True)
    parser.add_argument('--project', type=pathlib.Path, required=True)
    parser.add_argument('--port', type=int, required=True)
    parser.add_argument('--host', default='127.0.0.1')
    parser.add_argument('--login', default='TaskServerE2E')
    parser.add_argument('--timeout', type=float, default=15)
    parser.add_argument('--run-seconds', type=float, default=0.15)
    args = parser.parse_args()
    for file in (args.server, args.config, args.project):
        if not file.is_file():
            parser.error(f'Missing: {file}')
    compiled = args.project.parent / '.serverengine' / args.project.name / 'compiled.bin'
    if not compiled.is_file():
        parser.error(f'Missing compiled.bin: {compiled}')
    log_path = pathlib.Path('task-server-e2e-01.log').resolve()
    with log_path.open('wb') as log:
        process = subprocess.Popen([str(args.server.resolve()), str(args.config.resolve())],
                                   cwd=str(args.config.resolve().parent), stdout=log, stderr=subprocess.STDOUT)
        try:
            with connect(args.host, args.port, args.timeout, process) as sock:
                client = Client(sock)
                client.request('LOGIN', {'name': args.login})
                client.state('UNLOADED')
                for iteration in range(2):
                    print('ITERATION', iteration + 1)
                    client.request('LOAD', {'path': str(args.project.resolve())})
                    client.state('LOADED')
                    client.request('RUN')
                    client.state('RUN')
                    time.sleep(args.run_seconds)
                    client.request('FREEZE')
                    client.state('FREEZE')
                    time.sleep(args.run_seconds)
                    client.request('RUN')
                    client.state('RUN')
                    time.sleep(args.run_seconds)
                    client.request('FREEZE')
                    client.state('FREEZE')
                    client.request('UNLOAD')
                    client.state('UNLOADED')
                client.request('SHUTDOWN')
            process.wait(timeout=args.timeout)
            if process.returncode != 0:
                raise RuntimeError(f'Server exited {process.returncode}; see {log_path}')
            print('SERVER TCP LIFECYCLE PASS; log:', log_path)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)


if __name__ == '__main__':
    try:
        main()
    except Exception as error:
        print('SERVER TCP LIFECYCLE FAIL:', error, file=sys.stderr)
        sys.exit(1)
