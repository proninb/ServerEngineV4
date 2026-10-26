"""Reuse all C1 fixture bodies against the slot-based producer, without editing C1."""
from pathlib import Path
import sys
repo, output = (Path(p).resolve() for p in sys.argv[1:])
source = (repo/'experiments/parallel-local-source-10-c1/feasibility.cpp').read_text()
source = source[:source.index('int main()')]
source = source.replace('#include "direct_commit.hpp"', '#include "slot_commit.hpp"')
source = source.replace('direct_commit sink(f.G)', 'pls10c2::slot_commit sink(f.G)')
source = source.replace('finish(pending,sink,', 'pls10c2::finish(pending,sink,')
output.write_text(source)
