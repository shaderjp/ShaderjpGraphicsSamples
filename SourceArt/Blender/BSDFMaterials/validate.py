"""Validate the deliberately restricted thick dielectric asset profile. MIT, masafumi 2026."""
from pathlib import Path
import argparse,hashlib,json
import numpy as np
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
def digest(p):return hashlib.sha256(p.read_bytes()).hexdigest()
def inspect(assets):
    folder=assets/'Models/BSDFMaterials';data=json.loads((folder/'BSDFMaterials.gltf').read_text(encoding='utf-8'))
    assert data['asset']['version']=='2.0' and len(data['meshes'])==6 and len(data['materials'])==4
    assert [s['name'] for s in data['scenes']]==['Lab','Thickness']
    assert set(data['extensionsUsed'])=={'KHR_materials_transmission','KHR_materials_ior','KHR_materials_volume'}
    assert not any(k in data for k in ('skins','animations'))
    buffers=[(folder/b['uri']).read_bytes() for b in data['buffers']]
    def accessor(i):
        a=data['accessors'][i];v=data['bufferViews'][a['bufferView']]
        dtype={5126:'<f4',5123:'<u2',5125:'<u4'}[a['componentType']];n={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        return np.ndarray((a['count'],n),dtype=dtype,buffer=buffers[v['buffer']],offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',n*np.dtype(dtype).itemsize),np.dtype(dtype).itemsize)).copy()
    ids=[x['extras']['id'] for k in ('scenes','nodes','meshes','materials') for x in data[k]];arrays=[]
    for mesh in data['meshes']:
        for p in mesh['primitives']:
            ids.append(p['extras']['id']);assert p.get('mode',4)==4
            a={k:accessor(p['attributes'][k]) for k in ('POSITION','NORMAL','TEXCOORD_0','TANGENT')}
            assert len({len(v) for v in a.values()})==1
            for v in a.values():assert np.isfinite(v).all()
            assert np.allclose(np.linalg.norm(a['NORMAL'],axis=1),1,atol=.002)
            assert np.allclose(np.linalg.norm(a['TANGENT'][:,:3],axis=1),1,atol=.002)
            assert np.max(np.abs(np.sum(a['NORMAL']*a['TANGENT'][:,:3],axis=1)))<.002,mesh['name']
            indices=accessor(p['indices']).reshape(-1,3);assert indices.max()<len(a['POSITION'])
            tri=a['POSITION'][indices].astype(np.float64)
            assert (np.linalg.norm(np.cross(tri[:,1]-tri[:,0],tri[:,2]-tri[:,0]),axis=1)>1e-12).all()
            m=data['materials'][p['material']]
            if 'KHR_materials_transmission' in m.get('extensions',{}):
                # Weld position seams for topology, retaining authored shading normals/UVs.
                _,weld=np.unique(np.round(a['POSITION'],5),axis=0,return_inverse=True)
                edges={}
                for face in weld[indices]:
                    for x,y in zip(face,np.roll(face,-1)):
                        key=(min(int(x),int(y)),max(int(x),int(y)));count,orientation=edges.get(key,(0,0))
                        edges[key]=(count+1,orientation+(1 if x<y else -1))
                assert all(v==(2,0) for v in edges.values()),('volume must be closed and consistently oriented',mesh['name'])
                signed=np.einsum('ij,ij->i',tri[:,0],np.cross(tri[:,1],tri[:,2])).sum()/6
                assert signed>0,('outward winding',mesh['name'])
            arrays.extend([*a.values(),indices])
    assert len(ids)==len(set(ids)),'stable IDs must be unique'
    for m in data['materials']:
        assert m['alphaMode']=='OPAQUE' and m['pbrMetallicRoughness']['metallicFactor']==0
        if 'extensions' in m:
            e=m['extensions'];assert e['KHR_materials_transmission']['transmissionFactor']==1 and e['KHR_materials_ior']['ior']==1.5
            assert e['KHR_materials_volume']['thicknessFactor']>0
    for im in data['images']:assert (folder/im['uri']).is_file()
    config=json.loads((assets/'Scenes/BSDFMaterials/BSDFMaterials.json').read_text(encoding='utf-8'))
    assert config['profile']=='bsdf-scene-1' and set(config['cameras'])=={'Lab','LabClose','InsideClear','Thickness','ThicknessOblique'}
    return arrays
def main():
    p=argparse.ArgumentParser();p.add_argument('--manifest',action='store_true');p.add_argument('--compare',type=Path);a=p.parse_args()
    arrays=inspect(ROOT/'Assets')
    if a.compare:
        other=inspect(a.compare);assert len(arrays)==len(other)
        for x,y in zip(arrays,other):assert x.shape==y.shape and np.allclose(x,y,rtol=0,atol=2e-4),'isolated re-export differs'
        # Check node transforms, material constants and camera/preset settings too.
        for relative in ['Models/BSDFMaterials/BSDFMaterials.gltf','Scenes/BSDFMaterials/BSDFMaterials.json']:
            assert json.loads((ROOT/'Assets'/relative).read_text(encoding='utf-8'))==json.loads((a.compare/relative).read_text(encoding='utf-8')),('isolated JSON differs',relative)
        for original in (ROOT/'Assets/Models/BSDFMaterials/Textures').glob('*.png'):
            assert original.read_bytes()==(a.compare/'Models/BSDFMaterials/Textures'/original.name).read_bytes(),('isolated texture differs',original.name)
    manifest=ROOT/'Assets/Scenes/BSDFMaterials/Manifest.json'
    files=[p for d in [ROOT/'Assets/Models/BSDFMaterials',ROOT/'Assets/Scenes/BSDFMaterials',HERE] for p in d.rglob('*') if p.is_file() and p!=manifest and '.generated' not in p.parts and '__pycache__' not in p.parts and p.suffix not in ('.blend1','.blend2')]
    sources=['SourceArt/Blender/PathTracing/PathTracing.blend','Assets/Models/PathTracing/PathTracing.gltf','Assets/Models/PathTracing/PathTracing.bin','Assets/Scenes/PathTracing/PathTracing.json']
    value={'profile':'bsdf-materials-assets-1','license':'MIT','author':'masafumi','blender':'5.2.1 LTS','files':{p.relative_to(ROOT).as_posix():{'bytes':p.stat().st_size,'sha256':digest(p)} for p in sorted(files)},'reusedSources':{x:digest(ROOT/x) for x in sources}}
    if a.manifest:manifest.write_text(json.dumps(value,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    else:assert json.loads(manifest.read_text(encoding='utf-8'))==value,'manifest differs; regenerate after deliberate source edits'
    print('BSDF assets: closed outward volumes, IDs, geometry, tangents, references and hashes PASS')
if __name__=='__main__':main()
