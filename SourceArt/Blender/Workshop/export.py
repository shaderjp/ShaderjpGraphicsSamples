"""Export saved Workshop sources, without saving or regenerating them. MIT License."""
import argparse
import json
import math
from pathlib import Path
import sys
import bpy
import numpy as np
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from workshop_tools import ROOT, arguments, json_write, save_source_image

def canonical_tangents(data,directory):
    """Deterministic float64 UV-gradient/angle-weighted tangent frame.

    Blender's rounded MikkTSpace output can cross a 1e-4 rounding boundary on
    reload. Positions, normals, UV seams and indices remain authored/exported.
    This offline derivation is not a new dependency of the runtime loader.
    """
    buffers=[bytearray((directory/b['uri']).read_bytes()) for b in data['buffers']]
    def read(index):
        a=data['accessors'][index]; v=data['bufferViews'][a['bufferView']]
        dtype={5126:'<f4',5123:'<u2',5125:'<u4'}[a['componentType']]
        width={'SCALAR':1,'VEC2':2,'VEC3':3,'VEC4':4}[a['type']]
        return np.ndarray((a['count'],width),dtype=dtype,buffer=buffers[v['buffer']],
            offset=v.get('byteOffset',0)+a.get('byteOffset',0),
            strides=(v.get('byteStride',np.dtype(dtype).itemsize*width),np.dtype(dtype).itemsize))
    processed=set()
    for mesh in data['meshes']:
        for p in mesh['primitives']:
            ti=p['attributes']['TANGENT']
            if ti in processed: continue
            processed.add(ti)
            position=read(p['attributes']['POSITION']).astype(np.float64)
            normal=read(p['attributes']['NORMAL']).astype(np.float64)
            uv=read(p['attributes']['TEXCOORD_0']).astype(np.float64)
            tri=read(p['indices']).ravel().astype(np.int64).reshape(-1,3)
            edges=position[tri[:,1:]]-position[tri[:,0,None]]
            delta=uv[tri[:,1:]]-uv[tri[:,0,None]]
            determinant=delta[:,0,0]*delta[:,1,1]-delta[:,0,1]*delta[:,1,0]
            if np.any(abs(determinant)<1e-14): raise RuntimeError('Degenerate UV triangle: '+mesh['name'])
            tangent=(edges[:,0]*delta[:,1,1,None]-edges[:,1]*delta[:,0,1,None])/determinant[:,None]
            bitangent=(edges[:,1]*delta[:,0,0,None]-edges[:,0]*delta[:,1,0,None])/determinant[:,None]
            accum=np.zeros_like(position); accum_b=np.zeros_like(position)
            for corner in range(3):
                a=position[tri[:,(corner+1)%3]]-position[tri[:,corner]]
                b=position[tri[:,(corner+2)%3]]-position[tri[:,corner]]
                weight=np.arccos(np.clip((a*b).sum(axis=1)/(np.linalg.norm(a,axis=1)*np.linalg.norm(b,axis=1)),-1,1))
                np.add.at(accum,tri[:,corner],tangent*weight[:,None])
                np.add.at(accum_b,tri[:,corner],bitangent*weight[:,None])
            accum-=normal*(accum*normal).sum(axis=1)[:,None]
            length=np.linalg.norm(accum,axis=1)
            if np.any(length<1e-12): raise RuntimeError('Undefined tangent: '+mesh['name'])
            accum/=length[:,None]
            sign=np.where((np.cross(normal,accum)*accum_b).sum(axis=1)<0,-1,1)
            result=np.column_stack((accum,sign)).astype('<f4')
            if not np.isfinite(result).all(): raise RuntimeError('Non-finite tangent')
            read(ti)[:]=result
    for info,buffer in zip(data['buffers'],buffers): (directory/info['uri']).write_bytes(buffer)

