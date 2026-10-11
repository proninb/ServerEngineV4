#!/usr/bin/env python3
"""Create isolated native V3-27 overlay; never write into production repo."""
from pathlib import Path
import sys,argparse
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from build_v325_sources import create
p=argparse.ArgumentParser()
p.add_argument('--repo',type=Path,required=True)
p.add_argument('--overlay',type=Path,required=True)
a=p.parse_args()
create(a.repo.resolve(),a.overlay.resolve())
(a.overlay/'v327_derived_cache.marker').write_text('V327_DERIVED_CACHE\n')
print('V3-27 isolated native overlay:',a.overlay)
