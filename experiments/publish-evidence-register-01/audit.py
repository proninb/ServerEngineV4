"""Read-only integrity check of the dated PUBLISH evidence register."""
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[2]
DOC = ROOT / "docs/PUBLISH_EVIDENCE_REGISTER.md"
SNAPSHOT = DOC.with_suffix(".json")

def main():
    data = json.loads(SNAPSHOT.read_text(encoding="utf-8"))
    reports = data["reports"]
    errors = []
    paths = [item["path"] for item in reports]
    if len(paths) != len(set(paths)):
        errors.append("Duplicate report paths")
    for item in reports:
        source = ROOT / item["path"]
        if not source.is_file():
            errors.append("Missing report: " + item["path"])
        elif hashlib.sha256(source.read_bytes()).hexdigest() != item["sha256"]:
            errors.append("Source changed since snapshot: " + item["path"])
    for target in re.findall(r"\]\(([^)]+)\)", DOC.read_text(encoding="utf-8")):
        if not (DOC.parent / target).is_file():
            errors.append("Broken link: " + target)
    print(json.dumps({"reports": len(reports), "errors": errors}, indent=2))
    return bool(errors)

if __name__ == "__main__":
    raise SystemExit(main())
