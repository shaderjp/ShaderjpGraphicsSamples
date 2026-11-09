"""Validate stable IDs, exact counts, opaque occluders, and the saved-source manifest."""
from pathlib import Path
import json
import hashlib
import argparse
import shutil
import subprocess
ROOT=Path(__file__).resolve().parents[3]
settings=ROOT/'Assets/Scenes/CullingLab'
config=json.loads((settings/'CullingLab.json').read_text())
model=(settings/config['model']).resolve()
data=json.loads(model.read_text())
manifest=json.loads((settings/'Manifest.json').read_text())
assert manifest['sourceSha256']==hashlib.sha256((ROOT/'SourceArt/Blender/CullingLab/CullingLab.blend').read_bytes()).hexdigest()
for file,digest in manifest['files'].items():assert hashlib.sha256((ROOT/file).read_bytes()).hexdigest()==digest,file
for scene in data['scenes']:
    pending=list(scene['nodes']);ids=set();targets=doors=0
    while pending:
        node=data['nodes'][pending.pop()];pending+=node.get('children',[])
        identity=node['extras']['id'];assert identity not in ids,identity;ids.add(identity)
        if 'mesh' not in node:continue
        primitives=data['meshes'][node['mesh']]['primitives']
        if identity.startswith('object:'):targets+=len(primitives)
        if identity.startswith('door:'):doors+=1
        if identity.startswith(('occluder:','door:')):
            for primitive in primitives:assert data['materials'][primitive['material']].get('alphaMode','OPAQUE')=='OPAQUE'
        scale=node.get('scale',[1,1,1]);assert min(scale)>0 and max(scale)-min(scale)<1e-5
    assert targets==manifest['targets'][scene['name']],(scene['name'],targets)
    assert doors==1
assert len(json.loads((settings/'CameraPath.json').read_text())['frames'])==1201
assert len({p['attributes']['POSITION'] for m in data['meshes'] for p in m['primitives']})<len(data['nodes'])
print('CullingLab asset/source manifest and scene counts passed')

parser=argparse.ArgumentParser()
parser.add_argument('--reexport',action='store_true')
parser.add_argument('--blender',default=r'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe')
args=parser.parse_args()
if args.reexport:
    scratch=ROOT/'.cache/culling-reexport'
    sources=['CullingLab/CullingLab.blend','CullingLab/export.py','Workshop/export.py',
             'Workshop/workshop_tools.py','ShaderBall/shader_ball_tools.py']
    for relative in sources:
        target=scratch/'SourceArt/Blender'/relative
        target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(ROOT/'SourceArt/Blender'/relative,target)
    lab=scratch/'SourceArt/Blender/CullingLab'
    startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    result=subprocess.run([args.blender,'--background',str(lab/'CullingLab.blend'),
        '--python-exit-code','1','--python',str(lab/'export.py')],startupinfo=startup,
        stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=180)
    (scratch/'export.log').write_bytes(result.stdout)
    assert result.returncode==0,'Isolated export failed; see .cache/culling-reexport/export.log'
    for file,digest in manifest['files'].items():
        assert hashlib.sha256((scratch/file).read_bytes()).hexdigest()==digest,file
    for name in ['CullingLab.json','CameraPath.json']:
        assert (settings/name).read_bytes()==(scratch/'Assets/Scenes/CullingLab'/name).read_bytes(),name
    print('Isolated saved-source re-export matched every glTF/BIN/PNG and camera setting byte')
