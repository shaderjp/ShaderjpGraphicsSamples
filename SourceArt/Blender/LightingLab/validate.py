"""Inspect generated assets and deterministic numerical IBL invariants. MIT License."""
from pathlib import Path
import argparse,hashlib,json,sys
import numpy as np
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE))
from ibl import read_dds,integrate,directions

def validate(asset_root,compare=None,verify_manifest=True):
    models=asset_root/'Models'/'LightingLab';envs=asset_root/'Environments'/'LightingLab'
    triangles=0
    for path in sorted(models.glob('*/*.gltf')):
        j=json.loads(path.read_text());assert len(j['nodes'])==len(j['meshes'])==len(j['materials'])==1
        assert len(j['meshes'][0]['primitives'])==1 and not j.get('extensionsUsed')
        primitive=j['meshes'][0]['primitives'][0];assert 'TANGENT' in primitive['attributes']
        triangles+=j['accessors'][primitive['indices']]['count']//3
        m=j['materials'][0];t=j['textures'];a=m['occlusionTexture']['index'];b=m['pbrMetallicRoughness']['metallicRoughnessTexture']['index']
        assert t[a]['source']==t[b]['source']
        assert len({x.get('sampler',-1) for x in t})==1
        for image in j['images']:assert (path.parent/image['uri']).is_file()
        if compare:
            for p in [path,path.parent/j['buffers'][0]['uri']]+[path.parent/i['uri'] for i in j['images']]:
                q=compare/p.relative_to(asset_root);assert p.read_bytes()==q.read_bytes(),str(p)
    assert triangles<=50000 and len(list(models.glob('*/*.gltf')))==17
    for name in ('Studio','Room'):
        for suffix,size,mips in [('Environment',512,1),('Irradiance',32,1),('Specular',256,9)]:
            faces=read_dds(envs/f'{name}_{suffix}.dds');assert len(faces)==6 and len(faces[0])==mips
            assert faces[0][0].shape==(size,size,4)
            for face in faces:
                for image in face:assert np.isfinite(image).all() and np.min(image)>=0
        assert max(np.max(f[0][...,:3]) for f in read_dds(envs/f'{name}_Environment.dds'))>1,'HDR lost'
    assert read_dds(envs/'BRDF.dds')[0][0].shape==(256,256,2)
    orientation=read_dds(envs/'Orientation.dds');assert len(orientation)==6
    # Axis center directions, handedness, and sign are independently checked.
    axes=np.eye(3)[[0,0,1,1,2,2]]*np.array([1,-1,1,-1,1,-1])[:,None]
    for face in range(6):assert np.allclose(directions(1,face)[0,0],axes[face])
    constant=np.ones((16,32,4))*np.array([.25,.5,2,1])
    for face in range(6):
        n=directions(3,face)
        assert np.allclose(integrate(constant,n),np.pi*np.array([.25,.5,2]),atol=1e-10)
        for mip in range(1,9):assert np.allclose(integrate(constant,n,mip/8),[.25,.5,2],atol=1e-10)
    manifest_path=asset_root/'Scenes'/'LightingLab'/'Manifest.json'
    if verify_manifest and manifest_path.exists():
        manifest=json.loads(manifest_path.read_text())
        for record in manifest['files']:
            p=asset_root/record['path'];assert p.stat().st_size==record['bytes'] and hashlib.sha256(p.read_bytes()).hexdigest()==record['sha256'],f'Manifest mismatch: {p}'
    return triangles

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('--assets',type=Path,default=ROOT/'Assets');parser.add_argument('--compare',type=Path);parser.add_argument('--manifest',action='store_true');a=parser.parse_args()
    count=validate(a.assets,a.compare,not a.manifest)
    if a.manifest:
        files=[p for folder in ['Models/LightingLab','Environments/LightingLab','Scenes/LightingLab'] for p in (a.assets/folder).rglob('*') if p.is_file() and p.name!='Manifest.json']
        manifest={'license':'MIT','copyright':'Copyright (c) 2026 masafumi','blender':'5.2.1 LTS','blenderBuild':'9e2066aef7ef',
            'coordinateSystem':'DDS: left-handed Y-up, glTF/scene: right-handed Y-up meters','faceOrder':['+X','-X','+Y','-Y','+Z','-Z'],
            'colorSpace':'scene-linear Rec.709 RGB; no display transform','irradiance':'E = integral(Li * NoL dOmega); shader divides by pi',
            'ggx':{'samples':1024,'sequence':'Hammersley','alpha':'max(perceptualRoughness, 0.045)^2','geometry':'height-correlated Smith',
                   'specularMipRoughness':[i/8 for i in range(9)],'mip0':'ideal mirror resample','BRDF':'Schlick split sum A/B'},
            'environmentRender':{'engine':'Cycles CPU','samples':256,'seed':0,'denoise':True,'projection':'equirectangular','width':2048,'height':1024,'EXR':'float32 scene-linear'},
            'sources':[{'path':p.relative_to(ROOT).as_posix(),'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(HERE.iterdir()) if p.is_file() and p.suffix in ('.blend','.exr','.py')],
            'triangles':count,'files':[{ 'path':p.relative_to(a.assets).as_posix(),'bytes':p.stat().st_size,'sha256':hashlib.sha256(p.read_bytes()).hexdigest()} for p in sorted(files)]}
        (a.assets/'Scenes'/'LightingLab'/'Manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    print(f'LightingLab asset profiles and IBL constant-environment invariants PASS ({count} triangles)')
