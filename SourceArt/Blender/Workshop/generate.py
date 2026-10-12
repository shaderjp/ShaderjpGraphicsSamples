"""Create NEW Workshop sources; never overwrite saved authoring files. MIT License."""
import argparse
import json
import math
from pathlib import Path
import sys
import bpy
HERE = Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from workshop_tools import *

def main():
    parser = argparse.ArgumentParser(); parser.add_argument('--source-dir',type=Path,default=HERE)
    destination = arguments(parser).source_dir.resolve()
    if any((destination/f'{name}.blend').exists() for name in ('Workshop','WorkshopSky')):
        raise RuntimeError('Saved source exists: use editing/export, not initial generation')
    destination.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene = bpy.context.scene; scene.name = 'Standard'
    render_settings(scene)
    scene.world = world()
    cache = destination/'.generated'; cache.mkdir(exist_ok=True)
    sets = {name:png_set(cache,name,size,name) for name,size in [('concrete',1024),('wood',1024),
        ('paint',1024),('metal',2048),('rubber',512),('label',512),('grid',512),('glass',4),('display',512),('amber',512)]}
    specs = [('Concrete','concrete',(1,1,1),0,1),('Plaster','paint',(.88,.88,.8),0,1),
        ('Walnut','wood',(1,1,1),0,.9),('Teal','paint',(.06,.43,.42),0,.52),
        ('Steel','metal',(.75,.8,.85),1,.38),('Chrome','metal',(.95,.98,1),1,.08),
        ('Brass','metal',(.87,.59,.2),1,.28),('Rubber','rubber',(.16,.18,.2),0,1),
        ('Navy','paint',(.045,.08,.12),0,.72),('Red','paint',(.65,.09,.035),0,.62),
        ('Label','label',(1,1,1),0,1),('Grid','grid',(.52,.64,.68),1,.8),
        ('Cover','glass',(.72,.88,.95),0,.7),('Display','display',(1,1,1),0,.8),
        ('Amber','amber',(1,1,1),0,.8),('RoughSteel','metal',(.68,.72,.74),1,.9)]
    mats = {}
    for name,surface,color,metal,rough in specs:
        mats[name] = material(name,sets[surface],color,metal,rough,
            'MASK' if name=='Grid' else 'BLEND' if name=='Cover' else 'OPAQUE',
            .24 if name=='Cover' else 1,4 if name=='Display' else 6 if name=='Amber' else 0)
    root = bpy.data.objects.new('StandardRoot',None); scene.collection.objects.link(root); root['id']='root:Standard'
    groups = {}
    for name in ('Architecture','Turbine','Workbench','Storage','Calibration'):
        obj = bpy.data.objects.new(name,None); scene.collection.objects.link(obj); obj.parent=root
        obj['id']='group:Standard:'+name; groups[name]=obj
    templates = {}
    def b(name,size,mat,bevel=.025): templates[name]=box(name,size,mats[mat],bevel)
    for name,size,mat in [
        ('Floor',(8,6,.18),'Concrete'),('BackWall',(8,.18,3.8),'Plaster'),('SideWall',(.18,6,3.8),'Plaster'),
        ('WindowBase',(.18,3,1.1),'Plaster'),('Column',(.16,.16,3.7),'Teal'),('RoofBeam',(8,.16,.22),'Navy'),
        ('RoofTruss',(2.1,.1,.12),'Steel'),('WindowFrame',(.1,2.9,.12),'Steel'),('Louver',(.08,2.6,.08),'Teal'),
        ('Partition',(.13,1.8,2.6),'Plaster'),('FloorTrim',(7.8,.08,.08),'Navy'),
        ('BenchTop',(3.3,1.3,.14),'Walnut'),('BenchRail',(3.15,.12,.14),'Navy'),('BenchLeg',(.12,.12,.82),'Steel'),
        ('MachineBase',(4.6,1.45,.22),'Teal'),('MachineFoot',(.18,.9,.24),'Rubber'),
        ('Cradle',(.16,.5,.48),'RoughSteel'),('Fin',(.055,.7,.65),'Steel'),
        ('MonitorFrame',(.76,.09,.52),'Navy'),('MonitorScreen',(.67,.012,.41),'Display'),('MonitorStem',(.08,.08,.3),'Steel'),
        ('ShelfUpright',(.08,.08,2.2),'Teal'),('ShelfPlank',(1.6,.55,.065),'Walnut'),('ShelfBrace',(1.6,.05,.06),'Navy'),
        ('Cabinet',(1.5,.65,1.0),'Teal'),('DrawerCabinet',(.9,.6,1.0),'Navy'),('CabinetDoor',(.7,.045,.85),'Navy'),('Drawer',(.85,.05,.17),'Teal'),
        ('Handle',(.25,.055,.045),'Chrome'),('ToolBox',(.38,.26,.2),'Red'),('ToolHead',(.17,.07,.06),'Chrome'),
        ('ToolHandle',(.03,.025,.22),'Walnut'),('Plaque',(1.35,.025,.52),'Label'),('SmallPlaque',(.4,.015,.16),'Label'),
        ('CoverTop',(.9,.75,.018),'Cover'),('CoverSide',(.018,.75,.48),'Cover'),('CoverBack',(.9,.018,.48),'Cover'),
        ('GridPanel',(.022,1.45,1.2),'Grid'),('LampFixture',(.3,.24,.1),'Navy'),('LampStem',(.04,.04,.5),'Steel'),
        ('Book',(.09,.22,.3),'Label'),('CalibrationBase',(1.1,.32,.09),'Navy'),('AmberStrip',(1.2,.018,.035),'Amber'),
        ('Vent',(1.2,.05,.3),'Navy'),('Bracket',(.08,.12,.2),'Brass')]: b(name,size,mat,0 if mat in ('Cover','Grid') else .025)
    for name,radius,depth,mat in [('Spindle',.065,4.0,'Chrome'),('Hub',.22,.35,'Brass'),
        ('Canister',.13,.32,'RoughSteel'),('Dial',.08,.035,'Brass'),('PipeStraight',.045,1.5,'Steel'),
        ('Valve',.12,.08,'Red'),('Bolt',.028,.045,'Chrome')]:
        templates[name]=cylinder(name,radius,depth,mats[mat])
    templates['Housing']=housing('Housing',mats['Teal'])
    templates['Rotor']=rotor('Rotor',mats['Chrome'])
    templates['Stator']=rotor('Stator',mats['Brass'],True)
    templates['Bearing']=torus('Bearing',.28,.065,mats['RoughSteel'],128,48)
    templates['Lip']=torus('Lip',.74,.025,mats['Brass'],128,24)
    templates['CoolantLoop']=torus('CoolantLoop',.36,.045,mats['Steel'])
    # A multi-material mesh demonstrates Primitive boundaries without changing the room into one mesh.
    rail=templates['BenchRail']; rail.data.materials.append(mats['Brass'])
    for face in rail.data.polygons:
        if abs(face.normal.z)>.8: face.material_index=1
    placements=[]
    def put(name,position,rotation=(0,0,0),group='Storage',role=None):
        proto=templates[name]; obj=proto.copy(); obj.data=proto.data
        scene.collection.objects.link(obj); obj.name=f'{name}_{sum(p.data==proto.data for p in placements):02d}'
        obj.parent=groups[group]; obj.location=position; obj.rotation_euler=rotation
        obj['id']='instance:Standard:'+obj.name
        if role: obj['role']=role
        placements.append(obj); return obj
    P=math.pi
    put('Floor',(0,0,-.09),group='Architecture')
    put('BackWall',(0,3,1.9),group='Architecture')
    put('SideWall',(-4,0,1.9),group='Architecture')
    put('WindowBase',(4,1.5,.55),group='Architecture')
    for x in (-3.85,0,3.85):
        for y in (-2.85,2.85):
            if x==0 and y<0: continue  # Clear view of the exploded rotor at the entrance.
            put('Column',(x,y,1.85),group='Architecture')
    for y in (-2.8,0,2.8): put('RoofBeam',(0,y,3.7),group='Architecture')
    for x in (-2.7,0,2.7): put('RoofTruss',(x,2.7,3.25),(0,.45,0),group='Architecture')
    for z in (1.1,3.6): put('WindowFrame',(4,1.5,z),group='Architecture')
    for z in (1.4,1.9,2.4,2.9,3.4): put('Louver',(4.03,1.5,z),(.2,0,0),group='Architecture')
    put('Partition',(-2.65,.8,1.3),group='Architecture')
    for y in (-2.85,2.85): put('FloorTrim',(0,y,.07),group='Architecture')
    put('MachineBase',(.25,-.15,.35),group='Turbine')
    for x in (-1.7,2.1): put('MachineFoot',(x,-.15,.16),group='Turbine')
    put('Spindle',(.25,-.15,1.28),(0,P/2,0),group='Turbine')
    for x in (-1.0,.2):
        put('Housing',(x,-.15,1.28),group='Turbine')
        put('Lip',(x+.6,-.15,1.28),(0,P/2,0),group='Turbine')
    for name,x in [('Rotor',1.05),('Stator',1.55),('Rotor',2.0),('Stator',-.25)]:
        put(name,(x,-.15,1.28),group='Turbine'); put('Hub',(x,-.15,1.28),(0,P/2,0),group='Turbine')
    for x in (-1.6,.55,2.15):
        put('Cradle',(x,-.15,.68),group='Turbine')
        put('Bearing',(x,-.15,1.28),(0,P/2,0),group='Turbine')
    for x in (-1.5,-.2,1.15): put('CoolantLoop',(x,.72,.84),(P/2,0,0),group='Turbine')
    for i in range(16): put('Fin',(-1.45+i*.05,-.15,.72),group='Turbine')
    for x in (-1.55,.6,2.15):
        for y in (-.67,.37): put('Bolt',(x,y,.48),group='Turbine')
    put('SmallPlaque',(.4,-.89,.49),(P/2,0,0),group='Turbine')
    put('GridPanel',(-2.45,-.5,1.0),group='Turbine')
    put('BenchTop',(-.25,2.05,.98),group='Workbench')
    for y in (1.47,2.63): put('BenchRail',(-.25,y,.85),group='Workbench')
    for x in (-1.7,1.2):
        for y in (1.55,2.55): put('BenchLeg',(x,y,.42),group='Workbench')
    put('MonitorFrame',(.3,2.4,1.53),group='Workbench')
    put('MonitorScreen',(.3,2.347,1.53),group='Workbench',role='renderTexture')
    put('MonitorStem',(.3,2.4,1.14),group='Workbench')
    put('ToolBox',(-1.2,2.1,1.15),group='Workbench')
    for x in (-.8,-.5,-.2):
        put('ToolHandle',(x,1.8,1.08),(P/2,0,.25),group='Workbench')
        put('ToolHead',(x,1.92,1.08),group='Workbench')
    put('CoolantLoop',(-1.0,2.2,1.13),group='Workbench')
    for x in (1.0,1.25): put('Canister',(x,2.2,1.2),group='Workbench')
    # The display cover does not overlap another transparent volume.
    put('CoverTop',(.3,1.7,1.62),group='Workbench')
    for x in (-.15,.75): put('CoverSide',(x,1.7,1.37),group='Workbench')
    put('CoverBack',(.3,2.075,1.37),group='Workbench')
    put('CoverBack',(.3,1.325,1.37),group='Workbench')
    put('Valve',(.3,1.7,1.13),group='Workbench')
    for x in (-3.0,3.15):
        shelf_y=2.5 if x<0 else 1.75
        for y in (shelf_y-.3,shelf_y+.25): put('ShelfUpright',(x-.76,y,1.1)); put('ShelfUpright',(x+.76,y,1.1))
        for z in (.3,.9,1.5,2.1):
            put('ShelfPlank',(x,shelf_y,z)); put('ShelfBrace',(x,shelf_y+.28,z+.1))
            for offset in (-.5,0,.5): put('Canister',(x+offset,shelf_y,z+.19))
        for i in range(5): put('Book',(x-.5+i*.12,shelf_y,1.69))
    put('Cabinet',(3.1,-1.65,.5))
    put('DrawerCabinet',(-3.3,-1.0,.5))
    for x in (2.73,3.47): put('CabinetDoor',(x,-2,.52)); put('Handle',(x,-2.05,.62))
    for z in (.45,.68,.91): put('Drawer',(-3.3,-1.3,z)); put('Handle',(-3.3,-1.36,z))
    put('ToolBox',(3.0,-1.6,1.12)); put('Dial',(3.35,-1.6,1.06))
    put('PipeStraight',(-3.6,.7,1.6)); put('Vent',(-3.9,1.5,.7),(0,0,P/2))
    put('Bracket',(-3.6,.7,.82)); put('Bracket',(-3.6,.7,2.4))
    put('Plaque',(0,2.875,2.8),group='Architecture')
    put('AmberStrip',(1.9,2.86,2.7),group='Architecture')
    put('CalibrationBase',(2.9,.6,.94),group='Calibration')
    for name,x in [('Brass',2.55),('Chrome',2.9),('RoughSteel',3.25)]:
        # Coupons share topology but preserve material boundaries.
        key='Coupon'+name; templates[key]=cylinder(key,.12,.065,mats[name],64)
        put(key,(x,.6,1.03),group='Calibration')
    put('SmallPlaque',(2.9,.4,.99),(P/2,0,0),group='Calibration')
    # Small fixtures use zero-radius point lights; emission displays are separate sources.
    for i,(x,y) in enumerate(((-2,-1),(0,-1),(2,-1),(-2,1),(0,1),(2,1),(-3,2),(3,2))):
        put('LampFixture',(x,y,3.25),group='Architecture'); put('LampStem',(x,y,3.53),group='Architecture')
        data=bpy.data.lights.new(f'Point{i:02d}','POINT'); data.energy=85 if i<6 else 45; data.shadow_soft_size=0
        data.color=(1,.84,.65) if i%2==0 else (.64,.82,1)
        obj=bpy.data.objects.new(f'Point{i:02d}',data); scene.collection.objects.link(obj)
        obj.location=(x,y,3.13); obj.parent=root; obj['id']='light:Standard:'+obj.name
    data=bpy.data.lights.new('Sun','SUN'); data.energy=1.2; data.angle=0
    sun=bpy.data.objects.new('Sun',data); scene.collection.objects.link(sun)
    sun.rotation_euler=(.45,-.55,-.55); sun['id']='light:Sun'
    shots={}
    for name,pos,target,fov in [('Hero',(6.8,-8.4,4.3),(0,.35,1.25),45),
        ('Materials',(3.9,-.3,1.9),(1.5,.1,1.25),45),('Occlusion',(-3.5,-2.4,1.7),(.5,.8,1.2),55),
        ('Transparency',(1.9,1.0,2.5),(.3,1.7,1.28),40),('MonitorCamera',(-2.3,-2.7,2.1),(.4,0,1.2),50)]:
        cam=camera(scene,name,pos,target,fov)
        shots[name]={'nodeId':cam['id'],'position':rh(pos),'target':rh(target),'fovDegrees':fov,'near':.05,'far':150}
    scene.camera=bpy.data.objects['Hero']
    # Remove unplaced prototype objects, keeping shared mesh datablocks.
    for obj in templates.values(): bpy.data.objects.remove(obj,do_unlink=True)
    stress=bpy.data.scenes.new('Stress'); render_settings(stress); stress.world=scene.world
    for row in range(3):
        for col in range(3):
            offset=Vector(((col-1)*9,(row-1)*7,0)); mapping={}
            for obj in list(scene.objects):
                if obj.type in ('CAMERA',) or obj==sun: continue
                clone=obj.copy(); stress.collection.objects.link(clone); mapping[obj]=clone
                clone.name=f'Bay{row}{col}_{obj.name}'; clone['id']=f'instance:Stress:{row}{col}:'+obj.name
            for original,clone in mapping.items():
                clone.parent=mapping.get(original.parent)
                if original.parent is None: clone.location=original.location+offset
    stress.collection.objects.link(sun)
    stress.camera=camera(stress,'StressHero',(19,-23,14),(0,0,1.1),55)
    shots['StressHero']={'nodeId':stress.camera['id'],'position':rh(stress.camera.location),'target':rh((0,0,1.1)),
        'fovDegrees':55,'near':.05,'far':150}
    keyframes=[(0,(1,-5.5,1.7),(0,.3,1.2)),(240,(1,-2.1,1.7),(0,.3,1.2)),
        (480,(2.8,-.2,1.6),(.2,0,1.3)),(720,(-2.9,.2,1.6),(.4,1.7,1.2)),
        (960,(-2.9,1.4,1.6),(1,.3,1.2)),(1080,(-2.9,2.3,1.7),(0,-.1,1.2)),
        (1200,(0,1.7,1.7),(0,-2,1.2))]
    config={'profile':'workshop-scene-1','model':'../../Models/Workshop/Workshop.gltf',
        'coordinateSystem':'right-handed Y-up meters','defaultScene':'Standard','defaultPreset':'OpaqueMask',
        'presets':{'Opaque':['OPAQUE'],'OpaqueMask':['OPAQUE','MASK'],'Full':['OPAQUE','MASK','BLEND']},
        'cameras':shots,'exposure':1.0,'environment':{'prefix':'../../Environments/Workshop/WorkshopSky',
        'intensity':.8,'yawDegrees':0},'cameraPath':{'durationSeconds':20,'fps':60,'samples':1201,
        'interpolation':'linear position and target; frame 0..1200 inclusive','fovDegrees':50,
        'keyframes':[{'frame':f,'position':rh(p),'target':rh(t)} for f,p,t in keyframes]},
        'roles':{'monitor':'instance:Standard:MonitorScreen_00','monitorCamera':'camera:MonitorCamera',
        'particleEmitter':{'position':rh((-.8,1.85,1.2)),'seed':0}},
        'reference':{'width':1280,'height':720,'device':'CPU','samples':512,'seed':0,'denoise':False,
        'display':'linear RGB * exposure -> Reinhard per channel -> exact sRGB OETF',
        'beautyDisplay':'AgX Medium High Contrast, exposure 0, denoise enabled'}}
    scene['workshop_config']=json.dumps(config)
    bpy.context.window.scene=scene
    bpy.ops.wm.save_as_mainfile(filepath=str(destination/'Workshop.blend'))
    bpy.ops.wm.read_factory_settings(use_empty=True)
    env=bpy.context.scene; env.name='WorkshopSky'; render_settings(env,256,False)
    env.world=world(); env.world.node_tree.nodes['Environment'].inputs['Strength'].default_value=1
    cam=camera(env,'Panorama',(0,0,0),(0,1,0)); cam.data.type='PANO'; cam.data.panorama_type='EQUIRECTANGULAR'
    env.camera=cam; env.render.resolution_x,env.render.resolution_y=2048,1024
    bpy.ops.wm.save_as_mainfile(filepath=str(destination/'WorkshopSky.blend'))
    print('Saved new Workshop and WorkshopSky authoring sources',flush=True)

if __name__=='__main__': main()
