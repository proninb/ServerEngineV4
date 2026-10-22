"""Real production preprocessing + independent Header V2 oracle, no PUBLISH writes."""
import json
import pathlib
import subprocess
import sys
import tempfile

exe = pathlib.Path(sys.argv[1]).resolve()
parent = pathlib.Path(sys.argv[2]).resolve()
parent.mkdir(parents=True, exist_ok=True)
cases = [
    (["struct A; struct B { A* p; }; struct A { int x; };"], True),
    (["struct A; struct B { A value; }; struct A { int x; };"], False),
    (["struct B { A* p; }; struct A {};"], False),
    (["struct A {};", "struct B { A value; };"], True),
    (["struct B { A* value; };", "struct A {};"], False),
    (["namespace N { struct A; }", "namespace N { struct A {}; struct B { A value; }; }"], True),
    (["struct A {}; namespace N { struct B { A value; }; struct A; }"], True),
    (["struct A {}; namespace N { struct A; struct B { A value; }; }"], False),
    (["struct A { A* next; };"], True),
    (["struct A { A value; };"], False),
    (["struct A; struct B : A {}; struct A {};"], False),
    (["struct A {}; struct B : public A { int x; };"], True),
    (["union A {}; struct B : A {};"], False),
    (["struct A; union A;"], False),
    (["struct A; class A {};"], True),
    (["typedef int X; struct A { X x; };"], True),
    (["typedef int X; typedef bool X;"], False),
    (["typedef int X; struct X {};"], False),
    (["struct X; typedef int X; struct B { X value; };"], True),
    (["typedef int X; struct X; struct B { X value; };"], True),
    (["struct X; struct B { X* value; }; typedef int X;"], True),
    (["struct A { int x; };", "struct A { bool x; };"], False),
    (["struct A { int x; };", "struct A { int x; };"], True),
    (["struct A { const int* x[2]; volatile short & y; };"], True),
    (["struct A { int (*p)[3]; short (&r)[6]; };"], True),
    (["namespace N { struct A {}; } namespace N { struct B { A value; }; }"], True),
    (["namespace N { struct A {}; } struct B { A* value; };"], False),
    (["#pragma once\nstruct A {};", '#include "0.h"\n#include "0.h"\nstruct B { A value; };'], True),
    (["#pragma once\nstruct A {};", 'namespace N {\n#include "0.h"\nstruct B { A value; }; }'], True),
    (["#pragma once\nstruct A {};", '#define A B\n#include "0.h"\nstruct C { B value; };'], True),
]
checks = 0
with tempfile.TemporaryDirectory(prefix="oracle-", dir=parent) as folder:
    root = pathlib.Path(folder)
    for index, (sources, expected) in enumerate(cases):
        case = root / str(index)
        case.mkdir()
        project = {"version": 1, "name": "bind_fixture", "project": [], "preprocessor": {"predefines": []}}
        for ordinal, source in enumerate(sources):
            name = f"{ordinal}.h"
            (case / name).write_text(source, encoding="utf-8")
            project["project"].append({"name": name, "type": "header", "path": name})
        path = case / "project.json"
        path.write_text(json.dumps(project), encoding="utf-8")
        for workers in (1, 4):
            p = subprocess.run([str(exe), str(path), str(workers), "2"], capture_output=True, text=True)
            if p.returncode:
                raise AssertionError(f"case={index} workers={workers}: {sources}\n{p.stdout}\n{p.stderr}")
            for line in p.stdout.splitlines():
                values = dict(field.split("=", 1) for field in line.split(","))
                assert values["oracle_accepted"] == str(int(expected)), (index, line)
                assert values["validation"] == "passed", line
                if not expected:
                    assert values["candidate_error_file"] == values["oracle_error_file"], line
                    assert values["candidate_error_offset"] == values["oracle_error_offset"], line
                checks += 1
print(f"oracle_checks={checks},validation=passed")
