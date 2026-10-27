"""Read-only external native SHM audit, never called during timed requests."""
import ctypes,hashlib,struct
from pathlib import Path
def native_payload(name,runtime):
 data=Path(runtime).read_bytes();count=struct.unpack_from('<I',data,16)[0];assert count==32
 entries=[struct.unpack_from('<IIQQ',data,64+i*24) for i in range(count)]
 kind,stride,offset,n=next(e for e in entries if e[0]==34);assert stride==64 and n==1 and offset+64<=len(data)
 size=struct.unpack_from('<Q',data,offset+24)[0];assert 72<=size<1<<40
 kernel=ctypes.WinDLL('kernel32',use_last_error=True)
 kernel.OpenFileMappingW.argtypes=[ctypes.c_uint32,ctypes.c_int,ctypes.c_wchar_p];kernel.OpenFileMappingW.restype=ctypes.c_void_p
 kernel.MapViewOfFile.argtypes=[ctypes.c_void_p,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_uint32,ctypes.c_size_t];kernel.MapViewOfFile.restype=ctypes.c_void_p
 kernel.UnmapViewOfFile.argtypes=[ctypes.c_void_p];kernel.CloseHandle.argtypes=[ctypes.c_void_p]
 handle=kernel.OpenFileMappingW(4,False,name)
 if not handle:raise ctypes.WinError(ctypes.get_last_error())
 address=kernel.MapViewOfFile(handle,4,0,0,0)
 if not address:kernel.CloseHandle(handle);raise ctypes.WinError(ctypes.get_last_error())
 try:
  digest=hashlib.sha256()
  # runtime_system is 72 bytes of mutable state/time, not G object storage.
  # Everything after it includes unconnected values, objects and their padding.
  for at in range(72,size,4*1024*1024):digest.update(ctypes.string_at(address+at,min(4*1024*1024,size-at)))
  return dict(bytes=size-72,sha256=digest.hexdigest(),excluded_system_bytes=72,fixed_base='0x0000010000000000')
 finally:kernel.UnmapViewOfFile(address);kernel.CloseHandle(handle)
