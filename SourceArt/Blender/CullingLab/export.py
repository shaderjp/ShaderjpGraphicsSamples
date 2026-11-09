"""Export the saved CullingLab, retaining editable source and shared geometry."""
from pathlib import Path
import sys
import json
import math
import hashlib
import bpy

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent/'Workshop'))
from workshop_tools import json_write, save_source_image
from export import canonical_tangents

if Path(bpy.data.filepath).name != 'CullingLab.blend':
    raise RuntimeError('Open saved CullingLab.blend')
out = ROOT/'Assets/Models/CullingLab'
settings = ROOT/'Assets/Scenes/CullingLab'
out.mkdir(parents=True, exist_ok=True)
settings.mkdir(parents=True, exist_ok=True)
materials = {m.name:m for m in bpy.data.materials if 'profile' in m}
for image in bpy.data.images:
    if image.packed_file:
        save_source_image(image, out/'Textures'/f'{image.name}.png')
bpy.ops.export_scene.gltf(filepath=str(out/'CullingLab.gltf'), export_format='GLTF_SEPARATE',
    export_texture_dir='Textures', use_active_scene=False, export_yup=True, export_normals=True,
    export_tangents=True, export_texcoords=True, export_materials='EXPORT', export_vertex_color='NONE',
    export_animations=False, export_morph=False, export_skins=False, export_cameras=False,
    export_lights=True, export_extras=True, export_apply=False, export_import_convert_lighting_mode='SPEC')
data = json.loads((out/'CullingLab.gltf').read_text())
for mat in data['materials']:
    source = materials[mat['name']]
    profile = json.loads(source['profile'])
    mat['extras'] = {'id':source['id']}
    pbr = mat['pbrMetallicRoughness']
    for key in ('baseColorFactor','metallicFactor','roughnessFactor'):
        pbr[key] = profile[key]
    mat['alphaMode'] = profile['alphaMode']
    mat['doubleSided'] = profile['doubleSided']
    if mat['alphaMode'] == 'MASK': mat['alphaCutoff'] = profile['alphaCutoff']
    mat['emissiveFactor'] = profile['emissiveFactor']
    mat.pop('extensions',None)
    for channel, info in [('Albedo',pbr['baseColorTexture']),('Normal',mat['normalTexture']),
                           ('ORM',pbr['metallicRoughnessTexture']),('Emissive',mat['emissiveTexture'])]:
        image = source.node_tree.nodes['Texture_'+channel].image
        texture = data['textures'][info['index']]
        data['images'][texture['source']]['uri'] = 'Textures/'+image.name+'.png'
    mat['occlusionTexture'] = {'index':pbr['metallicRoughnessTexture']['index']}
for mesh in data['meshes']:
    mesh['extras'] = {'id':'mesh:'+mesh['name']}
    for i,p in enumerate(mesh['primitives']): p['extras'] = {'id':f'primitive:{mesh["name"]}:{i}'}
for scene in data['scenes']: scene['extras'] = {'id':'scene:'+scene['name']}
for light in data['extensions']['KHR_lights_punctual']['lights']:
    light['intensity'] = 819.6
data['extensionsUsed'] = ['KHR_lights_punctual']
data.pop('extensionsRequired',None)
data['scene'] = next(i for i,s in enumerate(data['scenes']) if s['name']=='Standard')
canonical_tangents(data,out)
json_write(out/'CullingLab.gltf',data)
def camera(position, target, fov=60):
    # Authored settings use glTF RH Y-up. Runtime loader flips Z once.
    return dict(position=[position[0],position[1],-position[2]],target=[target[0],target[1],-target[2]],
                fovDegrees=fov,near=.05,far=160)
cameras = {'Hero':camera([4,2.5,-5],[4,1.2,10]),
    'Occlusion':camera([2,1.6,3],[3,1.2,17]),
    'Opening':camera([3.8,1.6,6],[3.8,1,15]),
    'Plaza':camera([16,32,-20],[16,0,16],65),
    'Near':camera([3.8,1.6,7.9],[3.8,1.6,12])}
roles = {o['id']:o['culling_role'] for s in bpy.data.scenes for o in s.objects if 'culling_role' in o}
json_write(settings/'CullingLab.json',dict(profile='workshop-scene-1',model='../../Models/CullingLab/CullingLab.gltf',
    cameras=cameras,environment=dict(prefix='../../Environments/Workshop/WorkshopSky',intensity=.8),
    exposure=1, cullingRoles=roles))
keys = [([4,2.5,-5],[4,1.2,10]),([3.8,1.6,6],[3.8,1.2,16]),
    ([4,1.6,11],[10,1.2,14]),([11,1.6,14],[12,1.2,24]),
    ([12,2,20],[4,1.2,8])]
frames=[]
for f in range(1201):
    t=f/300; k=min(int(t),3); a=t-k
    frames.append(camera([keys[k][0][c]*(1-a)+keys[k+1][0][c]*a for c in range(3)],
                         [keys[k][1][c]*(1-a)+keys[k+1][1][c]*a for c in range(3)]))
json_write(settings/'CameraPath.json',{'frames':frames})
json_write(settings/'Manifest.json',{'sourceSha256':hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest(),
    'blender':bpy.app.version_string,'targets':{'Mini':64,'Standard':512,'Stress':4096},
    'files':{str(p.relative_to(ROOT)):hashlib.sha256(p.read_bytes()).hexdigest() for p in out.rglob('*') if p.is_file()}})
print('Exported CullingLab:',len(data['meshes']),'meshes,',len(data['nodes']),'nodes')
