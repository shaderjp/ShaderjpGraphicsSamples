"""Export saved PathTracing sources, without saving or regenerating them. MIT License."""
import argparse
import json
import math
from pathlib import Path
import sys
import bpy
import numpy as np
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/"Workshop"))
sys.path.insert(0,str(HERE))
from workshop_tools import ROOT, arguments, json_write, save_source_image

def main():
    p=argparse.ArgumentParser(); p.add_argument('--output-dir',type=Path,default=ROOT/'Assets')
    args=arguments(p); out=args.output_dir.resolve(); directory=out/'Models'/'PathTracing'
    directory.mkdir(parents=True,exist_ok=True)
    if Path(bpy.data.filepath).stem!='PathTracing': raise RuntimeError('Open saved PathTracing.blend')
    scene=bpy.data.scenes['Room']; bpy.context.window.scene=scene
    config=json.loads(scene['pathtracing_config'])
    source_materials={m.name:m for m in bpy.data.materials if 'profile' in m}
    for image in bpy.data.images:
        if image.packed_file: save_source_image(image,directory/'Textures'/f'{image.name}.png')
    path=directory/'PathTracing.gltf'
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
    data['scene']=next(i for i,s in enumerate(data['scenes']) if s['name']=='Room')
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
    # Keep Blender MikkTSpace tangents; no cross-export binary rewriting.
    json_write(path,data); json_write(out/'Scenes'/'PathTracing'/'PathTracing.json',config)
    allowed={Path(i['uri']).name for i in images}
    for file in (directory/'Textures').glob('*.png'):
        if file.name not in allowed: file.unlink()
    print(f'Exported {len(data["meshes"])} shared meshes, {len(data["materials"])} materials, {len(data["nodes"])} nodes',flush=True)

if __name__=='__main__': main()
