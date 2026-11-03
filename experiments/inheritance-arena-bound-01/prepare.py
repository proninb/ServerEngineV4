"""Expose cache primitives in a renamed generated resolver, not production ABI."""
import hashlib,json
from pathlib import Path
repo=Path(__file__).resolve().parents[2]
out=repo/'build/inheritance-arena-bound-01';out.mkdir(parents=True,exist_ok=True)
source=repo/'server_engine/project/parser/source_endpoint_resolver.hpp'
text=source.read_text()
text=text.replace('"../graph/graph.hpp"','"'+(repo/'server_engine/project/graph/graph.hpp').as_posix()+'"')
text=text.replace('source_endpoint_resolver','arena_test_resolver').replace('private:','public:')
(out/'resolver.hpp').write_text(text)
(out/'source.json').write_text(json.dumps(dict(source=str(source),sha256=hashlib.sha256(source.read_bytes()).hexdigest(),changes=['rename resolver','expose private cache primitives','absolute graph include']),indent=2))
