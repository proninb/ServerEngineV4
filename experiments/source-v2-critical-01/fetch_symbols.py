"""Download matching public Windows PDBs from Microsoft's symbol server only."""
from pathlib import Path
import hashlib,json,re,struct,sys,urllib.request,uuid
repo=Path(__file__).resolve().parents[2]
out=Path(sys.argv[1]).resolve()
assert out.is_relative_to(repo/'build')
sys.path.insert(0,str(repo/'experiments/source-cpu-sampling-02'))
from symbols import pdb_info

def dbi_age(path):
    data=path.read_bytes();u=lambda offset:struct.unpack_from('<I',data,offset)[0]
    block=u(32);length=u(44);count=(length+block-1)//block
    ids=struct.unpack_from('<'+'I'*count,data,u(52)*block)
    directory=b''.join(data[i*block:(i+1)*block] for i in ids)[:length]
    n=struct.unpack_from('<I',directory)[0]
    sizes=struct.unpack_from('<'+'I'*n,directory,4);cursor=4+4*n
    for index,size in enumerate(sizes):
        if size==0xffffffff:continue
        count=(size+block-1)//block
        ids=struct.unpack_from('<'+'I'*count,directory,cursor);cursor+=4*count
        if index==3:
            stream=b''.join(data[i*block:(i+1)*block] for i in ids)[:size]
            return struct.unpack_from('<I',stream,8)[0]
    raise ValueError('Missing DBI stream')
requests=set()
for log in (out/'capture/analysis').glob('b*-samples.jsonl.symbols.log'):
    for name,guid,age in re.findall(r'Locating PDB (\S+) GUID ([0-9a-fA-F-]+) Age (\d+)',log.read_text(errors='replace')):
        name=Path(name).name
        if not name.lower().startswith(('ntkrnlmp','ntdll','kernel','wkernel','ucrtbase','msvcrt','vcruntime')):continue
        requests.add((name,str(uuid.UUID(guid)),int(age)))
result=[]
for name,guid,age in sorted(requests):
    key=guid.replace('-','').upper()+format(age,'x').upper()
    path=out/'system-symbols'/name/key/name
    path.parent.mkdir(parents=True,exist_ok=True)
    url=f'https://msdl.microsoft.com/download/symbols/{name}/{key}/{name}'
    try:
        existing_valid=False
        try:existing_valid=path.exists() and pdb_info(path)['guid']==guid and dbi_age(path)==age
        except Exception:pass
        if not existing_valid:
            with urllib.request.urlopen(url,timeout=60) as source,path.open('wb') as destination:
                while data:=source.read(1024*1024):destination.write(data)
        identity=pdb_info(path)
        identity['dbi_age']=dbi_age(path)
        # Microsoft PDB validation requires PDB age >= image age, DBI age == image.
        # Keep the stricter EXE/PDB check for our own application unchanged.
        assert identity['guid']==guid and identity['age']>=age and identity['dbi_age']==age,(path,identity)
        result.append(dict(name=name,path=str(path),url=url,identity=identity,status='matching'))
        print('MATCH',name,guid,age,flush=True)
    except Exception as error:
        result.append(dict(name=name,url=url,status='unavailable',error=str(error)))
        print('UNAVAILABLE',name,str(error),flush=True)
(out/'system-symbols/manifest.json').write_text(json.dumps(result,indent=2))
