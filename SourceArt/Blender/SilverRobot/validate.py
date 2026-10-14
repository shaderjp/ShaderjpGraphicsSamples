"""Validate real skin data; optionally compare CPU LBS with Blender's evaluated mesh.

Run using Blender's bundled Python, or inside Blender for --blender-compare.
MIT License, Copyright (c) 2026 masafumi.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import sys
import numpy as np

HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE))
DTYPES={5120:'i1',5121:'u1',5122:'<i2',5123:'<u2',5125:'<u4',5126:'<f4'}
COMPONENTS={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4,'MAT4':16}


def read(path):
    path=Path(path)
    if path.suffix=='.glb':
        data=path.read_bytes()
        magic,version,length=struct.unpack_from('<III',data)
        assert magic==0x46546c67 and version==2 and length==len(data)
        offset=12;chunks={}
        while offset<len(data):
            size,kind=struct.unpack_from('<II',data,offset);offset+=8
            assert offset+size<=len(data) and size%4==0
            chunks[kind]=data[offset:offset+size];offset+=size
        doc=json.loads(chunks[0x4e4f534a]);buffers=[chunks[0x004e4942]]
    else:
        doc=json.loads(path.read_text(encoding='utf-8'))
        buffers=[]
        for item in doc['buffers']:
            uri=item['uri'];assert not uri.startswith(('http:','https:','data:'))
            target=(path.parent/uri).resolve();assert target.parent==path.parent.resolve()
            buffers.append(target.read_bytes())
    for description,buffer in zip(doc['buffers'],buffers):assert len(buffer)>=description['byteLength']
    def accessor(index):
        a=doc['accessors'][index];assert 'sparse' not in a
        view=doc['bufferViews'][a['bufferView']];dtype=np.dtype(DTYPES[a['componentType']])
        count,n=a['count'],COMPONENTS[a['type']]
        stride=view.get('byteStride',dtype.itemsize*n)
        offset=view.get('byteOffset',0)+a.get('byteOffset',0)
        arr=np.ndarray((count,n),dtype=dtype,buffer=buffers[view['buffer']],offset=offset,strides=(stride,dtype.itemsize)).copy()
        if a.get('normalized'):
            assert a['componentType'] in (5121,5123)
            arr=arr.astype(np.float64)/np.iinfo(dtype).max
        return arr
    return doc,accessor


def trs(t,r,s):
    x,y,z,w=np.array(r,dtype=np.float64)/np.linalg.norm(r)
    m=np.eye(4)
    m[:3,:3]=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],
                      [2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],
                      [2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])@np.diag(s)
    m[:3,3]=t
    return m


def matrices(doc,accessor,animation=None,time=0):
    overrides={}
    if animation:
        for channel in animation['channels']:
            sample=animation['samplers'][channel['sampler']]
            keys=accessor(sample['input']).ravel();values=accessor(sample['output']).astype(np.float64)
            assert np.all(np.diff(keys)>0)
            index=max(0,min(len(keys)-2,np.searchsorted(keys,time,side='right')-1))
            alpha=max(0,min(1,(time-keys[index])/(keys[index+1]-keys[index]))) if len(keys)>1 else 0
            a=values[index];b=values[min(index+1,len(values)-1)]
            interpolation=sample.get('interpolation','LINEAR');assert interpolation in ('LINEAR','STEP')
            if interpolation=='STEP':v=a
            elif channel['target']['path']=='rotation':
                dot=float(np.dot(a,b))
                if dot<0:b=-b;dot=-dot
                if dot>.9995:v=a*(1-alpha)+b*alpha
                else:
                    theta=np.arccos(np.clip(dot,-1,1))
                    v=(np.sin((1-alpha)*theta)*a+np.sin(alpha*theta)*b)/np.sin(theta)
                v/=np.linalg.norm(v)
            else:v=a*(1-alpha)+b*alpha
            overrides[(channel['target']['node'],channel['target']['path'])]=v
    world={};parents={}
    for i,n in enumerate(doc['nodes']):
        for c in n.get('children',[]):
            assert c not in parents;parents[c]=i
    def calculate(index,visiting=None):
        if index in world:return world[index]
        visiting=set() if visiting is None else visiting
        assert index not in visiting;visiting.add(index)
        node=doc['nodes'][index]
        local=np.array(node['matrix']).reshape(4,4).T if 'matrix' in node else trs(
            overrides.get((index,'translation'),node.get('translation',[0,0,0])),
            overrides.get((index,'rotation'),node.get('rotation',[0,0,0,1])),
            overrides.get((index,'scale'),node.get('scale',[1,1,1])))
        world[index]=calculate(parents[index],visiting)@local if index in parents else local
        visiting.remove(index);return world[index]
    for i in range(len(doc['nodes'])):calculate(i)
    return world


def skin_positions(doc,accessor,world):
    skin=doc['skins'][0]
    inverse=accessor(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
    palette=np.array([world[n]@ibm for n,ibm in zip(skin['joints'],inverse)])
    results=[]
    for primitive in doc['meshes'][0]['primitives']:
        attributes=primitive['attributes'];p=accessor(attributes['POSITION'])
        joints=accessor(attributes['JOINTS_0']).astype(int);weights=accessor(attributes['WEIGHTS_0'])
        homogeneous=np.column_stack((p,np.ones(len(p))))
        moved=np.einsum('nvij,nj->nvi',palette[joints],homogeneous)
        results.append(np.einsum('nv,nvi->ni',weights,moved)[:,:3])
    return results


def profile(path):
    doc,a=read(path)
    assert doc['asset']['version']=='2.0'
    assert not doc.get('extensionsUsed') and not doc.get('extensionsRequired')
    assert not doc.get('cameras') and not doc.get('images') and not doc.get('textures')
    assert len(doc['skins'])==1 and len(doc['meshes'])==1 and len(doc['materials'])==5
    skin=doc['skins'][0];assert len(skin['joints'])==53 and len(set(skin['joints']))==53
    names=[doc['nodes'][n]['name'] for n in skin['joints']];assert len(set(names))==53
    assert 'Root' in names and 'Little3.L' in names and 'Thumb3.R' in names
    assert len(a(skin['inverseBindMatrices']))==53
    skinned=[i for i,n in enumerate(doc['nodes']) if n.get('skin')==0]
    assert len(skinned)==1 and doc['nodes'][skinned[0]]['mesh']==0
    assert not any(skinned[0] in n.get('children',[]) for n in doc['nodes'])
    rest=matrices(doc,a)
    inverse=a(skin['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
    bind_error=max(float(np.abs(rest[n]@m-np.eye(4)).max()) for n,m in zip(skin['joints'],inverse))
    assert bind_error<3e-6,bind_error
    count=triangles=blended=0;minv=np.full(3,np.inf);maxv=np.full(3,-np.inf)
    histogram={str(i):0 for i in range(1,5)}
    for primitive in doc['meshes'][0]['primitives']:
        assert primitive.get('mode',4)==4
        attrs=primitive['attributes'];assert set(attrs)=={'POSITION','NORMAL','JOINTS_0','WEIGHTS_0'}
        p=a(attrs['POSITION']);normal=a(attrs['NORMAL']);j=a(attrs['JOINTS_0']);w=a(attrs['WEIGHTS_0'])
        assert p.shape==normal.shape and j.shape==w.shape==(len(p),4)
        assert np.isfinite(p).all() and np.isfinite(w).all() and np.isfinite(normal).all()
        assert np.max(np.abs(np.linalg.norm(normal,axis=1)-1))<2e-5
        assert j.min()>=0 and j.max()<53 and w.min()>=0
        assert np.max(np.abs(w.sum(axis=1)-1))<2e-6
        influence=(w>1e-7).sum(axis=1);assert influence.min()>=1 and influence.max()<=4
        for i in range(1,5):histogram[str(i)]+=int((influence==i).sum())
        indices=a(primitive['indices']).ravel();assert len(indices)%3==0 and indices.max()<len(p)
        v=p[indices.reshape(-1,3)]
        area=np.linalg.norm(np.cross(v[:,1]-v[:,0],v[:,2]-v[:,0]),axis=1)
        assert np.all(area>1e-12),'Degenerate triangle'
        minv=np.minimum(minv,p.min(axis=0));maxv=np.maximum(maxv,p.max(axis=0))
        count+=len(p);triangles+=len(indices)//3;blended+=int((influence>1).sum())
    assert blended>500 and 1.88<maxv[1]<1.94 and minv[1]>=0
    actions={x['name']:x for x in doc['animations']}
    assert set(actions)=={'Bind_Apose','Retarget_Tpose','Idle','Walk_InPlace'}
    loops={}
    for name,animation in actions.items():
        duration=max(float(a(s['input'])[-1,0]) for s in animation['samplers'])
        assert duration>0
        start=matrices(doc,a,animation,0);end=matrices(doc,a,animation,duration)
        error=max(float(np.abs(start[n]-end[n]).max()) for n in skin['joints'])
        assert error<2e-5,(name,error)
        for sampler in animation['samplers']:
            assert np.isfinite(a(sampler['input'])).all() and np.isfinite(a(sampler['output'])).all()
        loops[name]={'durationSeconds':duration,'endpointMatrixMaxError':error}
    return {'vertices':count,'triangles':triangles,'joints':53,'materials':5,'skins':1,'primitives':5,
            'weightedVertexHistogram':histogram,'blendedVertices':blended,'inverseBindMaxError':bind_error,
            'aPoseBounds':{'min':minv.tolist(),'max':maxv.tolist()},'actions':loops}


def blender_compare(path):
    import bpy
    from robot_tools import reset_pose
    doc,a=read(path);rig=bpy.data.objects['SilverRobotRig'];obj=bpy.data.objects['SilverRobot']
    assert bpy.context.scene.render.fps==30
    names=[doc['nodes'][n]['name'] for n in doc['skins'][0]['joints']]
    groups={g.index:g.name for g in obj.vertex_groups if g.name in rig.data.bones}
    # Link split glTF vertices to source vertices using rest position AND bone weights.
    def key(p,weights):
        return (tuple(round(float(x),5) for x in p),tuple(sorted((n,round(float(w),5)) for n,w in weights if w>1e-7)))
    def axes(p):return (p[0],p[2],-p[1])
    source={}
    for v in obj.data.vertices:
        k=key(axes(v.co),[(groups[g.group],g.weight) for g in v.groups if g.group in groups])
        source.setdefault(k,v.index)
    indices=[]
    for primitive in doc['meshes'][0]['primitives']:
        attrs=primitive['attributes'];p=a(attrs['POSITION']);j=a(attrs['JOINTS_0']);w=a(attrs['WEIGHTS_0'])
        ids=[]
        for pos,joints,weights in zip(p,j,w):
            k=key(pos,[(names[int(ji)],we) for ji,we in zip(joints,weights)])
            assert k in source,('Export vertex absent from source',k)
            ids.append(source[k])
        indices.append(np.array(ids))
    old_action=rig.animation_data.action;old_frame=bpy.context.scene.frame_current
    result={}
    try:
        for animation in doc['animations']:
            name=animation['name'];action=bpy.data.actions[name];rig.animation_data.action=action
            end=int(action.frame_end);frames=sorted(set([1,end,1+(end-1)//4,1+(end-1)//2,1+3*(end-1)//4]))
            errors=[]
            for frame in frames:
                reset_pose(rig);bpy.context.scene.frame_set(frame);bpy.context.view_layer.update()
                evaluated=obj.evaluated_get(bpy.context.evaluated_depsgraph_get())
                mesh=evaluated.to_mesh()
                expected=np.array([axes(evaluated.matrix_world@v.co) for v in mesh.vertices])
                evaluated.to_mesh_clear()
                actual=skin_positions(doc,a,matrices(doc,a,animation,(frame-1)/30))
                error=max(float(np.linalg.norm(p-expected[ids],axis=1).max()) for p,ids in zip(actual,indices))
                assert error<3e-5,(name,frame,error)
                errors.append(error)
            result[name]={'frames':frames,'verticesComparedPerFrame':sum(len(i) for i in indices),'maxErrorMetres':max(errors)}
    finally:
        rig.animation_data.action=old_action;bpy.context.scene.frame_set(old_frame);reset_pose(rig)
    return result


def main():
    p=argparse.ArgumentParser();p.add_argument('--asset-dir',type=Path,default=ROOT/'Assets/Models/SilverRobot')
    p.add_argument('--compare',type=Path);p.add_argument('--blender-compare',action='store_true');p.add_argument('--report',type=Path)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else (sys.argv[1:] if 'bpy' not in sys.modules else []))
    report={'gltf':profile(args.asset_dir/'SilverRobot.gltf'),'glb':profile(args.asset_dir/'SilverRobot.glb')}
    assert report['gltf']==report['glb']
    if args.blender_compare:
        report['blenderCpuSkinning']=blender_compare(args.asset_dir/'SilverRobot.gltf')
    if args.compare:
        for name in ('SilverRobot.gltf','SilverRobot.bin','SilverRobot.glb','Humanoid.json'):
            assert (args.asset_dir/name).read_bytes()==(args.compare/name).read_bytes(),('Re-export differs',name)
        report['savedSourceReexport']='byte-identical'
    report['sha256']={path.name:hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(args.asset_dir.iterdir()) if path.suffix in ('.gltf','.glb','.bin','.json')}
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True);args.report.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__=='__main__':main()
