"""Silver Robot authoring helpers. MIT License, Copyright (c) 2026 masafumi."""
from pathlib import Path
import argparse
import json
import math
import sys

import bpy
from mathutils import Matrix, Quaternion, Vector

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
ASSETS = ROOT / 'Assets/Models/SilverRobot'
IMAGES = ROOT / 'Docs/Assets/Images/SilverRobot'
V = Vector


def arguments(parser):
    return parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])


def write_json(path, data):
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    Path(path).write_text(json.dumps(data, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')


def material(name, color, metallic, roughness, emission=None):
    m = bpy.data.materials.new(name)
    m.diffuse_color = (*color, 1)
    m.use_nodes = True
    p = m.node_tree.nodes.get('Principled BSDF')
    p.inputs['Base Color'].default_value = (*color, 1)
    p.inputs['Metallic'].default_value = metallic
    p.inputs['Roughness'].default_value = roughness
    if emission:
        p.inputs['Emission Color'].default_value = (*emission, 1)
        p.inputs['Emission Strength'].default_value = 1.0
    return m


class Geometry:
    """One editable mesh with named part selection groups and explicit bone weights."""
    def __init__(self):
        self.vertices, self.faces, self.materials, self.weights, self.parts = [], [], [], [], {}

    def add(self, name, verts, faces, mat, weights):
        start = len(self.vertices)
        self.vertices.extend(verts)
        self.faces.extend(tuple(start + i for i in face) for face in faces)
        self.materials.extend([mat] * len(faces))
        self.weights.extend(weights if isinstance(weights, list) else [weights.copy() for _ in verts])
        self.parts[name] = list(range(start, len(self.vertices)))

    def loft(self, name, rings, mat, bone, segments=24, angular=None, thickness=0.004):
        # Rings: center, transverse unit vector, depth unit vector, two radii.
        # Full loops are closed at both ends. Partial shells have a closed inner wall.
        partial = angular is not None
        lo, hi = angular or (0, 2 * math.pi)
        count = segments + 1 if partial else segments
        verts, weights = [], []
        layers = (0, thickness) if partial else (0,)
        for inset in layers:
            for j, (c, u, d, rx, ry) in enumerate(rings):
                for i in range(count):
                    a = lo + (hi - lo) * i / segments
                    p = V(c) + V(u) * ((rx - inset) * math.sin(a)) + V(d) * ((ry - inset) * math.cos(a))
                    verts.append(tuple(p))
                    weights.append(bone(p, j / max(1, len(rings) - 1)) if callable(bone) else {bone: 1.0})
        faces = []
        nr = len(rings)
        for layer in range(len(layers)):
            off = layer * nr * count
            for j in range(nr - 1):
                for i in range(segments):
                    nxt = i + 1 if partial else (i + 1) % count
                    face = (off + j * count + i, off + (j + 1) * count + i,
                            off + (j + 1) * count + nxt, off + j * count + nxt)
                    faces.append(face if layer == 0 else face[::-1])
        if partial:
            off = nr * count
            for j in (0, nr - 1):
                for i in range(segments):
                    a, b = j * count + i, j * count + i + 1
                    faces.append((a, b, off + b, off + a) if j == 0 else (b, a, off + a, off + b))
            for i in (0, count - 1):
                for j in range(nr - 1):
                    a, b = j * count + i, (j + 1) * count + i
                    faces.append((a, off + a, off + b, b) if i == 0 else (b, off + b, off + a, a))
        else:
            # Perimeter goes clockwise when viewed from positive longitudinal axis.
            faces.append(tuple(range(count)))
            faces.append(tuple((nr - 1) * count + i for i in range(count - 1, -1, -1)))
        self.add(name, verts, faces, mat, weights)

    def segment(self, name, a, b, profile, mat, bone, segments=24, angular=None):
        a, b = V(a), V(b)
        axis = (b - a).normalized()
        d = V((0, -1, 0))
        if abs(axis.dot(d)) > .95:
            d = V((0, 0, 1))
        d = (d - axis * axis.dot(d)).normalized()
        u = d.cross(axis).normalized()
        rings = [(a.lerp(b, t), u, d, rx, ry) for t, rx, ry in profile]
        self.loft(name, rings, mat, bone, segments, angular)

    def ellipsoid(self, name, center, scale, mat, bone, segments=24, rows=12):
        cx, cy, cz = center
        sx, sy, sz = scale
        rings = []
        # Very small capped end rings avoid coincident UV-sphere poles.
        for i in range(rows + 1):
            a = .008 + (math.pi - .016) * i / rows
            rings.append(((cx, cy, cz - sz * math.cos(a)), (1, 0, 0), (0, -1, 0),
                          sx * math.sin(a), sy * math.sin(a)))
        self.loft(name, rings, mat, bone, segments)

    def axle(self, name, center, radius, width, bone, axis=(1, 0, 0), segments=24):
        c, axis = V(center), V(axis).normalized()
        d = axis.orthogonal().normalized()
        u = d.cross(axis).normalized()
        def cylinder(label, start, end, r, mat):
            profile = [(start, r * .88), (start + .0015, r), (end - .0015, r), (end, r * .88)]
            self.loft(label, [(c + axis * t, u, d, rr, rr) for t, rr in profile], mat, bone, segments)
        cylinder(name + '.Housing', -width / 2, width / 2, radius, 1)
        for sign in (-1, 1):
            z = sign * width / 2
            cylinder(name + '.Rim' + str(sign), z - .002, z + .002, radius * .82, 2)
            cylinder(name + '.Cap' + str(sign), z - .0026, z + .0026, radius * .62, 3)

    def finish(self, collection, rig):
        mesh = bpy.data.meshes.new('SilverRobot.Geometry')
        mesh.from_pydata(self.vertices, [], self.faces)
        mesh.update()
        obj = bpy.data.objects.new('SilverRobot', mesh)
        collection.objects.link(obj)
        for name in ('PearlSilver', 'JointGraphite', 'BrushedTitanium', 'InsetBlack', 'VisorCyan'):
            mesh.materials.append(bpy.data.materials[name])
        for p, mat in zip(mesh.polygons, self.materials):
            p.material_index, p.use_smooth = mat, len(p.vertices) == 4
        groups = {b.name: obj.vertex_groups.new(name=b.name) for b in rig.data.bones}
        for i, weights in enumerate(self.weights):
            assert 1 <= len(weights) <= 4 and abs(sum(weights.values()) - 1) < 1e-6
            for name, weight in weights.items():
                if weight > 0:
                    groups[name].add([i], weight, 'REPLACE')
        for name, indices in self.parts.items():
            obj.vertex_groups.new(name='Part.' + name).add(indices, 1, 'REPLACE')
        bpy.context.view_layer.objects.active = obj
        obj.select_set(True)
        # Recalculate normals to make all disconnected closed armor surfaces outward.
        bpy.ops.object.mode_set(mode='EDIT')
        bpy.ops.mesh.select_all(action='SELECT')
        bpy.ops.mesh.normals_make_consistent(inside=False)
        bpy.ops.object.mode_set(mode='OBJECT')
        # Preserve smooth quad-surface normals while storing explicit triangles.
        # Recomputing after triangulation produces diagonal highlight artifacts.
        mesh.normals_split_custom_set([n.vector for n in mesh.corner_normals])
        tri = obj.modifiers.new('Export triangles', 'TRIANGULATE')
        tri.keep_custom_normals = True
        bpy.ops.object.modifier_apply(modifier=tri.name)
        mod = obj.modifiers.new('Linear blend skinning', 'ARMATURE')
        mod.object = rig
        mod.use_deform_preserve_volume = False
        obj.parent = rig
        obj['license'] = 'MIT; Copyright (c) 2026 masafumi'
        obj['description'] = 'Original silver humanoid, A-pose bind, rigid armor and blended flexible joints'
        return obj


def bone_layout():
    bones = []
    def add(name, a, b, parent):
        bones.append((name, V(a), V(b), parent))
    add('Root', (0, 0, 0), (0, 0, .12), None)
    add('Hips', (0, 0, 1.01), (0, 0, 1.12), 'Root')
    add('Spine', (0, 0, 1.12), (0, 0, 1.25), 'Hips')
    add('Spine1', (0, 0, 1.25), (0, 0, 1.40), 'Spine')
    add('Chest', (0, 0, 1.40), (0, 0, 1.565), 'Spine1')
    add('Neck', (0, 0, 1.565), (0, 0, 1.69), 'Chest')
    add('Head', (0, 0, 1.69), (0, 0, 1.90), 'Neck')
    for side, s in (('L', 1), ('R', -1)):
        n = lambda name: name + '.' + side
        shoulder, elbow, wrist = V((s * .257, 0, 1.54)), V((s * .443, 0, 1.292)), V((s * .595, -.008, 1.047))
        hand_dir = (wrist - elbow).normalized()
        hand_tip = wrist + hand_dir * .105
        add(n('Shoulder'), (s * .075, 0, 1.55), shoulder, 'Chest')
        add(n('UpperArm'), shoulder, elbow, n('Shoulder'))
        add(n('ForeArm'), elbow, wrist, n('UpperArm'))
        add(n('Hand'), wrist, hand_tip, n('ForeArm'))
        add(n('UpperLeg'), (s * .115, 0, 1.01), (s * .130, -.012, .575), 'Hips')
        add(n('LowerLeg'), (s * .130, -.012, .575), (s * .135, 0, .13), n('UpperLeg'))
        add(n('Foot'), (s * .135, 0, .13), (s * .135, -.175, .075), n('LowerLeg'))
        add(n('Toes'), (s * .135, -.175, .075), (s * .135, -.278, .070), n('Foot'))
        u = V((s * .85, 0, .527)).normalized()
        for finger, offset, length in (('Index', -.025, .078), ('Middle', -.008, .088), ('Ring', .010, .082), ('Little', .027, .064)):
            a = wrist + hand_dir * (.087 if finger != 'Little' else .078) + u * offset
            d = (hand_dir + u * (offset * 2.0)).normalized()
            for i, ratio in enumerate((.43, .32, .25), 1):
                b = a + d * length * ratio
                add(n(finger + str(i)), a, b, n('Hand') if i == 1 else n(finger + str(i - 1)))
                a = b
        a = wrist + hand_dir * .038 - u * .041
        d = (hand_dir * .65 - u * .65 + V((0, -.18, 0))).normalized()
        for i, length in enumerate((.025, .028, .024), 1):
            b = a + d * length
            add(n('Thumb' + str(i)), a, b, n('Hand') if i == 1 else n('Thumb' + str(i - 1)))
            a = b
    return bones


def create_rig(collection):
    data = bpy.data.armatures.new('Humanoid.Apose')
    rig = bpy.data.objects.new('SilverRobotRig', data)
    collection.objects.link(rig)
    bpy.context.view_layer.objects.active = rig
    rig.select_set(True)
    bpy.ops.object.mode_set(mode='EDIT')
    for name, a, b, parent in bone_layout():
        bone = data.edit_bones.new(name)
        bone.head, bone.tail = a, b
        if parent:
            bone.parent = data.edit_bones[parent]
        bone.align_roll(V((0, -1, 0)))
    bpy.ops.object.mode_set(mode='OBJECT')
    rig.show_in_front = True
    data.display_type = 'OCTAHEDRAL'
    for pb in rig.pose.bones:
        pb.rotation_mode = 'QUATERNION'
    rig['bind_pose'] = 'A-pose; Blender +Z up, -Y forward, +X character left; metres'
    rig['retargeting'] = 'Use Humanoid.json; source bone names and rest rotations must be mapped, not simply copied'
    for label, predicate in (('Body', lambda n: '.' not in n), ('Arms', lambda n: any(x in n for x in ('Shoulder.', 'UpperArm.', 'ForeArm.', 'Hand.'))),
                             ('Legs', lambda n: any(x in n for x in ('UpperLeg.', 'LowerLeg.', 'Foot.', 'Toes.'))),
                             ('Fingers', lambda n: any(x in n for x in ('Thumb', 'Index', 'Middle', 'Ring', 'Little')))):
        bc = data.collections.new(label)
        for bone in data.bones:
            if predicate(bone.name):
                bc.assign(bone)
    return rig


def model_geometry(rig):
    g = Geometry()
    b = {name: (a, z) for name, a, z, _ in bone_layout()}
    def vertical(name, profile, mat, bone, angular=None, segments=32):
        g.loft(name, [((0, y, z), (1, 0, 0), (0, -1, 0), rx, ry) for z, rx, ry, y in profile], mat, bone, segments, angular)
    def torso_weights(p, t):
        levels = ((1.03, 'Hips'), (1.17, 'Spine'), (1.32, 'Spine1'), (1.48, 'Chest'))
        for (z0, n0), (z1, n1) in zip(levels, levels[1:]):
            if z0 <= p.z <= z1:
                k = (p.z - z0) / (z1 - z0)
                return {n0: 1 - k, n1: k}
        return {levels[0 if p.z < 1.03 else -1][1]: 1}
    vertical('Torso.Flexible', [(1.015,.128,.080,0),(1.075,.137,.083,0),(1.15,.12,.08,0),(1.24,.14,.085,0),
                               (1.34,.174,.103,0),(1.44,.197,.102,0),(1.535,.174,.084,0)], 1, torso_weights)
    # Independent abdominal lames leave graphite seams visible under bending.
    for i, (z, rx, ry, owner) in enumerate(((1.116,.123,.087,'Spine'),(1.19,.133,.092,'Spine'),(1.265,.15,.1,'Spine1'))):
        vertical('Abdomen.Plate' + str(i), [(z-.028,rx*.94,ry,0),(z,rx,ry+.003,0),(z+.025,rx*.99,ry,0)], 3, owner, (-1.30,1.30), 24)
    chest = [(1.285,.148,.107,0),(1.31,.175,.13,0),(1.39,.215,.137,0),(1.475,.223,.125,0),
             (1.54,.218,.111,0),(1.58,.155,.087,0),(1.60,.093,.071,0)]
    for side, s in (('L',1),('R',-1)):
        lo, hi = (.023,1.49) if s == 1 else (-1.49,-.023)
        vertical('Chest.Armor.'+side,chest,0,'Chest',(lo,hi),28)
        # Curved lower edge frames the abdomen instead of ending in a horizontal box.
        for index in g.parts['Chest.Armor.'+side]:
            x,y,z=g.vertices[index]
            factor=max(0,1-(z-1.285)/.115)
            g.vertices[index]=(x,y,z+.053*max(0,1-abs(x)/.22)*factor)
        lo, hi = (1.58,math.pi-.028) if s == 1 else (math.pi+.028,4.70)
        back = [(z, rx*.99, ry*.83, .008) for z,rx,ry,y in chest]
        vertical('Back.Armor.'+side,back,0,'Chest',(lo,hi),24)
    vertical('Pelvis.Core',[(.945,.13,.075,0),(.99,.155,.095,0),(1.06,.167,.100,0),(1.096,.144,.087,0)],1,'Hips')
    vertical('Pelvis.Front',[(.95,.072,.073,-.034),(.974,.098,.095,-.022),(1.027,.151,.115,-.005),
                           (1.071,.164,.108,0),(1.093,.145,.091,0)],0,'Hips',(-1.1,1.1),28)
    vertical('Pelvis.Back',[(.977,.127,.082,.007),(1.01,.155,.10,.007),(1.067,.164,.102,0),(1.09,.144,.090,0)],0,'Hips',(2.02,4.26),24)
    def neck_weights(p,t):
        k = max(0,min(1,(p.z-1.565)/.105))
        return {'Neck':1-k,'Head':k}
    vertical('Neck.Flexible',[(1.56,.058,.055,0),(1.6,.054,.055,0),(1.65,.055,.055,0),(1.695,.065,.060,0)],1,neck_weights,segments=24)
    for i in range(4):
        z = 1.58 + i*.024
        vertical('Neck.Collar'+str(i),[(z,.059,.059,0),(z+.003,.061,.061,0),(z+.007,.059,.059,0)],2,'Neck',segments=24)
    head = [(1.685,.040,.043,-.017),(1.697,.065,.058,-.01),(1.73,.088,.076,-.003),
            (1.779,.106,.086,0),(1.82,.110,.087,.006),(1.856,.102,.082,.008),
            (1.884,.080,.067,.008),(1.901,.041,.041,.006),(1.908,.006,.006,.005)]
    vertical('Helmet.Main',head,0,'Head',segments=40)
    vertical('Helmet.VisorInset',[(1.786,.108,.089,.001),(1.792,.109,.090,.002),(1.801,.111,.091,.003)],3,'Head',(-1.08,1.08),36)
    vertical('Helmet.VisorLight',[(1.791,.110,.092,.001),(1.794,.1105,.0925,.001),(1.796,.111,.0925,.002)],4,'Head',(-1.05,1.05),36)
    for side,s in (('L',1),('R',-1)):
        g.axle('Helmet.Ear.'+side,(s*.106,.018,1.80),.045,.018,'Head',segments=32)
    # Narrow rear spinal spine plates and vents create a finished back view.
    for i in range(5):
        z = 1.125 + i*.073
        g.ellipsoid('Back.SpineLink'+str(i),(0,.091 if i<3 else .115,z),(.032,.014,.028),2,'Spine' if i<2 else ('Spine1' if i<3 else 'Chest'),16,6)
    for side,s in (('L',1),('R',-1)):
        n = lambda name: name+'.'+side
        a,e = b[n('UpperArm')]
        _,w = b[n('ForeArm')]
        # White shoulder cover, smaller than the torso to retain humanoid proportions.
        g.ellipsoid(n('Shoulder.Flexible'),a,(.073,.071,.072),1,n('UpperArm'))
        hood=[]
        for i in range(11):
            angle=.14+(math.pi-.28)*i/10
            hood.append((a+V((s*.017,0,-.080*math.cos(angle)+.008)),(1,0,0),(0,-1,0),
                         .081*math.sin(angle),.079*math.sin(angle)))
        g.loft(n('Shoulder.OuterCap'),hood,0,n('UpperArm'),28,
               (.04,math.pi-.04) if s==1 else (-math.pi+.04,-.04))
        g.segment(n('Shoulder.Armor'),a,e,[(.02,.035,.052),(.075,.066,.073),(.19,.079,.079),
                                                (.31,.074,.077),(.38,.058,.062),(.405,.028,.035)],0,n('UpperArm'),32)
        g.segment(n('UpperArm.Core'),a,e,[(.22,.044,.048),(.76,.044,.043),(.91,.032,.033)],1,n('UpperArm'))
        g.segment(n('UpperArm.Armor'),a,e,[(.39,.054,.057),(.42,.059,.060),(.57,.061,.063),
                                               (.77,.052,.056),(.88,.040,.043),(.9,.034,.038)],0,n('UpperArm'),28)
        g.axle(n('Elbow'),e,.041,.079,n('ForeArm'))
        g.segment(n('ForeArm.Core'),e,w,[(.08,.031,.033),(.9,.026,.029)],1,n('ForeArm'))
        g.segment(n('ForeArm.Armor'),e,w,[(.14,.035,.041),(.17,.048,.051),(.30,.055,.056),
                                              (.57,.048,.053),(.80,.032,.035),(.90,.028,.031)],0,n('ForeArm'),28)
        # Blended rubber bellows cross elbow and wrist; visible through armor gaps.
        for label, center, direction, radius, prev, nxt in (
            ('Elbow.Bellows',e,(w-a).normalized(),.032,n('UpperArm'),n('ForeArm')),
            ('Wrist.Bellows',w,(w-e).normalized(),.023,n('ForeArm'),n('Hand'))):
            weights = lambda p,t,prev=prev,nxt=nxt: {prev:1-t,nxt:t}
            g.segment(n(label),center-direction*.021,center+direction*.021,
                      [(i/6,radius*(1+.12*(i%2)),radius*(1+.12*(i%2))) for i in range(7)],1,weights,20)
        g.axle(n('Wrist'),w,.027,.05,n('Hand'))
        ha,hb = b[n('Hand')]
        g.segment(n('Hand.Core'),ha,hb,[(.15,.028,.019),(.25,.037,.022),(.73,.038,.024),(.96,.026,.02)],1,n('Hand'),20)
        g.segment(n('Hand.Dorsal'),ha+V((0,.012,0)),hb+V((0,.012,0)),
                  [(.19,.027,.013),(.27,.038,.016),(.71,.038,.017),(.85,.032,.015)],0,n('Hand'),24,(-math.pi/2,math.pi/2))
        for finger in ('Thumb','Index','Middle','Ring','Little'):
            for i in range(1,4):
                fn=n(finger+str(i)); fa,fb=b[fn]
                radius = .0085 if finger=='Little' else .0095
                g.ellipsoid(fn+'.Joint',fa,(radius,radius,radius),1,fn,12,6)
                g.segment(fn+'.Shell',fa,fb,[(.14,radius*.75,radius*.75),(.23,radius,radius),
                                           (.76,radius*.88,radius*.87),(.91,radius*.60,radius*.65)],2,fn,12)
        hip,knee=b[n('UpperLeg')]; _,ankle=b[n('LowerLeg')]
        g.ellipsoid(n('Hip.Flexible'),hip,(.087,.081,.083),1,n('UpperLeg'))
        g.axle(n('Hip'),hip,.072,.14,n('UpperLeg'))
        g.segment(n('Thigh.Core'),hip,knee,[(.07,.065,.069),(.88,.05,.05)],1,n('UpperLeg'))
        g.segment(n('Thigh.Armor'),hip,knee,[(.10,.056,.066),(.15,.078,.086),(.34,.087,.095),
                                                  (.58,.081,.087),(.82,.060,.063),(.9,.044,.049)],0,n('UpperLeg'),32)
        g.axle(n('Knee'),knee,.050,.11,n('LowerLeg'),segments=32)
        g.ellipsoid(n('Knee.Pad'),knee+V((0,-.043,.003)),(.034,.025,.045),0,n('LowerLeg'),24,10)
        weights = lambda p,t: {n('UpperLeg'):1-t,n('LowerLeg'):t}
        g.segment(n('Knee.Bellows'),knee+V((0,0,.027)),knee-V((0,0,.027)),
                  [(i/6,.037*(1+.13*(i%2)),.04*(1+.13*(i%2))) for i in range(7)],1,weights,24)
        g.segment(n('Shin.Core'),knee,ankle,[(.10,.042,.045),(.93,.034,.033)],1,n('LowerLeg'))
        g.segment(n('Shin.Armor'),knee,ankle,[(.12,.043,.052),(.17,.064,.072),(.32,.072,.082),
                                                (.55,.059,.068),(.81,.040,.043),(.93,.036,.038)],0,n('LowerLeg'),32)
        g.segment(n('Shin.RearInset'),knee+V((0,.046,0)),ankle+V((0,.034,0)),
                  [(.61,.010,.015),(.63,.014,.017),(.91,.011,.016),(.93,.006,.010)],3,n('LowerLeg'),16)
        g.axle(n('Ankle'),ankle,.039,.075,n('Foot'))
        # Foot built as vertical rounded rings, not an ellipsoid: stable flat sole.
        def foot_rings(profile):
            return [((s*.135,y,z),(1,0,0),(0,-1,0),rx,ry) for z,rx,ry,y in profile]
        g.loft(n('Foot.Sole'),foot_rings([(0,.052,.136,-.103),(.021,.062,.150,-.103),
                                            (.038,.061,.148,-.103),(.044,.055,.143,-.103)]),3,n('Foot'),32)
        g.loft(n('Foot.Armor'),foot_rings([(.04,.054,.141,-.103),(.048,.060,.142,-.10),(.075,.058,.132,-.09),
                                             (.111,.050,.101,-.063),(.135,.039,.058,-.022),(.151,.027,.030,-.004)]),0,n('Foot'),32)
        g.loft(n('Foot.ToeCap'),foot_rings([(.035,.049,.050,-.218),(.044,.054,.055,-.219),
                                              (.066,.051,.049,-.216),(.083,.040,.034,-.206)]),0,n('Toes'),28)
    return g.finish(bpy.data.collections['EXPORT'],rig)


def reset_pose(rig):
    for pb in rig.pose.bones:
        pb.matrix_basis = Matrix.Identity(4)


def world_rotation(rig, name, axis, angle):
    pb = rig.pose.bones[name]
    local_axis = pb.bone.matrix_local.to_3x3().inverted() @ V(axis)
    pb.rotation_quaternion = Quaternion(local_axis, angle)


def aim(rig, name, head, tail):
    pb = rig.pose.bones[name]
    rest = pb.bone
    q = (rest.tail_local-rest.head_local).rotation_difference(V(tail)-V(head))
    pb.matrix = Matrix.Translation(V(head)) @ q.to_matrix().to_4x4() @ rest.matrix_local.to_3x3().to_4x4()
    bpy.context.view_layer.update()


def create_actions(rig):
    rig.animation_data_create()
    actions = []
    for label,end in (('Bind_Apose',2),('Retarget_Tpose',2),('Idle',61),('Walk_InPlace',31)):
        action=bpy.data.actions.new(label)
        action.use_fake_user=True
        action.use_frame_range=True
        action.frame_start,action.frame_end=1,end
        rig.animation_data.action=action
        for frame in range(1,end+1):
            bpy.context.scene.frame_set(frame)
            reset_pose(rig)
            phase=(frame-1)/(end-1)
            if label=='Retarget_Tpose':
                for side,s in (('L',1),('R',-1)):
                    world_rotation(rig,'UpperArm.'+side,(0,1,0),-s*math.atan2(.248,.186))
                    world_rotation(rig,'ForeArm.'+side,(0,1,0),-s*(math.atan2(.245,.152)-math.atan2(.248,.186)))
            elif label in ('Idle','Walk_InPlace'):
                for side,s in (('L',1),('R',-1)):
                    world_rotation(rig,'UpperArm.'+side,(0,1,0),s*.49)
                    world_rotation(rig,'ForeArm.'+side,(1,0,0),-.10)
                if label=='Idle':
                    world_rotation(rig,'Spine1',(1,0,0),.012*math.sin(2*math.pi*phase))
                    world_rotation(rig,'Head',(0,0,1),.018*math.sin(2*math.pi*phase))
                else:
                    bob=-.034+.008*math.cos(4*math.pi*phase)
                    rig.pose.bones['Hips'].location.y=bob  # Hips local Y is world Z.
                    bpy.context.view_layer.update()
                    for side,s,offset in (('L',1,0),('R',-1,.5)):
                        t=(phase+offset)%1
                        if t < .6:
                            foot_y=-.135+.270*t/.6
                            lift=0
                        else:
                            swing=(t-.6)/.4
                            foot_y=.135*math.cos(math.pi*swing)
                            lift=.075*math.sin(math.pi*swing)
                        hip=V((s*.115,0,1.01+bob))
                        ankle=V((s*.135,foot_y,.13+lift))
                        l1=(rig.data.bones['UpperLeg.'+side].tail_local-rig.data.bones['UpperLeg.'+side].head_local).length
                        l2=(rig.data.bones['LowerLeg.'+side].tail_local-rig.data.bones['LowerLeg.'+side].head_local).length
                        d=ankle-hip; length=d.length
                        along=(l1*l1-l2*l2+length*length)/(2*length)
                        bend=math.sqrt(max(0,l1*l1-along*along))
                        forward=V((0,-1,0)); perpendicular=(forward-d.normalized()*forward.dot(d.normalized())).normalized()
                        knee=hip+d.normalized()*along+perpendicular*bend
                        aim(rig,'UpperLeg.'+side,hip,knee)
                        aim(rig,'LowerLeg.'+side,knee,ankle)
                        rest=rig.data.bones['Foot.'+side]
                        aim(rig,'Foot.'+side,ankle,ankle+rest.tail_local-rest.head_local)
                        # Opposite arm swing, with a subtle elbow bend.
                        name='UpperArm.'+side; pb=rig.pose.bones[name]
                        q=pb.rotation_quaternion.copy()
                        local=pb.bone.matrix_local.to_3x3().inverted()@V((1,0,0))
                        pb.rotation_quaternion=Quaternion(local,.25*math.sin(2*math.pi*(phase+offset)))@q
                    world_rotation(rig,'Chest',(0,0,1),.025*math.sin(2*math.pi*phase))
            bpy.context.view_layer.update()
            for pb in rig.pose.bones:
                pb.keyframe_insert('location',frame=frame,group=pb.name)
                pb.keyframe_insert('rotation_quaternion',frame=frame,group=pb.name)
                pb.keyframe_insert('scale',frame=frame,group=pb.name)
        # Mark actions in muted NLA tracks: available to export and easy to find in Blender.
        track=rig.animation_data.nla_tracks.new(); track.name=label
        track.strips.new(label,1,action)
        track.mute=True
        action['license']='Original MIT animation; not imported motion capture'
        actions.append(action)
    rig.animation_data.action=None
    reset_pose(rig)
    bpy.context.scene.frame_set(1)
    return actions


def mapping(rig):
    m={'hips':'Hips','spine':'Spine','chest':'Spine1','upperChest':'Chest','neck':'Neck','head':'Head'}
    for side,prefix in (('L','left'),('R','right')):
        for semantic,bone in (('Shoulder','Shoulder'),('UpperArm','UpperArm'),('LowerArm','ForeArm'),('Hand','Hand'),
                              ('UpperLeg','UpperLeg'),('LowerLeg','LowerLeg'),('Foot','Foot'),('Toes','Toes')):
            m[prefix+semantic]=bone+'.'+side
        for finger in ('Thumb','Index','Middle','Ring','Little'):
            labels=('Metacarpal','Proximal','Distal') if finger=='Thumb' else ('Proximal','Intermediate','Distal')
            for i,label in enumerate(labels,1):
                m[prefix+finger+label]=finger+str(i)+'.'+side
    return {'schema':'Shaderjp.Humanoid.1','units':'metres','authoringAxes':{'up':'+Z','forward':'-Y','left':'+X'},
            'gltfAxes':{'up':'+Y','forward':'+Z','left':'+X'},'restPose':'A','rootMotionBone':'Root','humanoid':m,
            'bones':[{'name':b.name,'parent':b.parent.name if b.parent else None,
                      'head':list(b.head_local),'tail':list(b.tail_local),
                      'restMatrixColumnMajor':[b.matrix_local[r][c] for c in range(4) for r in range(4)]} for b in rig.data.bones],
            'notes':['Humanoid semantics follow common engine/VRM labels; this is core glTF, not a VRM file.',
                     'Other-rig Actions require retargeting of rest rotations, hierarchy and proportions; name matching alone is insufficient.']}


def studio(collection):
    scene=bpy.context.scene
    floor_mat=material('StudioFloor',(.055,.071,.095),.05,.58)
    bpy.ops.mesh.primitive_plane_add(size=200,location=(0,0,0))
    floor=bpy.context.object; floor.name='StudioFloor'
    for c in list(floor.users_collection): c.objects.unlink(floor)
    collection.objects.link(floor); floor.data.materials.append(floor_mat)
    def camera(name,position,target,scale):
        data=bpy.data.cameras.new(name); obj=bpy.data.objects.new(name,data); collection.objects.link(obj)
        obj.location=position; obj.rotation_euler=(V(target)-obj.location).to_track_quat('-Z','Y').to_euler()
        data.type='ORTHO'; data.ortho_scale=scale; data.lens=70; return obj
    scene.camera=camera('Hero',(3,-6,2.4),(0,0,.98),3.65)
    camera('Front',(0,-6,.965),(0,0,.965),3.75)
    camera('Right',(-6,0,.965),(0,0,.965),3.75)
    camera('Back',(0,6,.965),(0,0,.965),3.75)
    camera('Walk',(3,-6,2.4),(0,0,.98),3.65)
    for name,location,energy,size,color in (
        ('Key',(-3,-4,5),650,4,(.89,.95,1)),('Fill',(3,-2,2.7),400,3,(.68,.84,1)),
        ('Rim',(1,3,4.5),1000,3,(1,.87,.73)),('Top',(-2,1,5),450,2,(1,1,1))):
        data=bpy.data.lights.new(name,'AREA');data.energy=energy;data.shape='DISK';data.size=size;data.color=color
        obj=bpy.data.objects.new(name,data);collection.objects.link(obj);obj.location=location
        obj.rotation_euler=(V((0,0,1))-obj.location).to_track_quat('-Z','Y').to_euler()
    world=bpy.data.worlds.new('StudioWorld');world.use_nodes=True
    world.node_tree.nodes['Background'].inputs['Color'].default_value=(.075,.095,.13,1)
    world.node_tree.nodes['Background'].inputs['Strength'].default_value=.35
    scene.world=world
    scene.render.engine='CYCLES';scene.cycles.samples=48;scene.cycles.use_denoising=True
    scene.cycles.device='CPU';scene.cycles.seed=0
    scene.render.resolution_x=1280;scene.render.resolution_y=720;scene.render.resolution_percentage=100
    scene.render.image_settings.file_format='PNG';scene.render.image_settings.color_mode='RGBA'
    scene.view_settings.view_transform='AgX'
    scene.render.fps=30


def export_assets(output):
    rig=bpy.data.objects.get('SilverRobotRig'); obj=bpy.data.objects.get('SilverRobot')
    if rig is None or obj is None: raise RuntimeError('Open saved SilverRobot.blend')
    if obj.parent!=rig or len([m for m in obj.modifiers if m.type=='ARMATURE' and m.object==rig])!=1:
        raise RuntimeError('Mesh must have exactly one correct armature modifier')
    if any(m.type!='ARMATURE' for m in obj.modifiers): raise RuntimeError('Apply geometry modifiers before export')
    if any(len(p.vertices)!=3 for p in obj.data.polygons): raise RuntimeError('Triangulate edited mesh before export')
    bone_indices={g.index for g in obj.vertex_groups if g.name in rig.data.bones}
    for v in obj.data.vertices:
        weights=[g.weight for g in v.groups if g.group in bone_indices and g.weight>1e-7]
        if not 1<=len(weights)<=4 or abs(sum(weights)-1)>1e-5: raise RuntimeError(f'Invalid skin weights at vertex {v.index}')
    output=Path(output).resolve();output.mkdir(parents=True,exist_ok=True)
    active=rig.animation_data.action; pose=rig.data.pose_position;frame=bpy.context.scene.frame_current
    basis={p.name:p.matrix_basis.copy() for p in rig.pose.bones}
    selected=list(bpy.context.selected_objects);active_object=bpy.context.view_layer.objects.active
    track_mutes=[t.mute for t in rig.animation_data.nla_tracks]
    try:
        rig.animation_data.action=None
        for t in rig.animation_data.nla_tracks:t.mute=True
        reset_pose(rig);rig.data.pose_position='POSE';bpy.context.scene.frame_set(1)
        bpy.ops.object.select_all(action='DESELECT');rig.select_set(True);obj.select_set(True)
        bpy.context.view_layer.objects.active=rig
        settings=dict(use_selection=True,export_skins=True,export_rest_position_armature=True,
                      export_armature_object_remove=True,
                      export_all_influences=False,export_influence_nb=4,export_def_bones=False,
                      export_animations=True,export_animation_mode='ACTIONS',export_anim_single_armature=True,
                      export_force_sampling=True,export_frame_step=1,export_reset_pose_bones=True,
                      export_anim_slide_to_zero=True,export_optimize_animation_size=False,
                      export_yup=True,export_normals=True,export_texcoords=False,export_tangents=False,
                      export_materials='EXPORT',export_cameras=False,export_lights=False,
                      export_extras=False,export_morph=False,export_leaf_bone=False)
        for fmt,suffix in (('GLTF_SEPARATE','.gltf'),('GLB','.glb')):
            result=bpy.ops.export_scene.gltf(filepath=str(output/('SilverRobot'+suffix)),export_format=fmt,**settings)
            if result!={'FINISHED'}:raise RuntimeError('glTF export failed')
        write_json(output/'Humanoid.json',mapping(rig))
    finally:
        rig.animation_data.action=active;rig.data.pose_position=pose
        for t,mute in zip(rig.animation_data.nla_tracks,track_mutes):t.mute=mute
        bpy.context.scene.frame_set(frame)
        for p in rig.pose.bones:p.matrix_basis=basis[p.name]
        bpy.ops.object.select_all(action='DESELECT')
        for ob in selected:ob.select_set(True)
        bpy.context.view_layer.objects.active=active_object
