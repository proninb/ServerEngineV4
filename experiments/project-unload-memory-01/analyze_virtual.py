"""Aggregate captured target-process ETW; no server or system mutations."""
import bisect
import collections
import datetime as dt
import json
from pathlib import Path

ROOT = Path('build/project-unload-memory-01')
def timestamp(value):
    return dt.datetime.fromisoformat(value.replace('Z', '+00:00')).timestamp()

rows = [json.loads(line) for line in (ROOT/'etw-control/checkpoints.jsonl').open()]
ends = [timestamp(row['timestamp_utc']) for row in rows]
groups = collections.defaultdict(collections.Counter)
examples = {}
for line in (ROOT/'virtual-events.jsonl').open():
    event = json.loads(line)
    index = bisect.bisect_left(ends, timestamp(event['timestamp_utc']))
    if index >= len(rows):
        continue
    flags = event['payload']['Flags']
    frames = event['frames']
    # Preserve full representative stacks; classify only observed named frames.
    heap = any('!Rtl' in frame and ('Heap' in frame or 'RtlpHp' in frame) for frame in frames)
    app = [frame for frame in frames if 'serverenginev4' in frame.lower()]
    key = (event['name'], flags, 'windows_heap' if heap else 'other', app[-1] if app else '')
    groups[index][key] += int(event['payload']['Length'])
    examples.setdefault(key, frames)

result = []
for index, row in enumerate(rows):
    if row['cycle'] not in (70, 79, 80, 99, 100, 109, 110):
        continue
    result.append({**row, 'interval_start_utc': rows[index-1]['timestamp_utc'] if index else None,
        'events_grouped_bytes': [dict(name=k[0], flags=k[1], category=k[2], app_frame=k[3],
            bytes=v, frames=examples[k]) for k,v in groups[index].most_common()]})
(ROOT/'virtual-analysis.json').write_text(json.dumps(result, indent=2))
for row in result:
    print(row['cycle'], row['stage'], round(row['private_bytes']/2**20,3))
    for item in row['events_grouped_bytes'][:8]:
        print(' ',item['name'],item['flags'],item['category'],round(item['bytes']/2**20,3),item['app_frame'])
