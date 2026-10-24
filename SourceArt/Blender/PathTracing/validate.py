"""Validate distribution and isolated re-export. MIT License, masafumi 2026."""
from pathlib import Path
import argparse, hashlib, json
import numpy as np
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def inspect(assets):
    folder=assets/'Models/PathTracing';data=json.loads((folder/'PathTracing.gltf').read_text())
    assert data['asset']['version']=='2.0' and len(data['meshes'])==10 and len(data['materials'])==7
    assert data['scenes'][0]['name']=='Room'
    assert set(data.get('extensionsUsed',[])) <= {'KHR_materials_emissive_strength'}
    assert not any(k in data for k in ('skins','animations'))
    buffers=[(folder/b['uri']).read_bytes() for b in data['buffers']]
    def accessor(i):
        a=data['accessors'][i];v=data['bufferViews'][a['bufferView']];dtype={5126:'<f4',5123:'<u2',5125:'<u4'}[a['componentType']];width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        return np.ndarray((a['count'],width),dtype=dtype,buffer=buffers[v['buffer']],offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',width*np.dtype(dtype).itemsize),np.dtype(dtype).itemsize)).copy()
    ids=[];arrays=[]
    for category in ('scenes','nodes','meshes','materials'):
        ids.extend(x['extras']['id'] for x in data[category])
    for mesh in data['meshes']:
        for primitive in mesh['primitives']:
            ids.append(primitive['extras']['id']);assert primitive.get('mode',4)==4
            attrs={k:accessor(primitive['attributes'][k]) for k in ['POSITION','NORMAL','TEXCOORD_0','TANGENT']}
            assert len({len(v) for v in attrs.values()})==1
            for v in attrs.values():assert np.isfinite(v).all()
            assert np.allclose(np.linalg.norm(attrs['NORMAL'],axis=1),1,atol=2e-4)
            assert np.allclose(np.linalg.norm(attrs['TANGENT'][:,:3],axis=1),1,atol=2e-4)
            indices=accessor(primitive['indices']);assert indices.max()<len(attrs['POSITION']) and len(indices)%3==0
            arrays.extend([*attrs.values(),indices])
    assert len(ids)==len(set(ids)),'Stable IDs must be unique'
    for image in data['images']:assert (folder/image['uri']).is_file()
    settings=json.loads((assets/'Scenes/PathTracing/PathTracing.json').read_text());assert settings['profile']=='pathtracing-scene-1' and len(settings['cameras'])==2 and 'environment' not in settings
    return arrays
def main():
    p=argparse.ArgumentParser();p.add_argument('--compare',type=Path);p.add_argument('--manifest',action='store_true');a=p.parse_args()
    arrays=inspect(ROOT/'Assets')
    if a.compare:
        other=inspect(a.compare);assert len(arrays)==len(other)
        for x,y in zip(arrays,other):assert x.shape==y.shape and np.allclose(x,y,rtol=0,atol=2e-4),'Saved-source re-export mismatch'
    manifest=ROOT/'Assets/Scenes/PathTracing/Manifest.json'
    files=[p for folder in [ROOT/'Assets/Models/PathTracing',ROOT/'Assets/Scenes/PathTracing',HERE] for p in folder.rglob('*') if p.is_file() and '.generated' not in p.parts and '__pycache__' not in p.parts and p.suffix not in ('.blend1','.blend2') and p!=manifest]
    hashes={str(p.relative_to(ROOT)).replace('\\','/'):{'bytes':p.stat().st_size,'sha256':digest(p)} for p in sorted(files)}
    sources=['SourceArt/Blender/ShaderBall/ShaderBall.blend','Assets/Models/ShaderBall/ShaderBall_PBR.gltf']
    value={'profile':'pathtracing-assets-1','license':'MIT','author':'masafumi','blender':'5.2.1 LTS','files':hashes,'reusedSources':{x:digest(ROOT/x) for x in sources}}
    if a.manifest:manifest.write_text(json.dumps(value,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    else:assert json.loads(manifest.read_text())==value,'Manifest hashes differ; regenerate deliberately after asset edits'
    print('PathTracing asset profile, IDs, indices, normals/tangents, references and hashes PASS')
if __name__=='__main__':main()
