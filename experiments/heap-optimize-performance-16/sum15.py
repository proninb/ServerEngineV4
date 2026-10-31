import json
from pathlib import Path
result=[]
for p in sorted(Path('build/heap-optimize-policy-15').glob('run-*/server/heap-optimize.jsonl')):
 rows=[json.loads(x) for x in p.read_text().splitlines()];calls=[x for x in rows if x['called']]
 result.append(dict(run=p.parent.parent.name,API_calls=len(calls),actual_API_interval_sum_ms=sum(x['ticks']/x['frequency']*1000 for x in calls),measured_actual_API_interval_sum_ms=sum(x['ticks']/x['frequency']*1000 for x in calls if x['unload']>20)))
with Path('build/heap-optimize-performance-16/heap15-API-sums.json').open('x') as f:json.dump(result,f,indent=2)
print(json.dumps(result,indent=2))
