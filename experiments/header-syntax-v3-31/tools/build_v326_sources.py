#!/usr/bin/env python3
"""Create a V3-26-only overlay; never edits the original repository."""
from pathlib import Path
import sys,argparse
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from build_v325_sources import create
p=argparse.ArgumentParser()
p.add_argument('--repo',type=Path,required=True)
p.add_argument('--overlay',type=Path,required=True)
a=p.parse_args()
target=a.overlay.resolve()
create(a.repo.resolve(),target)
(target/'v326_fast_type.marker').write_text('V326_FAST_TYPE_RESOLVE\n')
print('V3-26 isolated type-cache overlay:',target)
