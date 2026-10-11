#!/usr/bin/env python3
"""Build V3-31 isolated Graph overlay from the user's unmodified repo."""
from pathlib import Path
import argparse
from build_v325_sources import create
ap=argparse.ArgumentParser()
ap.add_argument('--repo',type=Path,required=True)
ap.add_argument('--overlay',type=Path,required=True)
a=ap.parse_args()
repo=a.repo.resolve(); dest=a.overlay.resolve()
if dest.exists():raise RuntimeError('V331 overlay already exists; select a new OutputDirectory')
create(repo,dest)
(dest/'v331_sampled_type_profile.marker').write_text('V331_SAMPLED_TYPE_PROFILE\n',encoding='ascii')
print('V3-31 isolated overlay created:',dest)
