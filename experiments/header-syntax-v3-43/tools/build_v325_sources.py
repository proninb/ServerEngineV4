#!/usr/bin/env python3
"""Extend FIX3 isolated benchmark overlay with V3-25 APIs. Never edits production."""
from pathlib import Path
import argparse,sys
HERE=Path(__file__).resolve().parent.parent
sys.path.insert(0,str(HERE/'tools'))
from build_v324_sources import create_overlay,once

def inject_existing_overlay(target:Path):
    h=target/'server_engine/project/graph/graph.hpp'
    c=target/'server_engine/project/graph/graph.cpp'
    header=h.read_text(encoding='utf-8')
    anchor='    [[nodiscard]] server_status add_object(\n'
    header=once(header,anchor,(HERE/'tools/v325_graph_api_extra.hpp.txt').read_text(encoding='utf-8')+anchor,'V325 graph API')
    header=once(header,'[[nodiscard]] member_record* direct_record_member(',
                      '[[nodiscard]] const member_record* direct_record_member(',
                      'const member observer')
    h.write_text(header,encoding='utf-8',newline='')
    cpp=c.read_text(encoding='utf-8')
    anchor='server_status graph::add_object(\n'
    cpp=once(cpp,anchor,(HERE/'tools/v325_graph_api_extra.cpp.txt').read_text(encoding='utf-8')+'\n'+anchor,'V325 graph definitions')
    cpp=once(cpp,'member_record* graph::direct_record_member(',
                 'const member_record* graph::direct_record_member(',
                 'const member observer impl')
    c.write_text(cpp,encoding='utf-8',newline='')
    (target/'v325_prevalidated_deferred.marker').write_text('V325_DEFERRED_INDEX\n')
    print('V3-25: isolated native Graph variant created:',target)

def create(repo:Path,target:Path):
    create_overlay(repo,target)
    inject_existing_overlay(target)

if __name__=='__main__':
    p=argparse.ArgumentParser()
    p.add_argument('--repo',type=Path,required=True)
    p.add_argument('--overlay',type=Path,required=True)
    a=p.parse_args();create(a.repo.resolve(),a.overlay.resolve())
