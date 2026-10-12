"""CPU asset checks, independent of the future runtime loader. MIT License."""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import sys
import zlib
import numpy as np
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'LightingLab'))
from ibl import read_dds, directions, integrate

def check(condition,message):
    if not condition: raise ValueError(message)

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')

def png(path):
    data=path.read_bytes(); check(data[:8]==b'\x89PNG\r\n\x1a\n','Invalid PNG '+str(path))
    offset,packed=8,b''
    while offset<len(data):
        count=struct.unpack_from('>I',data,offset)[0]; kind=data[offset+4:offset+8]
        payload=data[offset+8:offset+8+count]
        check(zlib.crc32(kind+payload)&0xffffffff==struct.unpack_from('>I',data,offset+8+count)[0],'PNG CRC')
        if kind==b'IHDR':
            w,h,depth,color,compression,filtering,interlace=struct.unpack('>2I5B',payload)
            check(depth==8 and color==6 and interlace==0,'Expected non-interlaced RGBA8 PNG')
        elif kind==b'IDAT': packed+=payload
        offset+=count+12
    raw=zlib.decompress(packed); stride=w*4
    check(len(raw)==h*(stride+1),'PNG byte count')
    rows=np.empty((h,stride),np.uint8); previous=np.zeros(stride,np.uint8)
    for y in range(h):
        f=raw[y*(stride+1)]; row=np.frombuffer(raw,dtype=np.uint8,count=stride,offset=y*(stride+1)+1).copy()
        if f:
            for x in range(stride):
                a=int(row[x-4]) if x>=4 else 0; b=int(previous[x]); c=int(previous[x-4]) if x>=4 else 0
                if f==1: value=a
                elif f==2: value=b
                elif f==3: value=(a+b)//2
                elif f==4:
                    p=a+b-c; pa,pb,pc=abs(p-a),abs(p-b),abs(p-c)
                    value=a if pa<=pb and pa<=pc else b if pb<=pc else c
                else: raise ValueError('PNG filter')
                row[x]=(int(row[x])+value)&255
        rows[y]=row; previous=row
    return rows.reshape(h,w,4)

