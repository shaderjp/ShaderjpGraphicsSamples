"""Create NEW LightingLab authoring sources. Refuses existing saved sources.
Run with Blender --background --factory-startup --python generate.py.
MIT License, Copyright (c) 2026 masafumi.
"""
from pathlib import Path
import json
import math
import sys
import bpy
import numpy as np
from mathutils import Vector

ROOT = Path(__file__).resolve().parents[3]
HERE = Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'ShaderBall'))
from shader_ball_tools import write_png, build_material

def reset():
    bpy.ops.wm.read_factory_settings(use_empty=True)

def camera(position,target,panorama=False):
    bpy.ops.object.camera_add(location=position)
    cam = bpy.context.object
    cam.rotation_euler = (Vector(target)-cam.location).to_track_quat('-Z','Y').to_euler()
    if panorama:
        cam.data.type = 'PANO'
        cam.data.panorama_type = 'EQUIRECTANGULAR'
    else:
        cam.data.type = 'PERSP'
        cam.data.lens = 40
    bpy.context.scene.camera = cam
    return cam

def cube(name,dimensions,position,material):
    bpy.ops.mesh.primitive_cube_add(size=1,location=position)
    obj = bpy.context.object
    obj.name = name
    obj.dimensions = dimensions
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    obj.data.materials.append(material)
    return obj

def plain(name,color,emission=False):
    m = bpy.data.materials.new(name)
    m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color,1)
    p.inputs['Roughness'].default_value = .8
    if emission:
        p.inputs['Emission Color'].default_value = (*color,1)
        p.inputs['Emission Strength'].default_value = 1
    return m

def render_settings(width,height,samples=256):
    s = bpy.context.scene
    s.render.engine = 'CYCLES'
    s.cycles.device = 'CPU'
    s.cycles.samples = samples
    s.cycles.seed = 0
    s.cycles.use_denoising = True
    s.render.resolution_x, s.render.resolution_y = width,height
    s.render.resolution_percentage = 100
    s.world = bpy.data.worlds.new('World')
    s.world.use_nodes = True
    s.world.node_tree.nodes['Background'].inputs['Color'].default_value = (.08,.08,.08,1)
    s.world.node_tree.nodes['Background'].inputs['Strength'].default_value = 1

def environment(name):
    reset()
    render_settings(2048,1024)
    room = name == 'Room'
    grey = plain('Grey',(.18,.2,.22) if room else (.2,.2,.2))
    floor = plain('Floor',(.09,.1,.12))
    cube('Floor',(16,16,.1),(0,0,-.1),floor)
    cube('Back wall',(16,.1,8),(0,8,4),grey)
    cube('Side wall',(.1,16,8),(-8,0,4),plain('Side',(.3,.09,.04) if room else (.2,.2,.2)))
    cube('Ceiling',(16,16,.1),(0,0,8),grey)
    cube('Key panel',(3,.08,3),(-3,5,4),plain('Key emission',(12,8,4) if room else (10,10,10),True))
    cube('Fill panel',(.08,2,3),(5,1,3),plain('Fill emission',(2,4,8) if room else (4,4,4),True))
    cube('Top panel',(2,2,.08),(0,0,6),plain('Top emission',(3,3,3),True))
    camera((0,0,1.5),(0,1,1.5),True)
    bpy.ops.wm.save_as_mainfile(filepath=str(HERE/f'{name}.blend'))

def material(name,metallic,roughness,color):
    directory = HERE/'.generated'/name
    directory.mkdir(parents=True,exist_ok=True)
    images = {}
    values = {'Albedo':(*color,1),'Normal':(.5,.5,1,1),'ORM':(1,roughness,metallic,1),'Emissive':(0,0,0,1)}
    for key,value in values.items():
        pixels = np.tile(np.rint(np.array(value)*255).astype(np.uint8),(4,4,1))
        path = directory/f'{key}.png'
        write_png(path,pixels)
        image = bpy.data.images.load(str(path),check_existing=False)
        image.name = name+'_'+key
        image.colorspace_settings.name = 'sRGB' if key in ('Albedo','Emissive') else 'Non-Color'
        image.pack()
        images[key] = image
    return build_material(images)

def exhibition():
    reset()
    m = material('Neutral',0,.65,(.65,.65,.65))
    objects = []
    objects.append(cube('Floor',(7,6,.12),(0,0,-.06),m))
    objects.append(cube('Wall',(7,.12,2.7),(0,2.8,1.35),m))
    objects.append(cube('Plinth',(1.5,1.5,.25),(-1.25,0,.125),m))
    objects.append(cube('Box',(.7,.7,.9),(1.1,-1,.45),m))
    objects.append(cube('FloatingPlate',(1.4,.8,.12),(1,1,1.65),m))
    objects.append(cube('ThinColumn',(.12,.12,1.4),(2.2,.6,.7),m))
    ramp = cube('Ramp',(1.2,.9,.16),(-2,-1.8,.3),m)
    ramp.rotation_euler.x = math.radians(18)
    objects.append(ramp)
    for row in range(2):
        for column,r in enumerate((.05,.15,.3,.6,.9)):
            bpy.ops.mesh.primitive_uv_sphere_add(segments=32,ring_count=16,radius=.22,location=(.15+column*.52,1.6-row*.55,.32))
            obj = bpy.context.object
            obj.name = f'Sphere_{row}_{column}'
            obj.data.materials.append(material(obj.name,row,r,(.7,.7,.7)))
            for p in obj.data.polygons:
                p.use_smooth = True
            objects.append(obj)
    records = []
    for obj in objects:
        position = tuple(obj.location)
        rotation = tuple(obj.rotation_euler)
        obj['asset_name'] = obj.name
        # Exporter temporarily clears placement, applying only the local geometry.
        mod = obj.modifiers.new('Triangulate','TRIANGULATE')
        bpy.context.view_layer.objects.active = obj
        bpy.ops.object.modifier_apply(modifier=mod.name)
        records.append({'name':obj.name,'model':f'../../Models/LightingLab/{obj.name}/{obj.name}.gltf',
                        'position':[position[0],position[2],-position[1]],
                        'rotation':[math.degrees(rotation[0]),0,0], 'scale':[1,1,1],
                        'castsShadow':obj.name not in ('Floor','Wall','Ramp'), 'movable':obj.name=='Box'})
    records.append({'name':'ShaderBall','model':'../../Models/ShaderBall/ShaderBall_PBR.gltf',
                    'position':[-1.25,.25,0],'rotation':[0,0,0],'scale':[1,1,1],'castsShadow':True,'movable':False})
    scene = {'version':1,'coordinateSystem':'right-handed Y-up meters','objects':records,
             'camera':{'position':[-4.8,3.4,6.3],'target':[0,.8,0],'fovDegrees':45},
             'light':{'direction':[-.7,.9,-.4],'color':[1,.95,.9],'intensity':3},'exposure':1,
             'environments':{'Studio':'../../Environments/LightingLab/Studio','Room':'../../Environments/LightingLab/Room'}}
    bpy.context.scene['scene_json'] = json.dumps(scene)
    bpy.ops.wm.save_as_mainfile(filepath=str(HERE/'LightingLab.blend'))

if __name__ == '__main__':
    if bpy.app.version[:3] != (5,2,1):
        raise RuntimeError('Use Blender 5.2.1 LTS')
    if any((HERE/f'{name}.blend').exists() for name in ('Studio','Room','LightingLab')):
        raise RuntimeError('Saved authoring sources exist. Edit/export them; do not regenerate.')
    HERE.mkdir(parents=True,exist_ok=True)
    for name in ('Studio','Room'):
        environment(name)
    exhibition()
