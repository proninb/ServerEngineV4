"""Copy legacy headers, removing Studio annotations and optionally expanding SDK byte."""
import argparse
import collections
import hashlib
import json
from pathlib import Path
import re


# Work on bytes to preserve the original encoding, comments and line endings.
TOKEN = re.compile(
    rb'//[^\r\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"'
    rb'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'|[A-Za-z_][A-Za-z_0-9]*|[^\s]',
    re.DOTALL,
)
CALLS = {b"FOR", b"GRAPHICS", b"COLOR"}
MARKERS = {b"HIDDEN", b"FUNCTIONS"}


def strip_annotations(data):
    tokens = [m for m in TOKEN.finditer(data)
              if not m.group().startswith((b"//", b"/*"))]
    removed = collections.Counter()
    output = bytearray(data)
    i = 0
    while i < len(tokens):
        token = tokens[i]
        name = token.group()
        if name in CALLS and i + 1 < len(tokens) and tokens[i + 1].group() == b"(":
            depth = 0
            end = i + 1
            while end < len(tokens):
                spelling = tokens[end].group()
                if spelling == b"(":
                    depth += 1
                elif spelling == b")":
                    depth -= 1
                    if depth == 0:
                        break
                end += 1
            if depth:
                raise ValueError(f"Unclosed {name.decode()} at byte {token.start()}")
        elif name in MARKERS:
            end = i
        else:
            i += 1
            continue
        # Preserve comments between tokens as well as newlines and offsets.
        for part in tokens[i:end + 1]:
            for pos in range(part.start(), part.end()):
                if output[pos] not in (10, 13):
                    output[pos] = 32
        removed[name.decode()] += 1
        i = end + 1
    return bytes(output), removed


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--expand-sdk-byte", action="store_true",
                        help="Expand the legacy SDK byte identifier to unsigned char")
    parser.add_argument("--typedef-sdk-byte", action="store_true",
                        help="Declare typedef unsigned char byte in the copied base header")
    parser.add_argument("--empty-simlink", action="store_true",
                        help="Add the requested empty SimLink to unity_pro_xl_base.h")
    args = parser.parse_args()
    if args.expand_sdk_byte and args.typedef_sdk_byte:
        parser.error("Choose typedef or textual expansion, not both")
    source, destination = args.source.resolve(), args.destination.resolve()
    if source == destination or source in destination.parents or destination in source.parents:
        parser.error("Source and destination must be separate directory trees")
    paths = sorted(source.rglob("*.h"))
    if not paths:
        parser.error("No headers found")
    total = collections.Counter()
    records = []
    for path in paths:
        data = path.read_bytes()
        cleaned, counts = strip_annotations(data)
        # Ensure every recognized annotation was removed without touching the source.
        assert not strip_annotations(cleaned)[1]
        expanded_byte = 0
        if args.expand_sdk_byte:
            def expand(match):
                nonlocal expanded_byte
                if match.group() == b"byte":
                    expanded_byte += 1
                    return b"unsigned char"
                return match.group()
            cleaned = TOKEN.sub(expand, cleaned)
        if (args.empty_simlink or args.typedef_sdk_byte) and path.relative_to(source).as_posix() == "unity_pro_xl_base.h":
            anchor = b"struct SimObject {};"
            newline = b"\r\n" if b"\r\n" in cleaned else b"\n"
            if cleaned.count(anchor) == 0:
                base = b"struct U_BASE : SimObject"
                if cleaned.count(base) != 1:
                    raise ValueError("Expected one SDK SimObject base in unity_pro_xl_base.h")
                cleaned = cleaned.replace(base, anchor + newline + newline + base, 1)
            if cleaned.count(anchor) != 1:
                raise ValueError("Expected one SimObject declaration in unity_pro_xl_base.h")
            declarations = anchor
            if args.empty_simlink and b"struct SimLink {};" not in cleaned:
                declarations += newline + b"struct SimLink {};"
            if args.typedef_sdk_byte and b"typedef unsigned char byte;" not in cleaned:
                declarations += newline + b"typedef unsigned char byte;"
            cleaned = cleaned.replace(anchor, declarations, 1)
        target = destination / path.relative_to(source)
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(cleaned)
        if path.read_bytes() != data:
            raise RuntimeError(f"Source changed during copy: {path}")
        total.update(counts)
        records.append({"path": path.relative_to(source).as_posix(),
                        "source_sha256": hashlib.sha256(data).hexdigest(),
                        "output_sha256": hashlib.sha256(cleaned).hexdigest(),
                        "expanded_byte": expanded_byte,
                        "removed": dict(counts)})
    report = {"source": str(source), "destination": str(destination),
              "headers": len(records), "removed": dict(total),
              "expanded_byte": sum(record["expanded_byte"] for record in records),
              "empty_simlink": args.empty_simlink,
              "typedef_sdk_byte": args.typedef_sdk_byte,
              "files": records}
    (destination / "annotation-removal.json").write_text(
        json.dumps(report, indent=2), encoding="utf-8")
    print(json.dumps({k: v for k, v in report.items() if k != "files"}, indent=2))


if __name__ == "__main__":
    main()
