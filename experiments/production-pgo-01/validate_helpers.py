import hashlib
def images(project):
 result={}
 for name in ('compiled.bin','runtime.bin'):
  with (project.parent/'.serverengine'/project.name/name).open('rb') as f:result[name]=hashlib.file_digest(f,'sha256').hexdigest()
 return result
