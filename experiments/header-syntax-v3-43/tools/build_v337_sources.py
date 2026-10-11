#!/usr/bin/env python3
"""Make a private native-Graph overlay at a pinned local repo commit."""
from pathlib import Path
import argparse
from build_v325_sources import create
parser=argparse.ArgumentParser()
parser.add_argument('--repo',required=True,type=Path)
parser.add_argument('--overlay',required=True,type=Path)
args=parser.parse_args()
repo=args.repo.resolve(); overlay=args.overlay.resolve()
create(repo,overlay)
(overlay/'v337_direct_range.marker').write_text('V337_DIRECT_RANGE_ONLY\n',encoding='ascii')
print('V3-37 isolated overlay ready:',overlay)
