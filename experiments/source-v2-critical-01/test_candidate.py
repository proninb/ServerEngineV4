"""Reuse the string/reference/diagnostic fixtures; no new production test hooks."""
from pathlib import Path
import sys
repo=Path(__file__).resolve().parents[2]
root=Path(sys.argv[1]).resolve()
script=(repo/'experiments/source-string-batch-ab-01/test.py').read_text()
script=script[:script.index("for mode,fault in (")]+'''(out/'functional-results.json').write_text(json.dumps(results,indent=2))
print('ALL',len(results),'FUNCTIONAL OPERATIONS PASSED',flush=True)
'''
script=script.replace("for mode in ('scalar','batch'):","for mode in ('baseline','candidate'):")
script=script.replace("if mode=='scalar':","if mode=='baseline':")
script=script.replace("out/'functional-artifacts'/case/mode","cases/case/'.serverengine/project.json'")
script=script.replace('SOURCE_STRING_BATCH_01_MODE=mode,SOURCE_STRING_BATCH_01_DIAGNOSTICS=\'1\'',
                      'SOURCE_CRITICAL_LINK_MODE=mode,SOURCE_CRITICAL_LINK_DIAGNOSTICS=\'1\'')
script=script.replace("CW_RUNTIME_SHM_AUDIT='1'","SOURCE_CRITICAL_SHM_AUDIT='1'")
script=script.replace("out/'build/Release/ServerEngineV4PublishBenchmark.exe'","out.parent/'candidate-build/Release/SourceCriticalCandidate.exe'")
sys.argv=[sys.argv[0],str(root/'candidate-functional')]
sys.argv[1]=str(root/'candidate-functional')
(root/'candidate-functional').mkdir(exist_ok=True)
exec(compile(script,str(repo/'experiments/source-string-batch-ab-01/test.py'),'exec'),globals())
