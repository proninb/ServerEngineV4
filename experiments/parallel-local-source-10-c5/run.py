from pathlib import Path
source=(Path(__file__).resolve().parents[1]/'parallel-local-source-10-c3/run.py').read_text()
source=source.replace('PublishOrderedSource10C3','PublishStreamingSource10C5').replace('build/parallel-local-source-10-c3/Release','build/parallel-local-source-10-c5/Release')
exec(compile(source,str(Path(__file__)), 'exec'))
