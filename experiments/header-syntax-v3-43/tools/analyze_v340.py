#!/usr/bin/env python3
import argparse,csv,math,statistics
from pathlib import Path
ap=argparse.ArgumentParser()
ap.add_argument('--directory',type=Path,required=True)
ap.add_argument('--pairs',type=int,required=True)
ap.add_argument('--warmups',type=int,required=True)
ap.add_argument('--no-allocations',action='store_true')
a=ap.parse_args()
def load(name):
    with (a.directory/name).open(newline='',encoding='utf-8-sig') as f:return list(csv.DictReader(f))
def val(s):
    x=float(s)
    if not math.isfinite(x):raise ValueError('nonfinite numeric value')
    return x
try:
    pairs=load('v340-pairs.csv');summary=load('v340-summary.csv');phases=load('v340-phases.csv');types=load('v340-type-samples.csv');resolution=load('v340-resolution.csv')
    if (len(pairs),len(summary),len(phases),len(types),len(resolution))!=(a.pairs+a.warmups,1,1,1,1):raise ValueError('report counts differ')
    if any(r['header_graph_gate']!='HEADER_GRAPH_PARITY_PASS' or r['full_publish_gate']!='PARTIAL' for r in pairs):raise ValueError('pair parity failed')
    if any(r['header_graph_gate']!='HEADER_GRAPH_PARITY_PASS' for r in (summary[0],phases[0],types[0],resolution[0])):raise ValueError('profile parity failed')
    if summary[0]['full_publish_gate']!='PARTIAL':raise ValueError('invalid PUBLISH flag')
    r=resolution[0]
    if r['full_publish_gate']!='PARTIAL':raise ValueError('invalid resolution PUBLISH flag')
    if (int(r['named_cache_hits'])+int(r['named_cache_misses'])+
        int(r['named_rejected_by_grammar'])!=int(r['named_attempts'])):
        raise ValueError('named branch partition mismatch')
    if sum(int(r[k]) for k in ('fallback_qualified','fallback_contextual_int64','fallback_modifiers','fallback_other'))!=int(r['fallback_attempts']):
        raise ValueError('fallback category partition mismatch')
    if int(r['fallback_success'])+int(r['fallback_failed'])!=int(r['fallback_attempts']):raise ValueError('fallback outcome partition mismatch')
    if int(r['sampled_fields'])<int(r['named_attempts']):raise ValueError('sampled fields mismatch')
    if sum(int(r[k]) for k in ('shape_failed','layers_zero','layers_one','layers_two_plus'))!=int(r['shape_attempts']):
        raise ValueError('shape category partition mismatch')
    if int(r['shape_attempts'])>int(r['sampled_fields']):raise ValueError('shape count exceeds sampled fields')
    diffs=[]; ctrl=[]; cand=[];orders={}
    for i,r in enumerate(pairs):
        warm=i<a.warmups
        if r['phase']!=('warmup' if warm else 'measure'):raise ValueError('warmup/measure mismatch')
        expected=i+1 if warm else i-a.warmups+1
        if int(r['pair'])!=expected:raise ValueError('pair index mismatch')
        old=val(r['v338a_ms']);new=val(r['v340_ms']);saved=val(r['saved_ms'])
        if min(old,new)<=0 or abs(old-new-saved)>0.005:raise ValueError('saved mismatch')
        if not warm:
            diffs.append(saved);ctrl.append(old);cand.append(new)
            orders.setdefault(r['order'],[]).append(saved)
    if len(orders)!=2:raise ValueError('missing AB/BA counterbalance')
    wins=sum(x>0 for x in diffs);med=statistics.median(diffs)
    if abs(med-val(summary[0]['median_paired_saved_ms']))>0.005 or wins!=int(summary[0]['paired_wins']):raise ValueError('summary mismatches pairs')
    if abs(statistics.median(ctrl)-val(summary[0]['median_v338a_ms']))>0.005 or abs(statistics.median(cand)-val(summary[0]['median_v340_ms']))>0.005:raise ValueError('median mismatch')
    if int(summary[0]['pairs'])!=a.pairs or int(summary[0]['warmups'])!=a.warmups:raise ValueError('count mismatch')
    if not a.no_allocations:
        allocations=load('v340-allocations.csv')
        if len(allocations)!=2 or [r['variant'] for r in allocations]!=['v338a','v340'] or any(r['header_graph_gate']!='HEADER_GRAPH_PARITY_PASS' for r in allocations):raise ValueError('allocation parity mismatch')
    else:allocations=[]
    n=sum(x!=0 for x in diffs); k=min(wins,n-wins)
    sign_p=min(1.,2*sum(math.comb(n,i) for i in range(k+1))/(2**n)) if n else 1.
    cut=sorted(diffs)[1:-1] if len(diffs)>2 else diffs
    md=['# V3-40 — прямой отрицательный фильтр builtin','',
        'Контроль: V3-38A (V3-37 + events.reserve); кандидат: V3-40.',
        'Проверяется только пропуск заведомо ложной intrinsic-проверки для первого identifier.','',
        f'- Пар: {len(diffs)}; побед: {wins}; sign test p={sign_p:.6f}',
        f'- Median V3-38A: {statistics.median(ctrl):.6f} ms',
        f'- Median V3-40: {statistics.median(cand):.6f} ms',
        f'- Median paired saved: {med:.6f} ms',
        f'- Median без двух крайних разностей: {statistics.median(cut):.6f} ms',
        '- Внимание: paired A/B измеряет влияние диагностической инструментализации, НЕ ускорение алгоритма.',
        '- Порядок A/B: '+', '.join(f'{key}={statistics.median(vals):.6f} ms' for key,vals in sorted(orders.items())),
        '', 'Header parity: PASS, Full PUBLISH parity: PARTIAL.',
        'Фазы профилирования измерены отдельно; вложенные таймеры не суммировать.',
        'Время внутри sample и вложенные таймеры нельзя переносить на полный проект.','']
    path=a.directory/'v340-review.md'
    with path.open('x',encoding='utf-8') as f:f.write('\n'.join(md))
    print(f'V3-40 REVIEW PASS pairs={len(diffs)} wins={wins} median_paired_saved={med:.6f} sign_p={sign_p:.6f}')
except (ValueError,OSError,KeyError,TypeError) as ex:
    ap.exit(1,'V3-40 REVIEW FAIL: '+str(ex)+'\n')
