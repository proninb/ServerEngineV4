"""Convert legacy book includes into ordered V4 source nodes; preserve source bytes."""
import argparse
import hashlib
import json
from pathlib import Path
import re
from prepare_unitproxl_headers import TOKEN

INCLUDE = re.compile(rb'^\s*#include\s+"([^"\r\n]+)"\s*(?://[^\r\n]*)?$')


def prepare(book, destination, header, expand_sdk_byte=False):
    book, destination = book.resolve(), destination.resolve()
    source_root = book.parent
    if destination == source_root or source_root in destination.parents or destination in source_root.parents:
        raise ValueError("Source and destination must be separate directory trees")
    records, active, seen = [], set(), set()

    def source_node(path, data, original):
        original_hash = hashlib.sha256(data).hexdigest()
        expansions = 0
        if expand_sdk_byte:
            def expand_token(match):
                nonlocal expansions
                if match.group() == b"byte":
                    expansions += 1
                    return b"unsigned char"
                return match.group()
            data = TOKEN.sub(expand_token, data)
        target = destination / "Sources" / path
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data)
        records.append({"source": str(original), "path": target.relative_to(destination).as_posix(),
                        "sha256": hashlib.sha256(data).hexdigest(),
                        "original_sha256": original_hash, "byte_expansions": expansions})
        return {"name": path.name, "type": "source", "path": target.relative_to(destination).as_posix()}

    def expand(path):
        path = path.resolve()
        relative = path.relative_to(source_root)
        if path in active:
            raise ValueError(f"Cyclic book include: {path}")
        if path in seen:
            raise ValueError(f"Repeated input requires explicit review: {path}")
        seen.add(path)
        data = path.read_bytes()
        if path.suffix.lower() != ".book":
            return source_node(relative, data, path)
        active.add(path)
        children, chunk = [], bytearray()
        part = 0

        def flush():
            nonlocal part
            if chunk.strip():
                part += 1
                output = relative.with_name(relative.name + f".part{part}.source")
                children.append(source_node(output, bytes(chunk), path))
            chunk.clear()

        for line in data.splitlines(keepends=True):
            match = INCLUDE.fullmatch(line.rstrip(b"\r\n"))
            if match:
                flush()
                include = match[1].decode("utf-8").replace("\\", "/")
                children.append(expand(path.parent / include))
            elif line.lstrip().startswith(b"#"):
                raise ValueError(f"Unsupported book directive in {path}: {line!r}")
            else:
                chunk.extend(line)
        flush()
        active.remove(path)
        return {"name": path.stem, "type": "group", "children": children}

    sources = expand(book)
    configuration = {"version": 1, "name": book.stem,
                     "project": [{"name": "Types", "type": "group", "children": [
                         {"name": Path(header).name, "type": "header", "path": header}]}, sources],
                     "preprocessor": {"predefines": [], "include_directories": ["Types"]}}
    destination.mkdir(parents=True, exist_ok=True)
    (destination / "project.json").write_text(json.dumps(configuration, indent=2) + "\n", encoding="utf-8")
    (destination / "source-transfer.json").write_text(json.dumps(records, indent=2) + "\n", encoding="utf-8")
    return len(seen), len(records)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("book", type=Path)
    parser.add_argument("destination", type=Path)
    parser.add_argument("--header", default="Types/unity_pro_xl.h")
    parser.add_argument("--expand-sdk-byte", action="store_true")
    args = parser.parse_args()
    inputs, sources = prepare(args.book, args.destination, args.header, args.expand_sdk_byte)
    print(f"Converted {inputs} inputs into {sources} source nodes: {args.destination / 'project.json'}")
