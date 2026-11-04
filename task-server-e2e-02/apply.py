"""Install only new test fixture targets; never modify production sources."""
from pathlib import Path
import sys
root = Path(sys.argv[1] if len(sys.argv) > 1 else '.').resolve()
src = Path(__file__).resolve().parent / 'task_server_fixture.cpp'
cmake = root / 'CMakeLists.txt'
target = root / 'tests' / 'task_server_e2e_fixture.cpp'
if not cmake.is_file() or not (root / 'simtask/include/SimTask.hpp').is_file():
    raise SystemExit('Missing CMakeLists.txt or SimTask.hpp')
text = cmake.read_text(encoding='utf-8-sig')
marker = '# TASK-SERVER-E2E-02 fixture targets.'
if marker in text or target.exists():
    raise SystemExit('Already installed or target exists; no changes made')
addition = '''\n# TASK-SERVER-E2E-02 fixture targets.\nif(WIN32)\n    foreach(task_id IN ITEMS 0 1 2)\n        add_executable(task_server_e2e_fixture_${task_id}\n            tests/task_server_e2e_fixture.cpp)\n        target_link_libraries(task_server_e2e_fixture_${task_id} PRIVATE SimTask)\n        target_compile_definitions(task_server_e2e_fixture_${task_id} PRIVATE TASK_ID=${task_id})\n    endforeach()\n    target_compile_definitions(task_server_e2e_fixture_2 PRIVATE TASK_FORCE_FAIL=1)\nendif()\n'''
target.write_bytes(src.read_bytes())
cmake.write_text(text + addition, encoding='utf-8')
print('Installed test-only fixture targets. No commit/push.')
