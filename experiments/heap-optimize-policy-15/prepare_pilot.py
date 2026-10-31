from pathlib import Path
source=Path('experiments/heap-optimize-resources-14/pilot.py').read_text(encoding='utf-8')
source=source.replace('heap-optimize-resources-14','heap-optimize-policy-15').replace('SE_HEAP_OPT14','SE_HEAP_OPT15')
source=source.replace("('OFF_','ON__')","('OFF_','EACH','POL_')")
source=source.replace("r['called']==(mode=='ON__')","r['called']==(mode=='EACH' or (mode=='POL_' and r['before_private']>=120635392))")
source=source.replace("if mode=='ON__':assert all(r['success'] and r['error']==0 for r in rows)","assert all(r['success'] and r['error']==0 for r in rows if r['called'])")
target=Path('experiments/heap-optimize-policy-15/pilot.py')
with target.open('x',encoding='utf-8') as stream:stream.write(source)
