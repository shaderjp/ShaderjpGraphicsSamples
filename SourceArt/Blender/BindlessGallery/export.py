"""Export SAVED Blender data. Material variants share geometry accessors. MIT."""
import argparse
import json
from pathlib import Path
import sys
import bpy
import numpy as np
sys.path.insert(0,str(Path(__file__).resolve().parent))
from gallery_tools import *
from workshop_tools import save_source_image

def main():
    parser=argparse.ArgumentParser(); parser.add_argument('--output-dir',type=Path,default=ROOT/'Assets')
    args=arguments(parser)
    if Path(bpy.data.filepath).stem!='BindlessGallery':
        raise RuntimeError('Open saved BindlessGallery.blend')
    source=Path(bpy.data.filepath); original=sha(source)
    out=args.output_dir.resolve(); directory=out/'Models'/'BindlessGallery'; directory.mkdir(parents=True,exist_ok=True)
    config=json.loads(bpy.data.scenes['Standard']['gallery_config'])
    data={'asset':{'version':'2.0','generator':'Shader.jp saved BindlessGallery exporter 1'},
          'buffers':[],'bufferViews':[],'accessors':[],'meshes':[],'materials':[],'textures':[],
          'images':[],'samplers':[{'magFilter':9729,'minFilter':9987,'wrapS':10497,'wrapT':10497}],
          'nodes':[],'scenes':[],'scene':1,'extensionsUsed':['KHR_lights_punctual','KHR_materials_emissive_strength'],
          'extensions':{'KHR_lights_punctual':{'lights':[]}}}
    binary=bytearray()
    def accessor(a,kind,target):
        a=np.ascontiguousarray(a,dtype='<u4' if kind=='SCALAR' else '<f4')
        while len(binary)%4: binary.append(0)
        offset=len(binary); binary.extend(a.tobytes()); view=len(data['bufferViews'])
        data['bufferViews'].append({'buffer':0,'byteOffset':offset,'byteLength':a.nbytes,'target':target})
        info={'bufferView':view,'componentType':5125 if kind=='SCALAR' else 5126,'count':len(a),'type':kind}
        if kind=='VEC3':
            info['min']=a.min(axis=0).tolist(); info['max']=a.max(axis=0).tolist()
        data['accessors'].append(info); return len(data['accessors'])-1
    image_indices={}
    for image in sorted((i for i in bpy.data.images if i.packed_file),key=lambda i:i.name):
        save_source_image(image,directory/'Textures'/f'{image.name}.png')
        image_indices[image.name]=len(data['images'])
        data['images'].append({'uri':f'Textures/{image.name}.png'})
        data['textures'].append({'source':len(data['images'])-1,'sampler':0})
    for material_index in range(256):
        m=bpy.data.materials[f'M{material_index:03d}']; profile=json.loads(m['profile'])
        def tex(key): return {'index':image_indices[m.node_tree.nodes['Texture_'+key].image.name]}
        info={'name':m.name,'extras':{'id':m['id']},'pbrMetallicRoughness':{
            'baseColorTexture':tex('Albedo'),'baseColorFactor':profile['baseColorFactor'],
            'metallicRoughnessTexture':tex('ORM'),'metallicFactor':profile['metallicFactor'],
            'roughnessFactor':profile['roughnessFactor']},'normalTexture':tex('Normal'),
            'occlusionTexture':tex('ORM'),'emissiveTexture':tex('Emissive'),
            'emissiveFactor':profile['emissiveFactor'],'alphaMode':profile['alphaMode'],
            'doubleSided':profile['doubleSided']}
        if info['alphaMode']=='MASK': info['alphaCutoff']=.5
        # glTF emissiveStrength is >= 1. Values below one belong in the factor.
        info['emissiveFactor']=[x*profile['emissiveStrength'] for x in profile['emissiveFactor']]
        data['materials'].append(info)
    geometry={}
    meshes=sorted((m for m in bpy.data.meshes if 'catalogIndex' in m),key=lambda m:m['catalogIndex'])
    for mesh in meshes:
        mesh.calc_loop_triangles()
        pos=np.array([tuple(mesh.vertices[l.vertex_index].co) for l in mesh.loops],dtype=np.float64)
        normal=np.array([tuple(n.vector) for n in mesh.corner_normals],dtype=np.float64)
        uv=np.array([tuple(v.uv) for v in mesh.uv_layers[0].data],dtype=np.float64)
        pos=pos[:,[0,2,1]]*np.array([1,1,-1]); normal=normal[:,[0,2,1]]*np.array([1,1,-1]); uv[:,1]=1-uv[:,1]
        tri=np.array([tuple(t.loops) for t in mesh.loop_triangles],dtype=np.int64)
        # Split mirrored UV tangent signs before welding corners.
        edges=pos[tri[:,1:]]-pos[tri[:,0,None]]; delta=uv[tri[:,1:]]-uv[tri[:,0,None]]
        det=delta[:,0,0]*delta[:,1,1]-delta[:,0,1]*delta[:,1,0]
        if np.any(abs(det)<1e-14): raise RuntimeError('Degenerate UV triangle '+mesh.name)
        tangent=(edges[:,0]*delta[:,1,1,None]-edges[:,1]*delta[:,0,1,None])/det[:,None]
        bitangent=(edges[:,1]*delta[:,0,0,None]-edges[:,0]*delta[:,1,0,None])/det[:,None]
        t=np.repeat(tangent,3,axis=0); n=normal[tri.ravel()]
        t-=n*(t*n).sum(axis=1)[:,None]; t/=np.linalg.norm(t,axis=1)[:,None]
        sign=np.where((np.cross(n,t)*np.repeat(bitangent,3,axis=0)).sum(axis=1)<0,-1,1)
        rows=np.column_stack((pos[tri.ravel()],n,uv[tri.ravel()],t,sign)).astype('<f4')
        unique,ind=np.unique(rows,axis=0,return_inverse=True)
        attrs={key:accessor(unique[:,a:b],kind,34962) for key,a,b,kind in
            [('POSITION',0,3,'VEC3'),('NORMAL',3,6,'VEC3'),('TEXCOORD_0',6,8,'VEC2'),('TANGENT',8,12,'VEC4')]}
        geometry[int(mesh['catalogIndex'])]=(attrs,accessor(ind,'SCALAR',34963))
    variants={}; primitive_geometry=[]
    for scene_name in ('Mini','Standard','Stress'):
        scene=bpy.data.scenes[scene_name]
        bpy.context.window.scene=scene
        scene.view_layers[0].update()
        roots=[]
        for obj in sorted(scene.objects,key=lambda o:o['id']):
            if obj.type not in ('MESH','LIGHT'): continue
            p,q,s=obj.matrix_world.decompose()
            # Conjugate a rotation by the Z-up -> Y-up basis.
            from mathutils import Matrix
            basis=Matrix(((1,0,0),(0,0,1),(0,-1,0)))
            qr=(basis @ q.to_matrix() @ basis.transposed()).to_quaternion()
            node={'name':obj.name,'extras':{'id':obj['id']},'translation':rh(p),
                  'rotation':[qr.x,qr.y,qr.z,qr.w],'scale':list(s)}
            if obj.type=='MESH':
                g=int(obj['geometry']); material_index=int(obj['material']); key=g,material_index
                if key not in variants:
                    variants[key]=len(data['meshes']); attrs,ind=geometry[g]
                    suffix=f'{g:02d}:{material_index:03d}'
                    data['meshes'].append({'name':'GeometryMaterial_'+suffix,'extras':{'id':'mesh:'+suffix,'geometry':g},
                        'primitives':[{'attributes':attrs,'indices':ind,'material':material_index,
                            'extras':{'id':'primitive:'+suffix}}]})
                    primitive_geometry.append(g)
                node['mesh']=variants[key]
            else:
                light=obj.data; li=len(data['extensions']['KHR_lights_punctual']['lights'])
                info={'name':light.name,'type':'directional' if light.type=='SUN' else 'point',
                      'color':list(light.color),'intensity':float(light.energy)*683/(1 if light.type=='SUN' else 4*math.pi)}
                data['extensions']['KHR_lights_punctual']['lights'].append(info)
                node['extensions']={'KHR_lights_punctual':{'light':li}}
            roots.append(len(data['nodes'])); data['nodes'].append(node)
        data['scenes'].append({'name':scene_name,'extras':{'id':'scene:'+scene_name},'nodes':roots})
    binary_path=directory/'BindlessGallery.bin'; binary_path.write_bytes(binary)
    data['buffers']=[{'uri':binary_path.name,'byteLength':len(binary)}]
    # All variants point to the exact same accessor payload; no material copies of geometry.
    json_write(directory/'BindlessGallery.gltf',data)
    config['gallery']['primitiveGeometry']=primitive_geometry
    json_write(out/'Scenes'/'BindlessGallery'/'BindlessGallery.json',config)
    frames=[]
    knots=[(0,(22,-25,16),(0,0,1)),(400,(12,-15,7),(0,-3,1)),
           (800,(-12,-13,5),(-8,-3,1)),(1200,(-22,10,15),(0,0,1))]
    for frame in range(1201):
        k=next(i for i in range(len(knots)-1) if knots[i][0]<=frame<=knots[i+1][0])
        a,pa,ta=knots[k]; b,pb,tb=knots[k+1]; t=(frame-a)/(b-a)
        frames.append({'position':rh([(1-t)*pa[j]+t*pb[j] for j in range(3)]),
                       'target':rh([(1-t)*ta[j]+t*tb[j] for j in range(3)]),'fovDegrees':50,'near':.05,'far':250})
    json_write(out/'Scenes'/'BindlessGallery'/'CameraPath.json',{'durationSeconds':20,'fps':60,'frames':frames})
    identities={'modelSha256':sha(directory/'BindlessGallery.gltf'),'binarySha256':sha(binary_path),
        'sourceSha256':original,'settingsSha256':sha(out/'Scenes'/'BindlessGallery'/'BindlessGallery.json'),
        'cameraPathSha256':sha(out/'Scenes'/'BindlessGallery'/'CameraPath.json'),
        'images':{i['uri']:sha(directory/i['uri']) for i in data['images']}}
    import hashlib
    identities['assetSha256']=hashlib.sha256(json.dumps(identities,sort_keys=True).encode()).hexdigest()
    json_write(out/'Scenes'/'BindlessGallery'/'Manifest.json',{'profile':'bindless-gallery-manifest-1',**identities})
    if sha(source)!=original: raise RuntimeError('Export modified authoring source')
    print(f'Exported {len(geometry)} geometries, {len(variants)} shared-payload material variants, 400 images',flush=True)

if __name__=='__main__': main()
