"""Create the editable CullingLab source once. Never overwrite an authored blend."""
from pathlib import Path
import bpy
import json
import math
from mathutils import Vector

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
destination = HERE / 'CullingLab.blend'
if destination.exists():
    raise RuntimeError('CullingLab.blend exists; edit it and run export.py instead')
bpy.ops.wm.read_factory_settings(use_empty=True)
names = ['Housing_00', 'Rotor_00', 'Bearing_00', 'ToolBox_00', 'BenchTop_00', 'GridPanel_00']
with bpy.data.libraries.load(str(HERE.parent/'Workshop/Workshop.blend'), link=False) as (src, dst):
    dst.objects = names
templates = []
for original in dst.objects:
    mesh = original.data.copy()
    mesh.name = 'Culling_' + original.data.name
    lo = Vector(tuple(min(v.co[a] for v in mesh.vertices) for a in range(3)))
    hi = Vector(tuple(max(v.co[a] for v in mesh.vertices) for a in range(3)))
    center = (lo + hi) * .5
    scale = 1.1 / max(hi - lo)
    for v in mesh.vertices:
        v.co = (v.co - center) * scale
    mesh['id'] = 'mesh:' + mesh.name
    templates.append(mesh)
door_material = templates[3].materials[0]
with bpy.data.libraries.load(str(HERE.parent/'Workshop/Workshop.blend'), link=False) as (src, dst):
    dst.materials = ['Plaster','Concrete','Teal']
wall_material = bpy.data.materials['Plaster']
for obj in list(bpy.data.objects):
    bpy.data.objects.remove(obj, do_unlink=True)
for scene in list(bpy.data.scenes)[1:]:
    bpy.data.scenes.remove(scene)

def box(scene, name, center, size, role='occluder'):
    bpy.context.window.scene = scene
    bpy.ops.mesh.primitive_cube_add(size=1, location=center)
    obj = bpy.context.object
    obj.name = name
    # Bake nonuniform dimensions into geometry: the runtime profile uses uniform node scales.
    for v in obj.data.vertices:
        v.co.x *= size[0]; v.co.y *= size[1]; v.co.z *= size[2]
    obj.data.materials.append(door_material if role=='door' else bpy.data.materials['Concrete'] if 'Floor' in name else bpy.data.materials['Teal'] if 'Column' in name else wall_material)
    obj['id'] = role + ':' + name
    obj['culling_role'] = role
    obj.data['id'] = 'mesh:' + name
    return obj

for si, (name, grid, count) in enumerate([('Mini', 2, 64), ('Standard', 4, 512), ('Stress', 8, 4096)]):
    scene = bpy.data.scenes[0] if si == 0 else bpy.data.scenes.new(name)
    scene.name = name
    scene['id'] = 'scene:' + name
    bpy.context.window.scene = scene
    box(scene, name+'_Floor', (grid*4, grid*4, -.2), (grid*8+6, grid*8+12, .4))
    per_room = count // (grid*grid)
    for ry in range(grid):
        for rx in range(grid):
            prefix = f'{name}_{rx}_{ry}'
            x, y = rx*8, ry*8
            box(scene, prefix+'_Side', (x-.1,y+4,2), (.2,8,4))
            box(scene, prefix+'_BackLeft', (x+1.4,y+8,2), (2.8,.3,4))
            box(scene, prefix+'_BackRight', (x+6.4,y+8,2), (3.2,.3,4))
            box(scene, prefix+'_Lintel', (x+3.8,y+8,3.6), (2,.3,.8))
            box(scene, prefix+'_Column', (x+6.6,y+4,2), (.65,.65,4))
            if rx == 0 and ry == 0:
                box(scene, name+'_Door', (x+3.8,y+8,1.6), (2,.2,3.2), 'door')
            cols = 4 if per_room == 16 else 8
            rows = per_room // cols
            for j in range(per_room):
                index = (ry*grid+rx)*per_room+j
                obj = bpy.data.objects.new(f'{name}_Object_{index:04}', templates[index % len(templates)])
                scene.collection.objects.link(obj)
                obj.location = (x+.7+(j%cols)*6.6/cols, y+.8+(j//cols)*6.4/rows, .6)
                obj.rotation_euler.z = (index % 4)*math.pi/2
                obj['id'] = f'object:{name}:{index:04}'
                obj['culling_role'] = 'target'
    light = bpy.data.lights.new(name+'_Sun', 'SUN')
    light.energy = 1.2
    obj = bpy.data.objects.new(light.name, light)
    scene.collection.objects.link(obj)
    obj.rotation_euler = (.4,-.6,-.3)
    obj['id'] = 'light:' + name
    scene.render.resolution_x, scene.render.resolution_y = 1920, 1080
    scene.render.resolution_percentage = 100
    scene.world = bpy.data.worlds.new(name+'_World')
    scene.world.color = (.15,.18,.22)
for image in bpy.data.images:
    if image.has_data and not image.packed_file:
        image.pack()
bpy.context.window.scene = bpy.data.scenes['Standard']
bpy.ops.wm.save_as_mainfile(filepath=str(destination))
print('Created', destination)
