#!/usr/bin/env python3
"""Analyze frozen 31-pair V337 vs V337R timings and separate sampled Fields probe.

Numbers here are derived from CSV, not from benchmark console output.
"""
from __future__ import annotations
import argparse,csv,math,statistics, pathlib,os,sys,hashlib
from collections import Counter

p=argparse.ArgumentParser()
for key in ('pairs','phases','types','summary','fields'):
    p.add_argument('--'+key,required=True,type=pathlib.Path)
p.add_argument('--allocations',type=pathlib.Path)
p.add_argument('--output',required=True,type=pathlib.Path)
a=p.parse_args()

def load(path):
    with path.open('r',encoding='utf-8-sig',newline='') as f:return list(csv.DictReader(f))
def one(path):
    rows=load(path)
    if len(rows)!=1:raise ValueError(f'Expected exactly one row in {path}, got {len(rows)}')
    return rows[0]
def num(value,label):
    x=float(value)
    if not math.isfinite(x):raise ValueError('Nonfinite '+label)
    return x
def need(cond,msg):
    if not cond:raise ValueError(msg)

rows=load(a.pairs); s=one(a.summary); ph=one(a.phases); ty=one(a.types); f=one(a.fields)
n=int(s['pairs']); warm=int(s['warmups'])
need(n==31, 'This package requires exactly 31 measured pairs')
need(len(rows)==n+warm,f'Unexpected rows {len(rows)} != {n+warm}')
for obj,name in [(s,'summary'),(ph,'phases'),(ty,'types'),(f,'fields')]:
    need(obj['header_graph_gate']=='HEADER_GRAPH_PARITY_PASS',f'{name}: header parity failed')
need(s['full_publish_gate']=='PARTIAL' and f['full_publish_gate']=='PARTIAL', 'Full PUBLISH status unexpectedly changed')
deltas=[];old=[];new=[];pcts=[]; orders={}
for k,r in enumerate(rows):
    need(r['header_graph_gate']=='HEADER_GRAPH_PARITY_PASS' and r['full_publish_gate']=='PARTIAL',f'pair {k}: parity failed')
    need(r['phase']==('warmup' if k<warm else 'measure'),f'pair {k}: phase mismatch')
    o=num(r['v337_ms'],'v337_ms');v=num(r['v337r_ms'],'v337r_ms')
    d=num(r['saved_ms'],'saved_ms');pct=num(r['speedup_pct'],'speedup_pct')
    need(o>0 and v>0 and abs(o-v-d)<=0.002 and abs(100*d/o-pct)<=0.005,f'pair {k}: arithmetic mismatch')
    if k>=warm:
        old.append(o);new.append(v);deltas.append(d);pcts.append(pct)
        order=r['order'];need(order in ('V337-V337R','V337R-V337'),f'pair {k}: unexpected order')
        orders.setdefault(order,[]).append(d)
need(abs(statistics.median(old)-num(s['median_v337_ms'],'median_v337_ms'))<0.005,'V337 median mismatch')
need(abs(statistics.median(new)-num(s['median_v337r_ms'],'median_v337r_ms'))<0.005,'V337R median mismatch')
need(abs(statistics.median(deltas)-num(s['median_paired_saved_ms'],'median_paired_saved_ms'))<0.005,'Paired median mismatch')
wins=sum(x>0 for x in deltas)
need(wins==int(s['paired_wins']),'Win count mismatch')
need(int(f['stride'])==int(s['sample_stride'])==int(ty['stride']),'Strides are inconsistent')
need(int(f['sampled_records'])>0 and int(f['sampled_fields_committed'])>0,'No diagnostic sample')
need(int(f['sampled_declaration_visits'])==sum(int(f[c]) for c in ('sampled_field_candidates','sampled_callable_candidates','sampled_access_declarations','sampled_other_declarations')),'Fields declaration buckets do not sum')
# Exact, two-sided binomial sign test; ties excluded.
nonzero=[x for x in deltas if x!=0]
wp=sum(x>0 for x in nonzero)
p_sign=min(1.0,2.0*sum(math.comb(len(nonzero),i) for i in range(min(wp,len(nonzero)-wp)+1))/2**len(nonzero))
# No trimming of pairs for headline; robust diagnostic only.
med=statistics.median(deltas)
mad=statistics.median(abs(x-med) for x in deltas)
ranked=sorted(deltas)
no_extremes=ranked[1:-1] if len(ranked)>2 else ranked
order_summaries=[(order,len(d),sum(x>0 for x in d),statistics.median(d)) for order,d in sorted(orders.items())]
need(len(order_summaries)==2,'Expected alternating A/B order')
fields_columns=['descriptor_lower_bound_ms_sampled','produce_type_ms_sampled','member_write_ms_sampled',
    'initializer_decode_ms_sampled','construction_set_ms_sampled',
    'method_return_ms_sampled','reference_binding_ms_sampled']
fields_values=[(key,num(f[key],key)) for key in fields_columns]
need(all(x>=0 for key,x in fields_values),'Negative diagnostic timer')
alloc_rows=load(a.allocations) if a.allocations else []
if alloc_rows:
    need(len(alloc_rows)==2 and all(r['header_graph_gate']=='HEADER_GRAPH_PARITY_PASS' for r in alloc_rows),'Allocation gate mismatch')
    alloc={r['variant']:r for r in alloc_rows}
    need(set(alloc)=={'v337','v337r'},'Unexpected allocation variants')

