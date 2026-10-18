"""Freeze executable/shader/asset bytes without modifying build outputs."""
from pathlib import Path
import hashlib,json,shutil,uuid
ROOT=Path(__file__).resolve().parents[1]
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
 out=ROOT/'.cache'/('bindless-runtime-'+uuid.uuid4().hex[:12]);out.mkdir()
 programs={}
 for api in ['Direct3D12','Vulkan']:
  for n,topic in [(12,'BindlessMaterials'),(13,'BindlessMeshShader'),(14,'BindlessRaytracing'),(15,'BindlessHybrid')]:
   name=f'{api}_Advanced{n}_{topic}';source=ROOT/'bin/x64/Release'/name;exe=source/(name+'.exe');before=sha(exe)
   ignored={'.pdb','.ilk','.iobj','.ipdb'}
   original={str(p.relative_to(source)).replace('\\','/'):sha(p)for p in source.rglob('*')if p.is_file() and p.suffix not in ignored}
   shutil.copytree(source,out/name,ignore=shutil.ignore_patterns('*.pdb','*.ilk','*.iobj','*.ipdb'))
   if sha(exe)!=before or sha(out/name/exe.name)!=before:raise RuntimeError('Concurrent build while freezing '+name)
   programs[name]={str(p.relative_to(out/name)).replace('\\','/'):sha(p)for p in sorted((out/name).rglob('*'))if p.is_file()}
   after={str(p.relative_to(source)).replace('\\','/'):sha(p)for p in source.rglob('*')if p.is_file() and p.suffix not in ignored}
   if original!=after or original!=programs[name]:raise RuntimeError('Runtime files changed while freezing '+name)
 (out/'Snapshot.json').write_text(json.dumps({'programs':programs,'source':'bin/x64/Release'},indent=2)+'\n')
 (ROOT/'.cache/bindless/RuntimePath.txt').write_text(str(out))
 print('Frozen runtime:',out)
if __name__=='__main__':main()
