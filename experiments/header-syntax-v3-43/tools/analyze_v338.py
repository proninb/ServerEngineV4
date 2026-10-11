#!/usr/bin/env python3
"""Independently validate V3-38 producer parity and evaluate paired timings."""
import argparse, csv, math, statistics
from pathlib import Path

p=argparse.ArgumentParser()
p.add_argument('--directory', type=Path,required=True)
p.add_argument('--mode',choices=['A','B','Both'],required=True)
p.add_argument('--pairs',type=int,required=True)
p.add_argument('--warmups',type=int,required=True)
p.add_argument('--no-allocations',action='store_true')
a=p.parse_args()

def rows(path):
    with path.open(newline='',encoding='utf-8-sig') as fh:
        return list(csv.DictReader(fh))
def number(x):
    f=float(x)
    if not math.isfinite(f):raise ValueError('Non-finite numeric value')
    return f

def analyze(mode):
    tag='v338'+mode.lower()
    data=rows(a.directory/f'{tag}-pairs.csv')
    summary=rows(a.directory/f'{tag}-summary.csv')
    phases=rows(a.directory/f'{tag}-phases.csv')
    types=rows(a.directory/f'{tag}-type-samples.csv')
    if (len(data),len(summary),len(phases),len(types))!=(a.pairs+a.warmups,1,1,1):
        raise ValueError(f'{tag}: row-count mismatch')
    gate=lambda row: row['header_graph_gate']=='HEADER_GRAPH_PARITY_PASS'
    if any(not gate(r) or r['full_publish_gate']!='PARTIAL' for r in data) or any(not gate(r) for r in (summary[0],phases[0],types[0])):
        raise ValueError(f'{tag}: parity gate mismatch')
    if summary[0]['full_publish_gate']!='PARTIAL':raise ValueError('Full PUBLISH status invalid')
    if int(summary[0]['pairs'])!=a.pairs or int(summary[0]['warmups'])!=a.warmups:raise ValueError('Summary pair mismatch')
    if number(summary[0]['sample_stride'])<1:raise ValueError('Missing profile stride')
    diffs=[]; base=[]; candidate=[]; byorder={}
    for i,r in enumerate(data):
        is_warm=i<a.warmups
        if r['phase']!=('warmup' if is_warm else 'measure'): raise ValueError(f'{tag} pair type mismatch')
        if int(r['pair'])!=(i+1 if is_warm else i+1-a.warmups):raise ValueError(f'{tag} pair id mismatch')
        v0=number(r['v337_ms']);v1=number(r[f'{tag}_ms']);saved=number(r['saved_ms'])
        if v0<=0 or v1<=0 or abs(v0-v1-saved)>0.005:raise ValueError(f'{tag}: saved inconsistent')
        if not is_warm:
            diffs.append(saved);base.append(v0);candidate.append(v1)
            byorder.setdefault(r['order'],[]).append(saved)
    if len(byorder)!=2:raise ValueError(f'{tag}: A/B counterbalanced order missing')
    wins=sum(d>0 for d in diffs)
    median=statistics.median(diffs)
    if int(summary[0]['paired_wins'])!=wins or abs(number(summary[0]['median_paired_saved_ms'])-median)>0.005:
        raise ValueError(f'{tag}: summary mismatch')
    if abs(number(summary[0]['median_v337_ms'])-statistics.median(base))>0.005 or abs(number(summary[0][f'median_{tag}_ms'])-statistics.median(candidate))>0.005:
        raise ValueError(f'{tag}: median mismatch')
    n=sum(d!=0 for d in diffs);k=min(wins,n-wins)
    sign_p=min(1.0,2*sum(math.comb(n,i) for i in range(k+1))/(2**n)) if n else 1.0
    without_two=sorted(diffs)[1:-1] if len(diffs)>2 else diffs
    result={'variant':mode,'pairs':a.pairs,'wins':wins,'median_baseline_ms':statistics.median(base),
        'median_candidate_ms':statistics.median(candidate),'median_paired_saved_ms':median,
        'median_saved_without_extremes_ms':statistics.median(without_two),'sign_p':sign_p,
        'order_medians':';'.join(f'{order}:{statistics.median(x):.5f}' for order,x in sorted(byorder.items())),
        'allocation_calls_baseline':'','allocation_calls_candidate':'','requested_bytes_baseline':'','requested_bytes_candidate':''}
    if not a.no_allocations:
        ar=rows(a.directory/f'{tag}-allocations.csv')
        if len(ar)!=2 or [x['variant'] for x in ar]!=['v337',tag] or any(not gate(x) for x in ar):
            raise ValueError(f'{tag}: allocation parity invalid')
        result.update(allocation_calls_baseline=ar[0]['new_calls'], allocation_calls_candidate=ar[1]['new_calls'],
            requested_bytes_baseline=ar[0]['requested_bytes'], requested_bytes_candidate=ar[1]['requested_bytes'])
    print(f'V3-38{mode} REPORT PASS pairs={a.pairs} wins={wins} median_saved={median:.6f} sign_p={sign_p:.5f} parity=PASS')
    return result

try:
    if a.mode!='Both':analyze(a.mode)
    else:
        reports=[analyze('A'),analyze('B')]
        out_csv=a.directory/'v338-review.csv'
        out_md=a.directory/'v338-review.md'
        if out_csv.exists() or out_md.exists():raise ValueError('Review files must be new')
        with out_csv.open('x',newline='',encoding='utf-8') as f:
            writer=csv.DictWriter(f,fieldnames=list(reports[0]))
            writer.writeheader();writer.writerows(reports)
        md=['# V3-38 — изолированные изменения V3-37R','',
            'Контроль: неизменённый V3-37; independent V2 Header Graph oracle.','',
            '| Вариант | Изменение | Победы | Median paired saved, ms | Sign p |',
            '|---|---|---:|---:|---:|']
        for r in reports:
            label='Только events.reserve()' if r['variant']=='A' else 'Только owned_declaration_count()'
            md.append(f"| V3-38{r['variant']} | {label} | {r['wins']}/{r['pairs']} | {r['median_paired_saved_ms']:.4f} | {r['sign_p']:.5f} |")
        md+=['','Положительное saved = ускорение кандидата. Расчёты по отдельным парным сравнениям.',
            'Фазы профилирования измерены отдельно от A/B; вложенные таймеры не суммировать.',
            'Full PUBLISH parity остаётся PARTIAL. Production не менялся.','']
        out_md.write_text('\n'.join(md),encoding='utf-8')
        print('V3-38 REVIEW PASS',out_md)
except (ValueError,KeyError,OSError,TypeError) as ex:
    p.exit(1,f'V3-38 REPORT FAIL: {ex}\n')
