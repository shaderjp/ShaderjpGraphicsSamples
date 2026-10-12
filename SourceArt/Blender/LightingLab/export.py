"""Export SAVED sources. Does not save or regenerate authoring files. MIT License."""
from pathlib import Path
import argparse
import json
import sys
import bpy
import math
from mathutils import Matrix
ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'ShaderBall'))
from shader_ball_tools import save_source_image

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--output-dir',type=Path,default=ROOT/'Assets')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    out = args.output_dir.resolve()
    if Path(bpy.data.filepath).stem != 'LightingLab':
        raise RuntimeError('Open the saved LightingLab.blend')
    scene = json.loads(bpy.context.scene['scene_json'])
    placements = {entry['name']:entry for entry in scene['objects']}
    # Blender Z-up -> glTF Y-up. Extract ZXY Euler to match the runtime's
    # roll/pitch/yaw composition, including combined-axis rotations.
    conversion = Matrix(((1,0,0),(0,0,1),(0,-1,0)))
    for obj in list(bpy.context.scene.objects):
        if obj.type != 'MESH' or 'asset_name' not in obj:
            continue
        name = obj['asset_name']
        if any(abs(component-1)>1e-6 for component in obj.scale):
            raise RuntimeError(f'Apply mesh scale before export: {name}')
        if obj.parent is not None:
            raise RuntimeError(f'Apply parent transforms before export: {name}')
        if name in placements:
            position = conversion @ obj.location
            rotation = (conversion @ obj.rotation_euler.to_matrix() @ conversion.transposed()).to_euler('ZXY')
            placements[name]['position'] = list(position)
            placements[name]['rotation'] = [math.degrees(value) for value in rotation]
        directory = out/'Models'/'LightingLab'/name
        directory.mkdir(parents=True,exist_ok=True)
        material = obj.data.materials[0]
        for key in ('Albedo','Normal','ORM','Emissive'):
            image = material.node_tree.nodes['Texture_'+key].image
            save_source_image(image,directory/'Textures'/f'{key}.png')
        bpy.ops.object.select_all(action='DESELECT')
        obj.select_set(True)
        bpy.context.view_layer.objects.active = obj
        location,rotation = obj.location.copy(),obj.rotation_euler.copy()
        try:
            obj.location = (0,0,0)
            obj.rotation_euler = (0,0,0)
            bpy.ops.export_scene.gltf(filepath=str(directory/f'{name}.gltf'),export_format='GLTF_SEPARATE',
                export_texture_dir='Textures',use_selection=True,export_yup=True,export_normals=True,
                export_tangents=True,export_texcoords=True,export_materials='EXPORT',export_vertex_color='NONE',
                export_animations=False,export_morph=False,export_skins=False,export_cameras=False,
                export_lights=False,export_extras=False,export_apply=False)
            path = directory/f'{name}.gltf'
            data = json.loads(path.read_text())
            if data.get('extensionsUsed') or len(data['nodes'])!=1 or len(data['materials'])!=1:
                raise RuntimeError('Export exceeded the static single-material profile')
            # Packed image datablock names are arbitrary; canonicalize URIs and
            # preserve the exact packed source PNG bytes for deterministic export.
            image_names = {}
            mat = data['materials'][0]
            tex = data['textures']
            for key,info in [('Albedo',mat['pbrMetallicRoughness']['baseColorTexture']),('Normal',mat['normalTexture']),
                             ('ORM',mat['pbrMetallicRoughness']['metallicRoughnessTexture']),('Emissive',mat['emissiveTexture'])]:
                image_names[tex[info['index']]['source']] = key
            for idx,key in image_names.items():
                data['images'][idx]['uri'] = f'Textures/{key}.png'
            path.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8',newline='\n')
        finally:
            obj.location,obj.rotation_euler = location,rotation
    scene_dir = out/'Scenes'/'LightingLab'
    scene_dir.mkdir(parents=True,exist_ok=True)
    (scene_dir/'Exhibition.json').write_text(json.dumps(scene,indent=2)+'\n',encoding='utf-8',newline='\n')
    study=json.loads(json.dumps(scene))
    study['objects']=[o for o in study['objects'] if o['name'].startswith('Sphere_')]
    study['camera']={'position':[1.2,1.7,1.6],'target':[1.2,.32,-1.3],'fovDegrees':45}
    (scene_dir/'MaterialStudy.json').write_text(json.dumps(study,indent=2)+'\n',encoding='utf-8',newline='\n')

if __name__ == '__main__':
    main()