metrics=[
    ('pairs',n,'pairs','paired benchmark'),('warmups',warm,'pairs','excluded'),
    ('median_v337_ms',statistics.median(old),'ms','paired benchmark'),
    ('median_v337r_ms',statistics.median(new),'ms','paired benchmark'),
    ('median_saved_ms',med,'ms','paired benchmark'),
    ('median_speedup_pct',statistics.median(pcts),'percent','paired benchmark'),
    ('wins_v337r',wins,'pairs','paired benchmark'),
    ('sign_test_p_two_sided',p_sign,'probability','paired benchmark'),
    ('median_absolute_deviation_ms',mad,'ms','paired benchmark'),
    ('median_without_one_min_and_max_ms',statistics.median(no_extremes),'ms','sensitivity only'),
    ('fields_ms_instrumented',num(ph['fields_ms'],'fields_ms'),'ms','full profiled run'),
    ('sampled_records',int(f['sampled_records']),'records','sampled diagnostic'),
    ('sampled_committed_fields',int(f['sampled_fields_committed']),'fields','sampled diagnostic'),
]
metrics.extend((k,v,'ms','sampled diagnostics') for k,v in fields_values)
if alloc_rows:
    metrics.extend((f'alloc_{key}_calls',int(alloc[key]['new_calls']),'calls','allocation profile') for key in ('v337','v337r'))
    metrics.extend((f'alloc_{key}_requested_bytes',int(alloc[key]['requested_bytes']),'bytes','allocation profile') for key in ('v337','v337r'))

out=a.output
out.mkdir(parents=True,exist_ok=True)
csv_path=out/'v337r31-review.csv'; md_path=out/'v337r31-review.md'
for path in (csv_path,md_path):
    need(not path.exists(),f'Existing output must not be overwritten: {path}')
# Create atomically with O_EXCL to prevent accidental overwrites.
with csv_path.open('x',encoding='utf-8',newline='') as io:
    writer=csv.writer(io);writer.writerow(['metric','value','unit','scope']);writer.writerows(metrics)

def fm(v):return f'{v:.4f}'
md=[
    '# ServerEngineV4 — V3-37R, независимый 31-парный A/B',
    '',
    'Контроль: **V3-37**, кандидат: **V3-37R**. Обе исходные реализации и основной benchmark не изменены.',
    '',
    '## Парные результаты (без warmups)',
    '',
    '| Показатель | Значение |','|---|---:|',
    f'| Пар | {n} |', f'| Побед V3-37R | {wins}/{n} |',
    f'| V3-37 median | {fm(statistics.median(old))} ms |',
    f'| V3-37R median | {fm(statistics.median(new))} ms |',
    f'| Median paired saved | {fm(med)} ms |',
    f'| Median per-pair speedup | {fm(statistics.median(pcts))}% |',
    f'| Two-sided exact sign test | p={p_sign:.5f} |',
    f'| MAD разностей | {fm(mad)} ms |',
    f'| Медиана без min/max разности | {fm(statistics.median(no_extremes))} ms |',
    '',
    '### Проверка порядка запуска',
    '', '| Порядок | Пар | Победы V3-37R | Median saved |','|---|---:|---:|---:|',
]
for order,nn,ww,mm in order_summaries:
    md.append(f'| {order} | {nn} | {ww} | {fm(mm)} ms |')
md.extend([
    '',
    '## Fields: отдельная диагностическая сборка',
    '',
    f"Полная профилированная фаза `Fields`: **{fm(num(ph['fields_ms'],'fields_ms'))} ms**.",
    f"Детальная выборка: **{f['sampled_records']}/{f['eligible_records']}** records, "
    f"**{f['sampled_fields_committed']}** успешно добавленных полей; stride={f['stride']}.",
    '',
    '| Участок | Время выборки |','|---|---:|',
])
for k,v in sorted(fields_values,key=lambda kv:kv[1],reverse=True):
    md.append(f'| `{k}` | {fm(v)} ms |')
md.extend([
    '',
    '**Интерпретация:** детальные таймеры относятся к выбранным записям; "Fields" — весь профилированный запуск. ',
    'Они могут быть вложены в таймеры `produce_type` и других функций, учитывают собственную стоимость вызовов `clock_type::now` и **не суммируются** в общее время поля. ',
    'Нельзя умножать sampled ms на stride и выдавать результат за время всего проекта.',
    '',
    '**Parities:** `HEADER_GRAPH_PARITY_PASS` для каждой пары, профиля и Fields-диагностики. `full_PUBLISH_gate=PARTIAL`.',
    '',
    '**Ограничение:** один 31-парный прогон не подтверждает перенос в production; не доказана полная parity Object/Assign/Source/Links/persistence.',
    ''
])
if alloc_rows:
    v0,v1=alloc['v337'],alloc['v337r']
    md.extend(['## Аллокации', '',
      f"`new`: V3-37 **{v0['new_calls']}**, V3-37R **{v1['new_calls']}**. ",
      f"`requested_bytes`: V3-37 **{v0['requested_bytes']}**, V3-37R **{v1['requested_bytes']}**.",
      'Измерение ограничено `current_thread_global_new_only`.', ''])
with md_path.open('x',encoding='utf-8') as io:io.write('\n'.join(md))
print(f'V3-37R31 REVIEW PASS pairs={n} wins={wins} median_paired_saved_ms={med:.6f} p_sign={p_sign:.5f}')
print(f'ReviewCsv={csv_path}')
print(f'ReviewMd={md_path}')