def main():
    p=argparse.ArgumentParser(); p.add_argument('--output-dir',type=Path,default=ROOT/'Assets')
    args=arguments(p); out=args.output_dir.resolve(); directory=out/'Models'/'Workshop'
    directory.mkdir(parents=True,exist_ok=True)
    if Path(bpy.data.filepath).stem!='Workshop': raise RuntimeError('Open saved Workshop.blend')
    scene=bpy.data.scenes['Standard']; bpy.context.window.scene=scene
    config=json.loads(scene['workshop_config'])
    source_materials={m.name:m for m in bpy.data.materials if 'profile' in m}
    for image in bpy.data.images:
        if image.packed_file: save_source_image(image,directory/'Textures'/f'{image.name}.png')
    path=directory/'Workshop.gltf'
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLTF_SEPARATE',export_texture_dir='Textures',
        use_active_scene=False,export_yup=True,export_normals=True,export_tangents=True,
        export_texcoords=True,export_materials='EXPORT',export_vertex_color='NONE',export_animations=False,
        export_morph=False,export_skins=False,export_cameras=True,export_lights=True,export_extras=True,
        export_apply=False,export_import_convert_lighting_mode='SPEC')
    data=json.loads(path.read_text(encoding='utf-8'))
    used=set()
    for mat in data['materials']:
        source=source_materials[mat['name']]; profile=json.loads(source['profile'])
        mat['extras']={'id':source['id']}
        pbr=mat['pbrMetallicRoughness']
        for key in ('baseColorFactor','metallicFactor','roughnessFactor'): pbr[key]=profile[key]
        mat['alphaMode']=profile['alphaMode']; mat['doubleSided']=profile['doubleSided']
        if mat['alphaMode']=='MASK': mat['alphaCutoff']=profile['alphaCutoff']
        else: mat.pop('alphaCutoff',None)
        mat['emissiveFactor']=profile['emissiveFactor']
        mat.pop('extensions',None)
        if profile['emissiveStrength']!=1:
            mat['extensions']={'KHR_materials_emissive_strength':{'emissiveStrength':profile['emissiveStrength']}}
            used.add('KHR_materials_emissive_strength')
        # Canonical packed PNGs and shared ORM, rather than exporter-generated channel packs.
        infos=[('Albedo',pbr['baseColorTexture']),('Normal',mat['normalTexture']),
            ('ORM',pbr['metallicRoughnessTexture']),('Emissive',mat['emissiveTexture'])]
        for key,info in infos:
            image=source.node_tree.nodes['Texture_'+key].image
            texture=data['textures'][info['index']]
            data['images'][texture['source']]['uri']='Textures/'+image.name+'.png'
        mat['occlusionTexture']={'index':pbr['metallicRoughnessTexture']['index']}
    if 'KHR_lights_punctual' in data.get('extensions',{}): used.add('KHR_lights_punctual')
    config['lightConversion']={'mode':'SPEC','wattsToLumens':683,'sources':{}}
    for light in data.get('extensions',{}).get('KHR_lights_punctual',{}).get('lights',[]):
        source_light=bpy.data.lights[light['name']]
        # Blender 5.2's Sun node strength can stay at 1 when Light.energy is
        # edited. Use the same saved Light properties as reference rendering.
        light['intensity']=float(source_light.energy)*683/(4*math.pi if light['type']=='point' else 1)
        light['color']=list(source_light.color)
        config['lightConversion']['sources'][light['name']]={'energy':float(source_light.energy),
            'sourceUnit':'W' if light['type']=='point' else 'W/m2',
            'gltfUnit':'cd' if light['type']=='point' else 'lux'}
    data['extensionsUsed']=sorted(used); data.pop('extensionsRequired',None)
    for mesh in data['meshes']:
        source_mesh=bpy.data.meshes.get(mesh['name'])
        mesh_id=source_mesh.get('id','mesh:'+mesh['name']) if source_mesh else 'mesh:'+mesh['name']
        mesh['extras']={'id':mesh_id}
        for i,primitive in enumerate(mesh['primitives']): primitive['extras']={'id':'primitive:'+mesh_id.removeprefix('mesh:')+':'+str(i)}
    for scene_entry in data['scenes']:
        scene_entry['extras']={'id':'scene:'+scene_entry['name']}
    data['scene']=next(i for i,s in enumerate(data['scenes']) if s['name']=='Standard')
    # Canonicalize image/texture tables and discard unused exporter images.
    image_map={}; images=[]; texture_map={}; textures=[]
    def fix(info):
        old=data['textures'][info['index']]; uri=data['images'][old['source']]['uri']
        if uri not in image_map: image_map[uri]=len(images); images.append({'uri':uri})
        key=(uri,old.get('sampler',-1))
        if key not in texture_map:
            texture_map[key]=len(textures); item={'source':image_map[uri]}
            if 'sampler' in old: item['sampler']=old['sampler']
            textures.append(item)
        info['index']=texture_map[key]
    for mat in data['materials']:
        pbr=mat['pbrMetallicRoughness']
        for info in (pbr['baseColorTexture'],pbr['metallicRoughnessTexture'],mat['normalTexture'],mat['occlusionTexture'],mat['emissiveTexture']): fix(info)
    data['images'],data['textures']=images,textures
    # Preserve vertex/index order; only derive the tangent frame deterministically.
    for node in data['nodes']:
        extras=node.get('extras',{})
        for key in list(extras):
            if key not in ('id','role','targetRH'): del extras[key]
        if not extras: node.pop('extras',None)
    # Blender emits a separate node for the shared Sun in each Scene. Distinguish
    # node instances while retaining the common authored light identity.
    for scene_entry in data['scenes']:
        pending=list(scene_entry['nodes'])
        while pending:
            node=data['nodes'][pending.pop()]; pending.extend(node.get('children',[]))
            if node.get('extras',{}).get('id')=='light:Sun':
                node['extras']={'id':'light:'+scene_entry['name']+':Sun','sourceId':'light:Sun'}
    canonical_tangents(data,directory)
    json_write(path,data); json_write(out/'Scenes'/'Workshop'/'Workshop.json',config)
    allowed={Path(i['uri']).name for i in images}
    for file in (directory/'Textures').glob('*.png'):
        if file.name not in allowed: file.unlink()
    print(f'Exported {len(data["meshes"])} shared meshes, {len(data["materials"])} materials, {len(data["nodes"])} nodes',flush=True)

if __name__=='__main__': main()
