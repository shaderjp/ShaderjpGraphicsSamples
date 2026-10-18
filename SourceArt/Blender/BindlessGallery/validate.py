"""Independent glTF, PNG, sharing and preset validation. No bpy dependency."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import zlib
import numpy as np

ROOT=Path(__file__).resolve().parents[3]
def check(ok,message):
    if not ok: raise ValueError(message)
def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()
def png(path):
    data=path.read_bytes(); check(data[:8]==b'\x89PNG\r\n\x1a\n','PNG signature')
    offset=8; compressed=bytearray(); width=height=0
    while offset<len(data):
        count=struct.unpack_from('>I',data,offset)[0]; kind=data[offset+4:offset+8]; payload=data[offset+8:offset+8+count]
        check(zlib.crc32(kind+payload)==struct.unpack_from('>I',data,offset+8+count)[0],'PNG CRC')
        if kind==b'IHDR':
            width,height,depth,color,*_=struct.unpack('>IIBBBBB',payload)
            check(depth==8 and color==6,'RGBA8 PNG')
        if kind==b'IDAT': compressed.extend(payload)
        offset+=count+12
    raw=np.frombuffer(zlib.decompress(compressed),np.uint8).reshape(height,1+width*4)
    check(np.all(raw[:,0]==0),'Canonical unfiltered PNG')
    return raw[:,1:].reshape(height,width,4)

def validate(assets):
    directory=assets/'Models'/'BindlessGallery'
    d=json.loads((directory/'BindlessGallery.gltf').read_text(encoding='utf-8'))
    payload=(directory/d['buffers'][0]['uri']).read_bytes()
    def read(index):
        a=d['accessors'][index]; view=d['bufferViews'][a['bufferView']]
        dtype='<u4' if a['componentType']==5125 else '<f4'
        return np.frombuffer(payload,dtype,count=a['count']*{'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']],
            offset=view.get('byteOffset',0)).reshape(a['count'],-1)
    ids=[obj['extras']['id'] for key in ('materials','meshes','nodes','scenes') for obj in d[key]]
    ids += [p['extras']['id'] for m in d['meshes'] for p in m['primitives']]
    check(len(ids)==len(set(ids)),'Duplicate stable ID')
    check(len(d['materials'])==256 and len(d['images'])==400,'Material/image counts')
    unique={}
    for m in d['meshes']:
        g=m['extras']['geometry']; p=m['primitives'][0]; identity=(tuple(p['attributes'].items()),p['indices'])
        check(g not in unique or unique[g]==identity,'Material variants duplicated geometry payload')
        unique[g]=identity
    check(len(unique)==64,'64 unique geometries')
    triangles=0
    for attrs,index in unique.values():
        attrs=dict(attrs); pos=read(attrs['POSITION']); n=read(attrs['NORMAL']); t=read(attrs['TANGENT']); uv=read(attrs['TEXCOORD_0']); ind=read(index).ravel()
        check(all(np.isfinite(a).all() for a in (pos,n,t,uv)),'Non-finite geometry')
        check(ind.size%3==0 and ind.max()<len(pos),'Index range')
        check(np.max(abs(np.linalg.norm(n,axis=1)-1))<.002,'Unit normals')
        check(np.max(abs(np.linalg.norm(t[:,:3],axis=1)-1))<.002 and np.max(abs((n*t[:,:3]).sum(axis=1)))<.002,'Orthonormal tangents')
        check(np.all(abs(t[:,3])==1),'Tangent sign')
        tri=ind.reshape(-1,3)
        check(np.all(t[tri,3]==t[tri[:,0],3,None]),'Triangle tangent sign')
        cross=np.cross(pos[tri[:,1]]-pos[tri[:,0]],pos[tri[:,2]]-pos[tri[:,0]])
        check(np.all(np.linalg.norm(cross,axis=1)>1e-10),'Degenerate triangle')
        triangles+=len(tri)
    scenes={}
    for scene,count,budget in zip(d['scenes'],(128,512,4096),(16,64,256)):
        nodes=[d['nodes'][i] for i in scene['nodes']]
        check(all(not n.get('children') and all(x>0 for x in n['scale']) and max(n['scale'])-min(n['scale'])<1e-6 for n in nodes),'Positive uniform scale/flat hierarchy')
        objects=[n for n in nodes if 'mesh' in n]
        placements=np.array([n['translation'] for n in objects])
        check(np.ptp(placements[:,0])>8 and np.ptp(placements[:,1])>1,'Scene placements collapsed or unevaluated')
        mats={d['meshes'][n['mesh']]['primitives'][0]['material'] for n in objects}
        check(len(objects)==count and len(mats)==budget,'Preset object/material count')
        scenes[scene['name']]={'objects':len(objects),'materials':len(mats)}
    image_hashes=set()
    for info in d['images']:
        path=directory/info['uri']; pixels=png(path)
        check(pixels.shape==(512,512,4),'512x512 images')
        digest=sha(path); check(digest not in image_hashes,'Duplicate image content'); image_hashes.add(digest)
        if path.stem.startswith('Normal'):
            n=pixels[...,:3].astype(float)/255*2-1
            check(np.max(abs(np.linalg.norm(n,axis=2)-1))<.012,'Normal image length')
        if path.stem.startswith('ORM'): check(np.all(pixels[...,0]==255),'ORM AO=1')
    for m in d['materials']:
        check(m['alphaMode'] in ('OPAQUE','MASK'),'Alpha modes')
        if m['alphaMode']=='MASK':
            check(m['alphaCutoff']==.5 and m['doubleSided'],'MASK policy')
            index=m['pbrMetallicRoughness']['baseColorTexture']['index']
            a=png(directory/d['images'][d['textures'][index]['source']]['uri'])[...,3]
            check(set(np.unique(a))=={0,255},'MASK coverage')
    path=json.loads((assets/'Scenes'/'BindlessGallery'/'CameraPath.json').read_text())
    check(len(path['frames'])==1201 and path['durationSeconds']==20 and path['fps']==60,'Camera path count')
    for f in path['frames']:
        check(np.isfinite(f['position']+f['target']).all() and np.linalg.norm(np.array(f['position'])-f['target'])>.01,'Camera path validity')
    return {'geometries':64,'uniqueTriangles':triangles,'materials':256,'images':400,'scenes':scenes,'pathFrames':1201,
        'modelSha256':sha(directory/'BindlessGallery.gltf'),'binarySha256':sha(directory/'BindlessGallery.bin')}

if __name__=='__main__':
    p=argparse.ArgumentParser(); p.add_argument('--assets',type=Path,default=ROOT/'Assets'); p.add_argument('--manifest',action='store_true')
    args=p.parse_args(); report=validate(args.assets.resolve())
    if args.manifest:
        paths=list((ROOT/'SourceArt'/'Blender'/'BindlessGallery').glob('*.blend'))+list((args.assets/'Models'/'BindlessGallery').rglob('*'))+list((args.assets/'Scenes'/'BindlessGallery').glob('*'))
        report['files']={str(path.relative_to(ROOT)).replace('\\','/'):sha(path) for path in sorted(paths) if path.is_file()}
        out=ROOT/'Docs'/'Assets'/'BindlessGalleryManifest.json'; out.parent.mkdir(parents=True,exist_ok=True)
        out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report if not args.manifest else {k:v for k,v in report.items() if k!='files'},indent=2))
