#!/usr/bin/env python3
"""Check the isolated overlay includes every always-configured CMake source.

On non-MSVC hosts the fixture unconditionally configures the MSVC oracle target,
reproducing the missing-file failure originally seen on Windows.
"""
from pathlib import Path
from tempfile import TemporaryDirectory
from subprocess import run
import importlib.util
import shutil

base=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('builder',base/'tools/build_v324_sources.py')
mod=importlib.util.module_from_spec(spec)
spec.loader.exec_module(mod)

with TemporaryDirectory(prefix='v324-overlay-test-') as tmp:
    root=Path(tmp)
    repo=root/'repo'
    (repo/'server_engine/project/graph').mkdir(parents=True)
    (repo/'server_engine').mkdir(exist_ok=True)
    (repo/'tests').mkdir()
    (repo/'server_engine/main.cpp').write_text('int main(){return 0;}\n')
    (repo/'server_engine/project/graph/graph.hpp').write_text(
        '    [[nodiscard]] server_status add_object(\n')
    (repo/'server_engine/project/graph/graph.cpp').write_text(
        'server_status graph::add_object(\n')
    (repo/'CMakeLists.txt').write_text('''cmake_minimum_required(VERSION 3.25)
project(ServerEngineV4 LANGUAGES CXX)
add_executable(ServerEngineV4 server_engine/main.cpp server_engine/project/graph/graph.cpp)
# Emulate if(MSVC) on Linux: oracle is still configured with BUILD_TESTING OFF.
add_executable(ServerEngineV4MsvcClassAbiOracle EXCLUDE_FROM_ALL tests/msvc_class_abi_oracle.cpp)
''')
    overlay=root/'overlay'
    try:
        mod.create_overlay(repo,overlay)
    except RuntimeError as e:
        assert 'msvc_class_abi_oracle.cpp' in str(e), str(e)
    else:
        raise AssertionError('Missing configure-only source must be caught before overlay is created')
    assert not overlay.exists()
    (repo/'tests/msvc_class_abi_oracle.cpp').write_text('int main(){return 0;}\n')
    mod.create_overlay(repo,overlay)
    copied=overlay/'tests/msvc_class_abi_oracle.cpp'
    assert copied.is_file()
    assert copied.read_bytes()==(repo/'tests/msvc_class_abi_oracle.cpp').read_bytes()

    # Exact regression: without the copied source, CMake cannot configure.
    test_copied=overlay/'tests/msvc_class_abi_oracle.cpp'
    test_copied.unlink()
    bad=run(['cmake','-S',str(base),'-B',str(root/'bad-build'),
        '-DSERVERENGINE_ROOT='+str(overlay),'-DBUILD_TESTING=OFF'],capture_output=True,text=True)
    assert bad.returncode!=0, (bad.stdout,bad.stderr)
    assert 'msvc_class_abi_oracle.cpp' in bad.stderr+bad.stdout

    shutil.copy2(repo/'tests/msvc_class_abi_oracle.cpp',test_copied)
    good=run(['cmake','-S',str(base),'-B',str(root/'good-build'),
        '-DSERVERENGINE_ROOT='+str(overlay),'-DBUILD_TESTING=OFF'],capture_output=True,text=True)
    assert good.returncode==0, (good.stdout,good.stderr)
    assert (root/'good-build/CMakeCache.txt').is_file()
    assert (repo/'tests/msvc_class_abi_oracle.cpp').read_text()=='int main(){return 0;}\n'
    print('V3-24 FIX1: overlay MSVC-oracle configure regression PASS')
