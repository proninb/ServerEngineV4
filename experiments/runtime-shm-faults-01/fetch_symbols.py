"""Download only public OS PDBs identified by the trace; never upload trace data."""
import hashlib
import json
import pathlib
import sys
import struct
import urllib.request
import uuid

repo = pathlib.Path(__file__).resolve().parents[2]
sys.path.insert(0, str(repo / 'experiments/source-cpu-sampling-02'))
from symbols import pdb_info, u32


def validate(path, guid, image_age):
    info = pdb_info(path)
    data = path.read_bytes()
    block, directory_size, block_map = u32(data, 32), u32(data, 44), u32(data, 52)
    pages = (directory_size + block - 1) // block
    assert pages * 4 <= block
    directory = b''.join(data[p * block:(p + 1) * block]
                        for p in struct.unpack_from('<' + 'I' * pages, data, block_map * block))[:directory_size]
    count = u32(directory, 0)
    sizes = struct.unpack_from('<' + 'I' * count, directory, 4)
    cursor = 4 + 4 * count
    dbi = None
    for index, size in enumerate(sizes):
        if size == 0xffffffff:
            continue
        n = (size + block - 1) // block
        ids = struct.unpack_from('<' + 'I' * n, directory, cursor)
        cursor += 4 * n
        if index == 3:
            dbi = b''.join(data[p * block:(p + 1) * block] for p in ids)[:size]
            break
    assert dbi is not None and len(dbi) >= 12
    dbi_age = u32(dbi, 8)
    # Microsoft PDB OpenValidate4: GUID exact, PDB age >= image age,
    # DBI age == image age (legacy zero DBI age is also permitted).
    assert info['guid'] == guid and info['age'] >= image_age and dbi_age in (0, image_age), (info, dbi_age)
    return {**info, 'image_age': image_age, 'dbi_age': dbi_age}

def main():
    inspection = json.loads((repo / 'build/runtime-shm-faults-01/inspection-b0.json').read_text())
    destination = repo / 'build/runtime-shm-faults-01/symbols'
    destination.mkdir(exist_ok=True)
    records = []
    for module in inspection['modules']:
        name = module['properties']['PdbName']
        if name not in ('ntkrnlmp.pdb', 'ntdll.pdb'):
            continue
        guid, age = str(uuid.UUID(module['PdbSignature'])), module['PdbAge']
        key = uuid.UUID(guid).hex.upper() + format(age, 'X')
        url = f'https://msdl.microsoft.com/download/symbols/{name}/{key}/{name}'
        target = destination / name / key / name
        if not target.exists():
            target.parent.mkdir(parents=True, exist_ok=True)
            temporary = target.with_suffix('.download')
            urllib.request.urlretrieve(url, temporary)
            identity = validate(temporary, guid, age)
            temporary.rename(target)
        identity = validate(target, guid, age)
        records.append({'module': module['Name'], 'pdb': str(target), 'url': url,
                        'bytes': target.stat().st_size, **identity})
    (destination / 'manifest.json').write_text(json.dumps(records, indent=2))
    print(json.dumps(records, indent=2))


if __name__ == "__main__":
    main()