def accessor(data,buffers,index):
    a=data['accessors'][index]; view=data['bufferViews'][a['bufferView']]
    check('sparse' not in a and not a.get('normalized',False),'Unsupported sparse/normalized accessor')
    dtype={5126:'<f4',5123:'<u2',5125:'<u4'}[a['componentType']]
    components={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
    width=np.dtype(dtype).itemsize*components; stride=view.get('byteStride',width)
    offset=view.get('byteOffset',0)+a.get('byteOffset',0)
    check(a['count']>0 and stride>=width,'Accessor count/stride')
    end=a.get('byteOffset',0)+(a['count']-1)*stride+width
    check(end<=view['byteLength'],'Accessor exceeds buffer view')
    check(view.get('byteOffset',0)+view['byteLength']<=len(buffers[view['buffer']]),'Buffer view exceeds buffer')
    result=np.ndarray((a['count'],components),dtype=dtype,buffer=buffers[view['buffer']],offset=offset,strides=(stride,np.dtype(dtype).itemsize)).copy()
    check(np.isfinite(result).all(),'Non-finite accessor')
    return result

def matrix(node):
    if 'matrix' in node:
        check(not any(k in node for k in ('translation','rotation','scale')),'Matrix and TRS coexist')
        result=np.array(node['matrix'],float).reshape(4,4).T
    else:
        x,y,z,w=node.get('rotation',[0,0,0,1]); check(abs(x*x+y*y+z*z+w*w-1)<1e-5,'Quaternion length')
        r=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
            [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
            [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
        scale=np.array(node.get('scale',[1,1,1]),float)
        check(np.all(scale>0) and np.ptp(scale)<1e-5,'Positive uniform scale required')
        result=np.eye(4); result[:3,:3]=r@np.diag(scale); result[:3,3]=node.get('translation',[0,0,0])
    check(np.isfinite(result).all() and np.linalg.det(result[:3,:3])>0,'Invalid transform')
    singular=np.linalg.svd(result[:3,:3],compute_uv=False)
    check(np.ptp(singular)<1e-5,'Shear/nonuniform transform')
    return result

def model_report(assets):
    directory=assets/'Models'/'Workshop'; data=json.loads((directory/'Workshop.gltf').read_text(encoding='utf-8'))
    check(set(data.get('extensionsUsed',[]))=={'KHR_lights_punctual','KHR_materials_emissive_strength'},'Extension profile')
    check(not any(data.get(k) for k in ('animations','skins')),'Animation/skin unsupported')
    check([s['name'] for s in data['scenes']]==['Standard','Stress'],'Two ordered scenes required')
    check(data['scenes'][data['scene']]['name']=='Standard','Default scene')
    buffers=[]
    for entry in data['buffers']:
        path=directory/entry['uri']; check(path.resolve().is_relative_to(directory.resolve()),'External buffer URI')
        payload=path.read_bytes(); check(len(payload)==entry['byteLength'],'Buffer byte count'); buffers.append(payload)
    ids=[]
    for key in ('nodes','meshes','materials','scenes'):
        for item in data[key]: ids.append(item['extras']['id'])
    check(len(ids)==len(set(ids)),'Duplicate stable ID')
    images={}; texture_bytes=0
    for i,image in enumerate(data['images']):
        path=directory/image['uri']; check(path.resolve().is_relative_to(directory.resolve()),'External image URI')
        pixels=png(path); images[i]=pixels; h,w=pixels.shape[:2]
        check(max(h,w)<=2048 and (max(h,w)<=1024 or 'metal_' in path.name),'Texture size profile')
        while True:
            texture_bytes+=w*h*4
            if w==1 and h==1: break
            w,h=max(1,w//2),max(1,h//2)
    check(texture_bytes<=256*1024*1024,'Unique full-mip RGBA8 texture budget')
    def tex(info):
        check(info.get('texCoord',0)==0,'Only UV0'); return images[data['textures'][info['index']]['source']]
    alpha_counts={'OPAQUE':0,'MASK':0,'BLEND':0}
    for mat in data['materials']:
        p=mat['pbrMetallicRoughness']; mode=mat.get('alphaMode','OPAQUE'); alpha_counts[mode]+=1
        albedo=tex(p['baseColorTexture']); normal=tex(mat['normalTexture']); orm=tex(p['metallicRoughnessTexture']); tex(mat['emissiveTexture'])
        check(mat['occlusionTexture']['index']==p['metallicRoughnessTexture']['index'],'Packed ORM sharing')
        check(np.all(orm[...,0]==255) and np.all(orm[...,3]==255),'Unbaked macro AO / ORM alpha')
        normals=normal[...,:3].astype(float)/255*2-1
        check(np.max(abs(np.linalg.norm(normals,axis=-1)-1))<.015,'Normal texture length')
        check(all(0<=x<=1 for x in p['baseColorFactor']) and 0<=p['metallicFactor']<=1 and 0<=p['roughnessFactor']<=1,'Material factors')
        if mode=='MASK':
            check(mat['alphaCutoff']==.5 and mat['doubleSided'],'MASK policy')
            check(set(np.unique(albedo[...,3]))=={0,255},'MASK must have both holes and opaque pixels')
        if mode=='BLEND': check(0<p['baseColorFactor'][3]<1 and not mat['doubleSided'],'BLEND policy')
    check(12<=len(data['materials'])<=16 and alpha_counts['MASK']==1 and alpha_counts['BLEND']==1,'Material count/profile')
    light_definitions=data['extensions']['KHR_lights_punctual']['lights']
    conversion=json.loads((assets/'Scenes'/'Workshop'/'Workshop.json').read_text(encoding='utf-8'))['lightConversion']
    check(conversion['mode']=='SPEC' and conversion['wattsToLumens']==683,'Light conversion profile')
    for light in light_definitions:
        source=conversion['sources'][light['name']]
        check(math.isfinite(source['energy']) and source['energy']>=0,'Light source energy')
        expected=source['energy']*683/(4*math.pi if light['type']=='point' else 1)
        check(math.isclose(light['intensity'],expected,rel_tol=1e-6),'Light SPEC unit conversion')
    meshes=[]; large=[]; primitive_ids=[]; all_degenerate=0
    for mesh in data['meshes']:
        count=0; minimum=np.full(3,np.inf); maximum=-minimum
        chunks=[]; triangles=[]; base=0
        for p in mesh['primitives']:
            primitive_ids.append(p['extras']['id']); check(p.get('mode',4)==4 and 'targets' not in p,'Static triangles')
            check(set(p['attributes'])=={'POSITION','NORMAL','TANGENT','TEXCOORD_0'},'Vertex attribute profile')
            pos=accessor(data,buffers,p['attributes']['POSITION']); n=accessor(data,buffers,p['attributes']['NORMAL'])
            t=accessor(data,buffers,p['attributes']['TANGENT']); uv=accessor(data,buffers,p['attributes']['TEXCOORD_0'])
            idx=accessor(data,buffers,p['indices']).ravel().astype(np.int64)
            check(len(idx)%3==0 and idx.min()>=0 and idx.max()<len(pos),'Index range')
            check(len(pos)==len(n)==len(t)==len(uv),'Vertex stream count')
            check(np.max(abs(np.linalg.norm(n,axis=1)-1))<1e-3,'Unit vertex normal')
            check(np.max(abs(np.linalg.norm(t[:,:3],axis=1)-1))<1e-3,'Unit tangent')
            check(np.max(abs((n*t[:,:3]).sum(axis=1)))<1e-3 and np.all(np.abs(t[:,3])==1),'Tangent orthogonality/sign')
            tri=idx.reshape(-1,3); edge1=pos[tri[:,1]]-pos[tri[:,0]]; edge2=pos[tri[:,2]]-pos[tri[:,0]]
            check(np.all(t[tri,3]==t[tri[:,0],3,None]),'Tangent sign must be constant within a triangle')
            degenerate=int(np.count_nonzero(np.linalg.norm(np.cross(edge1,edge2),axis=1)<1e-12)); all_degenerate+=degenerate
            check(degenerate==0,'Degenerate triangles '+mesh['name'])
            a=data['accessors'][p['attributes']['POSITION']]
            check(np.allclose(a['min'],pos.min(axis=0),atol=1e-6) and np.allclose(a['max'],pos.max(axis=0),atol=1e-6),'Accessor bounds')
            minimum=np.minimum(minimum,pos.min(axis=0)); maximum=np.maximum(maximum,pos.max(axis=0))
            chunks.append(pos); triangles.append(tri+base); base+=len(pos); count+=len(tri)
        # Weld UV/normal splits for topological checks, across material boundaries.
        _,weld=np.unique(np.concatenate(chunks),axis=0,return_inverse=True)
        tri=weld[np.concatenate(triangles)]; edges=np.concatenate((tri[:,[0,1]],tri[:,[1,2]],tri[:,[2,0]]))
        edges.sort(axis=1); _,multiplicity=np.unique(edges,axis=0,return_counts=True)
        check(np.all(multiplicity==2),'Mesh is not a closed 2-manifold '+mesh['name'])
        meshes.append({'id':mesh['extras']['id'],'triangles':count,'primitives':len(mesh['primitives']),
            'minimum':minimum.tolist(),'maximum':maximum.tolist()})
        if count>=10000: large.append({'id':mesh['extras']['id'],'triangles':count})
    check(len(set(primitive_ids))==len(primitive_ids),'Duplicate Primitive ID')
    check(40<=len(primitive_ids)<=80 and len(large)>=3,'Primitive density profile')
    scene_reports={}; node_worlds={}
    for s in data['scenes']:
        visited=set(); active=set(); used_meshes=set(); used_materials=set(); count=0; instances=0; points=0; suns=0
        minimum=np.full(3,np.inf); maximum=-minimum
        presets={name:{'triangles':0,'meshInstances':0,'primitiveInstances':0} for name in ('Opaque','OpaqueMask','Full')}
        def visit(index,parent):
            nonlocal count,instances,points,suns,minimum,maximum
            check(0<=index<len(data['nodes']) and index not in active and index not in visited,'Cycle/multiple parent/node range')
            visited.add(index); active.add(index); node=data['nodes'][index]; world=parent@matrix(node)
            node_worlds[(s['name'],node['extras']['id'])]=world
            if 'mesh' in node:
                mi=node['mesh']; check(0<=mi<len(meshes),'Mesh reference'); m=meshes[mi]
                count+=m['triangles']; instances+=1; used_meshes.add(mi)
                for p in data['meshes'][mi]['primitives']: used_materials.add(p['material'])
                for preset,allowed in (('Opaque',('OPAQUE',)),('OpaqueMask',('OPAQUE','MASK')),('Full',('OPAQUE','MASK','BLEND'))):
                    selected=[p for p in data['meshes'][mi]['primitives'] if data['materials'][p['material']]['alphaMode'] in allowed]
                    presets[preset]['triangles']+=sum(data['accessors'][p['indices']]['count']//3 for p in selected)
                    presets[preset]['primitiveInstances']+=len(selected)
                    presets[preset]['meshInstances']+=bool(selected)
                lo,hi=np.array(m['minimum']),np.array(m['maximum'])
                corners=np.array([[x,y,z,1] for x in (lo[0],hi[0]) for y in (lo[1],hi[1]) for z in (lo[2],hi[2])])
                transformed=(world@corners.T).T[:,:3]
                minimum=np.minimum(minimum,transformed.min(axis=0)); maximum=np.maximum(maximum,transformed.max(axis=0))
            light=node.get('extensions',{}).get('KHR_lights_punctual')
            if light:
                definition=data['extensions']['KHR_lights_punctual']['lights'][light['light']]
                if definition['type']=='point': points+=1
                elif definition['type']=='directional': suns+=1
                else: raise ValueError('No spot lights in this profile')
            for child in node.get('children',[]): visit(child,world)
            active.remove(index)
        for root in s['nodes']: visit(root,np.eye(4))
        scene_reports[s['name']]={'placedTriangles':count,'meshInstances':instances,'nodes':len(visited),
            'pointLights':points,'directionalLights':suns,'minimum':minimum.tolist(),'maximum':maximum.tolist(),
            'usedMeshes':sorted(used_meshes),'usedMaterials':sorted(used_materials),'presets':presets}
    a,b=scene_reports['Standard'],scene_reports['Stress']
    check(200000<=a['placedTriangles']<=300000 and b['placedTriangles']==a['placedTriangles']*9,'Placed triangle budget / exact nine bays')
    check(b['meshInstances']==a['meshInstances']*9 and a['usedMeshes']==b['usedMeshes'] and a['usedMaterials']==b['usedMaterials'],'Shared asset/instance structure')
    check(a['pointLights']==8 and b['pointLights']==72 and a['directionalLights']==b['directionalLights']==1,'Light counts')
    config=json.loads((assets/'Scenes'/'Workshop'/'Workshop.json').read_text(encoding='utf-8'))
    check(config['profile']=='workshop-scene-1' and config['defaultPreset']=='OpaqueMask','Scene config profile')
    check(config['presets']=={'Opaque':['OPAQUE'],'OpaqueMask':['OPAQUE','MASK'],'Full':['OPAQUE','MASK','BLEND']},'Preset material membership')
    by_id={n['extras']['id']:n for n in data['nodes']}
    for name,camera in config['cameras'].items():
        scene_name='Stress' if name=='StressHero' else 'Standard'; node=by_id[camera['nodeId']]
        world=node_worlds[(scene_name,camera['nodeId'])]
        check(np.allclose(world[:3,3],camera['position'],atol=1e-5),'Camera position '+name)
        forward=-world[:3,2]; target=np.array(camera['target'])-world[:3,3]; target/=np.linalg.norm(target)
        check(np.dot(forward,target)>1-1e-6,'Camera orientation '+name)
        definition=data['cameras'][node['camera']]['perspective']
        check(abs(math.degrees(definition['yfov'])-camera['fovDegrees'])<1e-4,'Vertical FOV '+name)
    path=config['cameraPath']; frames=[k['frame'] for k in path['keyframes']]
    check(path['fps']==60 and path['durationSeconds']==20 and path['samples']==1201 and frames==sorted(set(frames)) and frames[0]==0 and frames[-1]==1200,'Fixed camera path')
    # Expand the documented interpolation to verify every snapshot is finite/nondegenerate.
    samples=[]
    for frame in range(1201):
        i=min(np.searchsorted(frames,frame,side='right')-1,len(frames)-2)
        left,right=path['keyframes'][i:i+2]; t=(frame-left['frame'])/(right['frame']-left['frame'])
        pos=(1-t)*np.array(left['position'])+t*np.array(right['position']); target=(1-t)*np.array(left['target'])+t*np.array(right['target'])
        direction=target-pos
        check(np.isfinite(pos).all() and np.linalg.norm(direction)>1e-3 and np.linalg.norm(np.cross(direction,[0,1,0]))>1e-3,'Degenerate path camera')
        samples.append({'frame':frame,'position':pos.tolist(),'target':target.tolist()})
    # LH conversion is a reflection: conjugate transforms, flip triangle winding/tangent W once.
    reflect=np.diag([1,1,-1,1])
    for world in node_worlds.values(): check(np.allclose(reflect@(reflect@world@reflect)@reflect,world),'RH/LH roundtrip')
    return {'scenes':scene_reports,'uniqueTriangles':sum(m['triangles'] for m in meshes),
        'uniqueMeshes':len(meshes),'uniquePrimitives':len(primitive_ids),'materials':len(data['materials']),
        'images':len(images),'rgba8FullMipBytes':texture_bytes,'alphaMaterials':alpha_counts,
        'largeMeshes':large,'degenerateTriangles':all_degenerate,'meshes':meshes},samples

def environment_checks(assets):
    directory=assets/'Environments'/'Workshop'; values={}
    for name,size,mips,faces,channels in [('WorkshopSky_Environment',512,1,6,4),('WorkshopSky_Irradiance',32,1,6,4),
        ('WorkshopSky_Specular',256,9,6,4),('BRDF',256,1,1,2)]:
        levels=read_dds(directory/(name+'.dds')); check(len(levels)==faces,'DDS faces')
        for face in levels:
            check(len(face)==mips and face[0].shape==(size,size,channels),'DDS dimensions/mips')
            for pixels in face: check(np.isfinite(pixels).all() and pixels.min()>=0,'DDS finite nonnegative')
        values[name]=levels
    check(max(f[0][...,:3].max() for f in values['WorkshopSky_Environment'])>1,'HDR preserved')
    # Continuous sky: corresponding nearest edge texels differ only by subtexel projection.
    env=values['WorkshopSky_Environment']; side=(0,1,4,5)
    check(max(np.max(abs(env[i][0]-env[0][0])) for i in side)<.01,'Azimuth orientation / sky seam')
    zenith,nadir=env[2][0][256,256,:3],env[3][0][256,256,:3]
    check(zenith.min()>1 and nadir.max()<1 and zenith[2]>zenith[0],'Sky +Y zenith / -Y nadir orientation')
    colors=np.ones((8,16,4),float); colors[...,:3]=(.13,.37,1.7)
    n=np.concatenate([directions(4,i).reshape(-1,3) for i in range(6)])
    diffuse=integrate(colors,n,samples=1024)
    check(np.max(abs(diffuse-np.array([.13,.37,1.7])*math.pi))<1e-8,'Constant irradiance pi*C')
    for mip in range(1,9):
        spec=integrate(colors,n,mip/8,samples=1024)
        check(np.max(abs(spec-[.13,.37,1.7]))<1e-8,'Constant GGX mip '+str(mip))
    return {'constantIrradianceError':float(np.max(abs(diffuse-np.array([.13,.37,1.7])*math.pi))),
        'samples':1024,'faces':['+X','-X','+Y','-Y','+Z','-Z'],'cubeCoordinates':'left-handed Y-up',
        'irradiance':'E; shader multiplies Lambert 1/pi','ggx':'alpha=max(roughness,0.045)^2; correlated Smith; Schlick'}

def main():
    p=argparse.ArgumentParser(); p.add_argument('--assets',type=Path,default=ROOT/'Assets')
    p.add_argument('--compare',type=Path); p.add_argument('--manifest',action='store_true')
    p.add_argument('--skip-environment',action='store_true'); args=p.parse_args()
    assets=args.assets.resolve(); report,samples=model_report(assets)
    if not args.skip_environment: report['ibl']=environment_checks(assets)
    if args.compare:
        expected={f.relative_to(assets):sha(f) for tree in ('Models/Workshop','Scenes/Workshop') for f in (assets/tree).rglob('*') if f.is_file() and f.name not in ('Manifest.json','Statistics.json','CameraPath.json')}
        actual={f.relative_to(args.compare):sha(f) for tree in ('Models/Workshop','Scenes/Workshop') for f in (args.compare/tree).rglob('*') if f.is_file()}
        check(expected==actual,'Saved-source re-export file inventory or bytes differ')
    scenes=assets/'Scenes'/'Workshop'
    if args.manifest:
        write(scenes/'Statistics.json',report); write(scenes/'CameraPath.json',{'profile':'workshop-camera-path-1','fps':60,'frames':samples})
        paths=[f for tree in ('Models/Workshop','Environments/Workshop','Scenes/Workshop') for f in (assets/tree).rglob('*') if f.is_file() and f.name!='Manifest.json']
        paths += [f for f in HERE.iterdir() if f.suffix in ('.blend','.exr','.py','.md')]
        paths += [HERE.parent/'LightingLab'/'ibl.py',HERE.parent/'ShaderBall'/'shader_ball_tools.py']
        paths += [f for f in (ROOT/'Docs'/'Assets'/'Images'/'Workshop').glob('*') if f.is_file()]
        write(scenes/'Manifest.json',{'profile':'workshop-manifest-1','license':'MIT; Copyright (c) 2026 masafumi',
            'units':'meters','geometryCoordinates':'right-handed Y-up','geometry':'indexed triangles; static; no LOD',
            'textureColorSpaces':{'Albedo':'sRGB; alpha linear','Emissive':'sRGB','Normal':'linear','ORM':'linear; R=AO G=roughness B=metallic'},
            'tools':{'Blender':'5.2.1 LTS','buildHash':'9e2066aef7ef','exporter':'5.2.40','validator':'2.0.0-dev.3.10','Python':sys.version.split()[0],'NumPy':np.__version__},
            'environmentGeneration':{'width':2048,'height':1024,'format':'OpenEXR float32 scene-linear RGB','device':'Cycles CPU','samples':256,'seed':0,'denoise':False},
            'iblLayout':{'environment':{'size':512,'faces':6,'mips':1,'format':'RGBA16F'},'irradiance':{'size':32,'faces':6,'mips':1,'format':'RGBA16F'},
                'specular':{'size':256,'faces':6,'mips':9,'format':'RGBA16F'},'brdf':{'size':256,'faces':1,'mips':1,'format':'RG16F'},'samples':1024},
            'sharedTools':['SourceArt/Blender/LightingLab/ibl.py','SourceArt/Blender/ShaderBall/shader_ball_tools.py'],
            'files':[{'path':f.relative_to(ROOT).as_posix(),'bytes':f.stat().st_size,'sha256':sha(f)} for f in sorted(paths)]})
    else:
        manifest=json.loads((scenes/'Manifest.json').read_text(encoding='utf-8'))
        for item in manifest['files']:
            f=ROOT/item['path']; check(f.is_file() and f.stat().st_size==item['bytes'] and sha(f)==item['sha256'],'Manifest mismatch '+item['path'])
    print(json.dumps({k:v for k,v in report.items() if k!='meshes'},ensure_ascii=False,indent=2))
    print('Workshop geometry/material/sharing/bounds/camera/IBL/hash checks PASS')

if __name__=='__main__': main()
