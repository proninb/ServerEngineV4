"""Strict parser for the observed CDB NT HEAP STATS summary (decimal KiB)."""
import argparse
import json
import re
import collections
from pathlib import Path

ROW = re.compile(r'^([0-9a-fA-F]{16})\s+([0-9a-fA-F]{8})\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)(?:\s+.*)?$')
def parse(text):
    section = text.split('\nHEAP_SUMMARY_BEGIN\n', 1)[1].split('\nHEAP_STATS_BEGIN\n', 1)[0]
    if 'NT HEAP STATS BELOW' not in section or 'SEGMENT HEAP' in section:
        raise ValueError('Unsupported heap summary format')
    heaps = {}
    for line in section.splitlines():
        match = ROW.fullmatch(line.strip())
        if match:
            handle, flags, reserve, commit, virt, free, *_ = match.groups()
            heaps[handle.lower()] = dict(flags=flags, reserve_kib=int(reserve),
                commit_kib=int(commit), free_kib=int(free), virtual_kib=int(virt))
        elif re.match(r'^[0-9a-fA-F]{16}\s', line.strip()):
            raise ValueError('Unparsed heap row: ' + line)
    if not heaps or 'CAPTURE_END' not in text:
        raise ValueError('Missing heap rows or completion marker')
    return heaps

def address_heaps(text):
    totals = collections.defaultdict(lambda: collections.Counter())
    lines = text.splitlines()
    for index, line in enumerate(lines[:-1]):
        fields = line.lstrip('+ ').split()
        if len(fields) < 5 or fields[3] != 'MEM_PRIVATE':
            continue
        match = re.search(r'Heap\s+\[ID: \d+; Handle: ([0-9a-fA-F]+);', line)
        if match:
            assert fields[4] in ('MEM_COMMIT', 'MEM_RESERVE')
            totals[match[1].lower()][fields[4]] += int(fields[2].replace('`',''),16)
    if not totals:
        raise ValueError('No supported !address heap regions')
    return dict(totals)

def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('capture', type=Path)
    args = parser.parse_args()
    status = json.loads((args.capture/'status.json').read_text())
    assert status['status'] == 'CAPTURE_COMPLETE'
    snapshots = []
    for record in status['snapshots']:
        identity = record['cdb']
        assert identity['pid'] == status['pid']
        assert identity['creation_filetime'] == status['process_creation_filetime']
        assert identity['binary_sha256'] == status['binary_sha256']
        text = Path(identity['log']).read_text(errors='replace')
        heaps = parse(text)
        snapshots.append(dict(label=record['label'], checkpoint=record['checkpoint'],
            before=identity['memory_before'], after=identity['memory_after'], heaps=heaps,
            actual_heap_regions=address_heaps(text)))
    baseline = snapshots[0]['heaps']
    for snap in snapshots:
        snap['baseline_delta_kib'] = {handle: {
            key: data[key] - baseline.get(handle, {}).get(key, 0)
            for key in ('commit_kib', 'free_kib')} for handle,data in snap['heaps'].items()}
    result = dict(pid=status['pid'], creation_filetime=status['process_creation_filetime'],
        executable_sha256=status['binary_sha256'], snapshots=snapshots,
        busy_allocation_statistics='Not inferred from committed/free summary; raw !heap -stat preserved')
    (args.capture/'heap-comparison.json').write_text(json.dumps(result, indent=2))
    for snap in snapshots:
        print(snap['label'], snap['checkpoint']['cycle'],
            round(snap['before']['private_bytes']/2**20,3),snap['heaps'],snap['actual_heap_regions'])

if __name__ == '__main__':
    main()
