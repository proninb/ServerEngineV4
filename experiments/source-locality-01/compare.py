"""Check the complete ordered declaration/assignment identity stream against native G."""
import argparse
from collections import Counter
from itertools import zip_longest
import json
from pathlib import Path
from analyze import event_rows


def compare(native, structural):
    maps, inverse = {}, {}
    counts = Counter()
    def bind(domain, left, right):
        left, right = int(left), int(right)
        assert bool(left) == bool(right), (domain, left, right)
        if not left:
            return
        forward, backward = maps.setdefault(domain, {}), inverse.setdefault(domain, {})
        assert left not in forward or forward[left] == right, (domain, left, right)
        assert right not in backward or backward[right] == left, (domain, left, right)
        forward[left], backward[right] = right, left
    def stream(path):
        return (row for row in event_rows(Path(path) / 'events.csv') if row['kind'] != 'W')
    for ordinal, pair in enumerate(zip_longest(stream(native), stream(structural))):
        a, b = pair
        assert a is not None and b is not None, ordinal
        assert a['kind'] == b['kind'], ordinal
        counts[a['kind']] += 1
        bind('roots', a['root'], b['root'])
        bind('files', a['file'], b['file'])
        bind('scopes', a['scope'], b['scope'])
        bind('names', a['name'], b['name'])
        bind('objects', a['who'], b['who'])
        bind('objects', a['source'], b['source'])
        assert a['category'] == b['category'], ordinal
    result = {'status': 'passed', 'ordered_events': dict(counts),
              'identity_mapping_sizes': {key: len(value) for key, value in maps.items()},
              'excluded': 'W: native string-element expansion; type IDs and offsets are not compared'}
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('native', type=Path)
    parser.add_argument('structural', type=Path)
    parser.add_argument('output', type=Path)
    args = parser.parse_args()
    result = compare(args.native, args.structural)
    args.output.write_text(json.dumps(result, indent=2))
    print(json.dumps(result, indent=2))
