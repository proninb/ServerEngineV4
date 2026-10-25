"""Match PE CodeView GUID+age to the actual MSF7 PDB information stream."""
import hashlib
import pathlib
import struct
import uuid

def u32(data,offset):return struct.unpack_from('<I',data,offset)[0]
def pe_info(path):
    data=pathlib.Path(path).read_bytes();pe=u32(data,0x3c)
    assert data[:2]==b'MZ' and data[pe:pe+4]==b'PE\0\0','not a PE image'
    sections_count=struct.unpack_from('<H',data,pe+6)[0]
    optional=pe+24;optional_size=struct.unpack_from('<H',data,pe+20)[0]
    magic=struct.unpack_from('<H',data,optional)[0];assert magic in (0x10b,0x20b)
    directories=optional+(112 if magic==0x20b else 96)
    rva,size=struct.unpack_from('<II',data,directories+6*8)
    sections=[]
    for i in range(sections_count):
        pos=optional+optional_size+i*40
        name=data[pos:pos+8].rstrip(b'\0').decode('ascii')
        virtual_size,address,raw_size,raw_offset=struct.unpack_from('<IIII',data,pos+8)
        sections.append((name,address,max(virtual_size,raw_size),raw_offset,raw_size))
    result={'sha256':hashlib.sha256(data).hexdigest(),
            'text_sha256':next(hashlib.sha256(data[p:p+n]).hexdigest() for name,_,_,p,n in sections if name=='.text')}
    if not rva or not size:return result
    def offset(address):
        for _,base,length,raw,_ in sections:
            if base<=address<base+length:return raw+address-base
        raise ValueError('unmapped debug RVA')
    for pos in range(offset(rva),offset(rva)+size,28):
        kind,length,_,raw=struct.unpack_from('<IIII',data,pos+12)
        if kind==2 and data[raw:raw+4]==b'RSDS':
            result.update(guid=str(uuid.UUID(bytes_le=data[raw+4:raw+20])),age=u32(data,raw+20),
                          pdb_path=data[raw+24:raw+length].split(b'\0')[0].decode('utf-8'))
            return result
    return result

def pdb_info(path):
    data=pathlib.Path(path).read_bytes()
    assert data.startswith(b'Microsoft C/C++ MSF 7.00\r\n\x1aDS\0\0\0'),'not an MSF7 PDB'
    block=u32(data,32);directory_size=u32(data,44);block_map=u32(data,52)
    pages=(directory_size+block-1)//block
    assert pages*4<=block,'multi-page directory block map unsupported'
    directory=b''.join(data[p*block:(p+1)*block] for p in struct.unpack_from('<'+'I'*pages,data,block_map*block))[:directory_size]
    count=u32(directory,0);sizes=struct.unpack_from('<'+'I'*count,directory,4);cursor=4+4*count
    info=None
    for index,length in enumerate(sizes):
        if length==0xffffffff:continue
        n=(length+block-1)//block;page_ids=struct.unpack_from('<'+'I'*n,directory,cursor);cursor+=4*n
        if index==1:info=b''.join(data[p*block:(p+1)*block] for p in page_ids)[:length]
    assert info is not None and len(info)>=28,'missing PDB info stream'
    return {'sha256':hashlib.sha256(data).hexdigest(),'guid':str(uuid.UUID(bytes_le=info[12:28])),'age':u32(info,8)}

def verify(exe,pdb):
    image=pe_info(exe);debug=pdb_info(pdb)
    assert 'guid' in image,'EXE has no RSDS debug identity'
    assert (image['guid'],image['age'])==(debug['guid'],debug['age']),'PDB does not match EXE'
    return {'exe':image,'pdb':debug,'match':True}
