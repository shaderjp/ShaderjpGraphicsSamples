"""Initial generation only. Saved authoring sources are never overwritten."""
import argparse
import json
import math
from pathlib import Path
import sys
import bpy
from mathutils import Vector
sys.path.insert(0,str(Path(__file__).resolve().parent))
from gallery_tools import *

def main():
    p=argparse.ArgumentParser(); p.add_argument('--source-dir',type=Path,default=HERE)
    destination=arguments(p).source_dir.resolve()
    if (destination/'BindlessGallery.blend').exists():
        raise RuntimeError('Saved source exists: edit the blend and use export.py')
    destination.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    base=bpy.context.scene; base.name='Catalog'
    images=texture_catalog(destination/'.generated')
    materials=[]
    for i in range(256):
        inputs={key:images[key,index] for key,index in [('Albedo',i),('Normal',i%64),('ORM',i%64),('Emissive',i%16)]}
        mat=material(f'M{i:03d}',inputs,(1,1,1),1,1,'MASK' if i%16==15 else 'OPAQUE',1,.12)
        mat['id']=f'material:{i:03d}'
        materials.append(mat)
    prototypes=prototype_catalog(materials[0])
    shots={}
    for scene_name,cols,rows,budget in [('Mini',2,1,16),('Standard',4,2,64),('Stress',8,8,256)]:
        scene=bpy.data.scenes.new(scene_name); render_settings(scene,64,True); scene.world=world(scene_name+'Sky')
        for room in range(cols*rows):
            offset=Vector(((room%cols-(cols-1)/2)*10,(room//cols-(rows-1)/2)*10,0))
            for slot,proto in enumerate(prototypes):
                obj=proto.copy(); obj.data=proto.data
                scene.collection.objects.link(obj)
                obj.name=f'{scene_name}_{room:02d}_{slot:02d}'
                pos,rot=local_placement(slot); obj.location=Vector(pos)+offset; obj.rotation_euler=rot
                index=(slot+64*(room%4))%budget
                obj.material_slots[0].link='OBJECT'; obj.material_slots[0].material=materials[index]
                obj['id']=f'instance:{scene_name}:{room:02d}:{slot:02d}'
                obj['geometry']=slot; obj['material']=index
            data=bpy.data.lights.new(f'{scene_name}_Point{room}','POINT'); data.energy=1600; data.shadow_soft_size=.1
            light=bpy.data.objects.new(data.name,data); scene.collection.objects.link(light)
            light.location=offset+Vector((0,-1,4.5)); light['id']='light:'+light.name
        sun_data=bpy.data.lights.new(scene_name+'Sun','SUN'); sun_data.energy=2; sun_data.angle=0
        sun=bpy.data.objects.new(sun_data.name,sun_data); scene.collection.objects.link(sun)
        sun.rotation_euler=(.4,-.5,-.6); sun['id']='light:'+sun.name
        pos=(cols*6,-rows*8-10,max(cols,rows)*4)
        cam=camera(scene,scene_name+'Hero',pos,(0,0,.8),50); scene.camera=cam
        shots[cam.name]={'position':rh(pos),'target':rh((0,0,.8)),'fovDegrees':50,'near':.05,'far':250}
        if scene_name=='Standard':
            for name,pos,target in [('Materials',(-3.5,-3.5,2.7),(-2, -.8,1.6)),
                ('Reflection',(-18,-6,2.5),(-19, -4,1.5)),('Occlusion',(6,-10,2),(3,-3,1))]:
                camera(scene,name,pos,target,50)
                shots[name]={'position':rh(pos),'target':rh(target),'fovDegrees':50,'near':.05,'far':250}
    for obj in prototypes:
        bpy.data.objects.remove(obj,do_unlink=True)
    bpy.data.scenes.remove(base)
    scene=bpy.data.scenes['Standard']; bpy.context.window.scene=scene
    config={'profile':'workshop-scene-1','model':'../../Models/BindlessGallery/BindlessGallery.gltf',
        'coordinateSystem':'right-handed Y-up meters','defaultScene':'Standard','cameras':shots,
        'environment':{'prefix':'../../Environments/Workshop/WorkshopSky','intensity':.55,'yawDegrees':0},
        'exposure':1,'gallery':{'version':1,'prototypes':64,'materials':256,'images':400,
            'presets':{'Mini':{'objects':128,'materials':16},'Standard':{'objects':512,'materials':64},'Stress':{'objects':4096,'materials':256}}}}
    scene['gallery_config']=json.dumps(config)
    bpy.ops.wm.save_as_mainfile(filepath=str(destination/'BindlessGallery.blend'))
    print('Saved BindlessGallery: 64 prototypes, 256 materials, 400 packed images',flush=True)

if __name__=='__main__': main()
