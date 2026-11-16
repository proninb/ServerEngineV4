"""Prepare a ServerEngineV4 project from a legacy UnitProTr export."""
import argparse
import hashlib
import json
import re
import subprocess
import sys
from pathlib import Path

from prepare_unitproxl_project import prepare
from prepare_unitproxl_headers import TOKEN


SDK_TYPES = """#pragma once
// Data declarations from the legacy SimObj.h SDK; no task execution emulation.
typedef unsigned char byte;
struct SimObject {};
struct SimObjectEx : SimObject { const int* _TypeIndex; };
struct SimLink : SimObject {
    // Exported link metadata also addresses complete arrays, not only objects.
    void* From;
    void* To;
};
struct SimLinkEx : SimLink { const int* _TypeIndex; };
struct TRANSFER : SimLink {};
struct GLUE : SimLink {};
struct ASSIGN : SimLink {
    int Len;
    char Assign[1];
};

#include <unity_pro_xl_base.h>
#include <unity_pro_xl_array.h>
#include <unity_pro_xl_expression.h>
#include <unity_pro_xl_variable.h>
#include <unity_pro_xl_database.h>
#include <unity_pro_xl_ddt.h>
#include <unity_pro_xl_st.h>
#include <unity_pro_xl_fb.h>
"""


def widen_array_lengths(destination):
    """Keep generic array extents representable beyond the legacy byte limit."""
    path = destination / "Types/unity_pro_xl_base.h"
    data = path.read_bytes()
    names = ("INT", "BOOL", "BYTE", "DDT")
    storage = {}
    for name in names:
        pattern = rb"(struct\s+U_ANY_ARRAY_" + name.encode() + rb"\b[^\{]*\{\s*)(byte|(?:unsigned\s+)?int)(\s+LEN\s*;)"
        def widen(match):
            declared = match[2]
            selected = b"unsigned int" if declared == b"byte" else declared
            storage["U_ANY_ARRAY_" + name] = selected.decode("ascii")
            return match[1] + selected + match[3]
        data, count = re.subn(pattern, widen, data)
        if count != 1:
            raise ValueError(f"Expected exactly one LEN declaration in U_ANY_ARRAY_{name}")
    path.write_bytes(data)
    manifest = destination / "Types/annotation-removal.json"
    report = json.loads(manifest.read_text(encoding="utf-8"))
    for record in report["files"]:
        if record["path"] == "unity_pro_xl_base.h":
            record["output_sha256"] = hashlib.sha256(data).hexdigest()
            record["array_len_storage"] = storage
            record["array_len_types"] = ["U_ANY_ARRAY_" + name for name in names]
    manifest.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")


def strip_source_hidden(data):
    """Blank annotation tokens while preserving literals, comments and offsets."""
    count = 0
    def replace(match):
        nonlocal count
        if match.group() == b"HIDDEN":
            count += 1
            return b" " * len(match.group())
        return match.group()
    if b"HIDDEN" not in data:
        return data, count
    return TOKEN.sub(replace, data), count


def clean_source_hidden(destination):
    destination = destination.resolve()
    manifest = destination / "source-transfer.json"
    records = json.loads(manifest.read_text(encoding="utf-8"))
    changed = removed = 0
    for record in records:
        path = (destination / record["path"]).resolve()
        if not path.is_relative_to(destination / "Sources"):
            raise ValueError(f"Source path outside destination: {path}")
        data = path.read_bytes()
        if hashlib.sha256(data).hexdigest() != record["sha256"]:
            raise ValueError(f"Source copy changed since transfer: {path}")
        cleaned, count = strip_source_hidden(data)
        if count:
            path.write_bytes(cleaned)
            record["sha256"] = hashlib.sha256(cleaned).hexdigest()
            record["removed_hidden"] = record.get("removed_hidden", 0) + count
            changed += 1
            removed += count
    manifest.write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    report = {"removed_hidden": sum(r.get("removed_hidden", 0) for r in records),
              "changed_files": sum(bool(r.get("removed_hidden", 0)) for r in records)}
    (destination / "source-annotation-removal.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    return changed, removed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path, help="Legacy Project directory containing Types and Books")
    parser.add_argument("destination", type=Path)
    args = parser.parse_args()
    source, destination = args.source.resolve(), args.destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        parser.error("Source and destination must be separate directory trees")
    book = source / "Books/UnityProXL/UnityProXL.book"
    if not book.is_file() or not (source / "Types").is_dir():
        parser.error("Expected Types and Books/UnityProXL/UnityProXL.book")
    subprocess.run([
        sys.executable, str(Path(__file__).with_name("prepare_unitproxl_headers.py")),
        str(source / "Types"), str(destination / "Types"),
    ], check=True)
    widen_array_lengths(destination)
    (destination / "Types/unitprotr.h").write_text(SDK_TYPES, encoding="utf-8")
    inputs, sources = prepare(book, destination, "Types/unitprotr.h", expand_chapters=True)
    changed, removed = clean_source_hidden(destination)
    print(f"Converted {inputs} inputs into {sources} source nodes: {destination / 'project.json'}")
    print(f"Removed {removed} Source HIDDEN annotations in {changed} files")


if __name__ == "__main__":
    main()
