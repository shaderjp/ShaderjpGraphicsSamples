"""Deterministic, editable BindlessGallery authoring helpers. MIT License."""
from pathlib import Path
import hashlib
import json
import math
import sys
import bpy
import numpy as np
from mathutils import Vector

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'Workshop'))
from workshop_tools import (arguments, json_write, rh, camera, render_settings, world,
                            box, cylinder, torus, rotor, housing, material, write_png, projected_uv)

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def texture_catalog(directory):
    """400 distinct, meaningful images; four colorways share a physical surface."""
    size = 512
    y, x = np.mgrid[:size, :size].astype(np.float64) / size
    images = {}
    palette = np.array([[.27,.30,.32], [.75,.73,.65], [.06,.38,.38], [.70,.25,.07],
                        [.055,.105,.15], [.58,.62,.65], [.8,.57,.16], [.16,.18,.19]])
    for semantic, count in [('Albedo',256), ('Normal',64), ('ORM',64), ('Emissive',16)]:
        for i in range(count):
            a = np.ones((size,size,4), np.float64)
            surface = i % 64
            grain = np.sin(x*math.tau*(31+surface))*np.sin(y*math.tau*(29+surface))
            seam = (np.mod(x*4,1)<.018) | (np.mod(y*4,1)<.018)
            if semantic == 'Albedo':
                color = palette[(surface//4) % len(palette)].copy()
                color *= 1 + (i//64-1.5)*.035
                a[...,:3] = color*(.95+.025*grain)[...,None]
                a[seam,:3] *= .65
                # A readable binary serial and a stripe are authored image detail.
                for bit in range(8):
                    if (i>>bit)&1:
                        a[28:42, 24+bit*13:32+bit*13,:3] = (.85,.67,.24)
                a[470:479,24:125+(i%64)*4,:3] = (.07,.5,.47)
                if i%16 == 15:
                    a[...,3] = ((np.mod(x*12,1)<.16)|(np.mod(y*12,1)<.16)).astype(float)
            elif semantic == 'Normal':
                h = grain*.00003 + np.sin(y*math.tau*(9+i))*.00013
                dx = (np.roll(h,-1,1)-np.roll(h,1,1))*size
                dy = (np.roll(h,-1,0)-np.roll(h,1,0))*size
                n = np.stack((-dx,-dy,np.ones_like(x)),axis=-1)
                n /= np.linalg.norm(n,axis=-1,keepdims=True)
                a[...,:3] = n*.5+.5
            elif semantic == 'ORM':
                a[...,0] = 1
                a[...,1] = np.clip(.16+(i%8)*.09+grain*.018,.04,.96)
                a[...,2] = .88 if (i//4)%3 else .04
                # Per-surface roughness bands survive quantization.
                a[seam,1] = .7+i*.003
                if i==6:
                    a[...,1] = .045+grain*.004
                    a[...,2] = 1
            else:
                a[...,:3] = 0
                band = (y>.91)&(y<.925)&(x>.06)&(x<.22+i*.035)
                ticks = (np.mod(x*(8+i),1)<.16)&(y>.84)&(y<.875)
                a[band|ticks,:3] = (.03+i*.012,.24+i*.025,.34+i*.031)
            path = directory/f'{semantic}_{i:03d}.png'
            write_png(path,np.rint(np.clip(a,0,1)*255).astype(np.uint8))
            image = bpy.data.images.load(str(path),check_existing=False)
            image.name = path.stem
            image.colorspace_settings.name = 'sRGB' if semantic in ('Albedo','Emissive') else 'Non-Color'
            image.pack()
            images[semantic,i] = image
    return images

def join_parts(name, parts, mat):
    bpy.ops.object.select_all(action='DESELECT')
    for part in parts:
        part.select_set(True)
    bpy.context.view_layer.objects.active = parts[0]
    bpy.ops.object.join()
    obj = parts[0]
    bpy.ops.object.transform_apply(location=False,rotation=True,scale=True)
    # All prototypes have local-space vertices and an origin at ground center.
    matrix = obj.matrix_world.copy()
    obj.data.transform(matrix)
    obj.matrix_world.identity()
    projected_uv(obj.data)
    obj.name = obj.data.name = name
    obj.data.materials.clear()
    obj.data.materials.append(mat)
    for p in obj.data.polygons:
        p.material_index = 0
    obj.data['id'] = 'geometry:'+name
    return obj

def prototype_catalog(mat):
    result = []
    sizes = [(9.8,9.8,.12),(9.6,.15,2.8),(.18,.18,3.3),(9.6,.12,.16),
             (1.8,.9,.13),(.08,.08,2.4),(1.4,.035,1.2),(.9,.7,.3)]
    for i,s in enumerate(sizes):
        result.append(box(f'G{i:02d}_'+['Floor','Wall','Column','Beam','Shelf','Upright','Mirror','Plinth'][i],s,mat))
    # Eight recognizable machines. Geometry is shared by all placed copies.
    for i in range(8):
        parts=[]
        base=box('Base',(1.65,1.2,.16),mat); base.location.z=.12; parts.append(base)
        if i in (0,1):
            shell=housing('Casing',mat); shell.scale=(.8,.8,.8); shell.location.z=1; parts.append(shell)
            wheel=rotor('Blades',mat,i==1); wheel.scale=(.75,.75,.75); wheel.location=(.65,0,1); parts.append(wheel)
            shaft=cylinder('Shaft',.07,1.9,mat,32); shaft.rotation_euler.y=math.pi/2; shaft.location.z=1; parts.append(shaft)
        elif i in (2,3):
            for j in range(3):
                wheel=torus('Gear',.18+j*.08,.055,mat,48,12)
                wheel.rotation_euler.x=math.pi/2; wheel.location=(-.5+j*.48,0,.65+j*.13); parts.append(wheel)
                for tooth in range(12):
                    angle=tooth*math.tau/12
                    p=box('Tooth',(.09,.14,.1),mat,.01)
                    p.location=(wheel.location.x+(.24+j*.08)*math.cos(angle),0,wheel.location.z+(.24+j*.08)*math.sin(angle))
                    p.rotation_euler.y=-angle; parts.append(p)
        elif i in (4,5):
            for j,(x,z,a) in enumerate([(-.4,.55,.3),(-.1,1.1,-.6),(.4,1.5,.9)]):
                p=box('Arm',(.24,.3,.7),mat); p.location=(x,0,z); p.rotation_euler.y=a; parts.append(p)
                p=cylinder('Joint',.18,.38,mat,32); p.rotation_euler.x=math.pi/2; p.location=(x,0,z+.25); parts.append(p)
        else:
            for j in range(4):
                p=cylinder('Chamber',.13,.8+j*.12,mat,32); p.location=(-.55+j*.36,0,.65); parts.append(p)
                p=torus('Pipe',.2,.045,mat,48,12); p.rotation_euler.x=math.pi/2; p.location=(-.55+j*.36,0,1.2+j*.12); parts.append(p)
        result.append(join_parts(f'G{len(result):02d}_'+['Turbine','Compressor','GearTrain','Transmission','RobotArm','Actuator','Pump','HeatExchanger'][i],parts,mat))
    # 48 kit pieces include a MASK panel every sixteenth material slot.
    while len(result)<64:
        i=len(result)
        if i%16==15:
            obj=box(f'G{i:02d}_MaskGrille',(1.3,.025,.9),mat,0)
        elif i%4==0:
            obj=box(f'G{i:02d}_Cabinet',(.6+(i%3)*.12,.45,.55+(i%5)*.1),mat)
        elif i%4==1:
            obj=cylinder(f'G{i:02d}_Valve',.10+(i%5)*.035,.25+(i%7)*.065,mat,24+(i%4)*8)
        elif i%4==2:
            obj=torus(f'G{i:02d}_Bearing',.12+(i%6)*.035,.035+(i%3)*.008,mat,48,12)
        else:
            obj=box(f'G{i:02d}_Component',(.16+(i%5)*.07,.22,.18+(i%7)*.08),mat)
        result.append(obj)
    for i,obj in enumerate(result):
        obj.data['id']=f'geometry:{i:02d}'
        obj.data['catalogIndex']=i
    return result

def local_placement(slot):
    if slot==0: return (0,0,-.06),(0,0,0)
    if slot==1: return (0,4.8,1.4),(0,0,0)
    if slot==2: return (-4.6,4.6,1.65),(0,0,0)
    if slot==3: return (0,4.6,3.2),(0,0,0)
    if slot==4: return (0,3.7,1.4),(0,0,0)
    if slot==5: return (3.8,3.7,1.2),(0,0,0)
    if slot==6: return (-4,1,1.5),(0,0,.35)
    if slot==7: return (0,0,.15),(0,0,0)
    if slot<16: return (-3.1+((slot-8)%4)*2.1,-2.1+((slot-8)//4)*2.8,0),(0,0,0)
    if slot%16==15: return (3.5,-1+(slot//16)*1.3,1.0),(0,0,0)
    return (-3.9+((slot-16)%12)*.64,3.65,1.65+((slot-16)//12)*.36),(0,0,(slot%4)*.15)
