"""Workshop authoring helpers. MIT License, Copyright (c) 2026 masafumi."""
from pathlib import Path
import json
import math
import sys
import bpy
import numpy as np
from mathutils import Vector

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent / 'ShaderBall'))
from shader_ball_tools import write_png, save_source_image, build_material

def json_write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(value, ensure_ascii=False, indent=2) + '\n', encoding='utf-8', newline='\n')

def arguments(parser):
    return parser.parse_args(sys.argv[sys.argv.index('--') + 1:] if '--' in sys.argv else [])

def rh(position):
    return [float(position[0]), float(position[2]), float(-position[1])]

def blender_position(position):
    return Vector((position[0], -position[2], position[1]))

def camera(scene, name, position, target, fov=45):
    data = bpy.data.cameras.new(name)
    data.type = 'PERSP'
    data.sensor_fit = 'VERTICAL'
    data.sensor_height = 24
    data.lens = 12 / math.tan(math.radians(fov) / 2)
    data.clip_start, data.clip_end = .05, 150
    obj = bpy.data.objects.new(name, data)
    scene.collection.objects.link(obj)
    obj.location = position
    obj.rotation_euler = (Vector(target) - obj.location).to_track_quat('-Z', 'Y').to_euler()
    obj['id'] = 'camera:' + name
    obj['targetRH'] = rh(target)
    return obj

def render_settings(scene, samples=512, denoise=False):
    scene.render.engine = 'CYCLES'
    scene.cycles.device = 'CPU'
    scene.cycles.samples = samples
    scene.cycles.seed = 0
    scene.cycles.use_adaptive_sampling = False
    scene.cycles.use_denoising = denoise
    scene.cycles.max_bounces = 8
    scene.cycles.transparent_max_bounces = 16
    scene.render.resolution_x, scene.render.resolution_y = 1280, 720
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = 'OPEN_EXR'
    scene.render.image_settings.color_depth = '32'
    scene.render.image_settings.exr_codec = 'ZIP'
    scene.view_settings.view_transform = 'AgX'
    scene.view_settings.look = 'AgX - Medium High Contrast'
    scene.view_settings.exposure = 0

def world(name='WorkshopSky'):
    """Broad analytic sky, with no sun disk or local scene/light baked into it."""
    result = bpy.data.worlds.new(name)
    result.use_nodes = True
    n, l = result.node_tree.nodes, result.node_tree.links
    n.clear()
    coord = n.new('ShaderNodeTexCoord')
    sep = n.new('ShaderNodeSeparateXYZ')
    l.new(coord.outputs['Generated'], sep.inputs[0])
    # World Generated is not defined; Normal is the world ray direction.
    l.remove(sep.inputs[0].links[0]); l.new(coord.outputs['Normal'], sep.inputs[0])
    ramp = n.new('ShaderNodeValToRGB')
    ramp.color_ramp.elements.remove(ramp.color_ramp.elements[1])
    for index, (at, color) in enumerate(((0, (.45,.49,.55,1)), (.35,(.75,.85,1.1,1)), (.7,(1.3,1.6,2.1,1)), (1,(2.1,2.6,3.4,1)))):
        e = ramp.color_ramp.elements[0] if index == 0 else ramp.color_ramp.elements.new(at)
        e.position, e.color = at, color
    # In a World shader Normal points opposite the viewing ray. Zenith must
    # sample the blue/high end of the ramp, not the nadir.
    scale = n.new('ShaderNodeMath'); scale.operation = 'MULTIPLY_ADD'
    scale.inputs[1].default_value = -.5; scale.inputs[2].default_value = .5
    l.new(sep.outputs['Z'], scale.inputs[0]); l.new(scale.outputs[0], ramp.inputs[0])
    background = n.new('ShaderNodeBackground'); background.name = 'Environment'
    l.new(ramp.outputs['Color'], background.inputs['Color'])
    background.inputs['Strength'].default_value = .8
    output = n.new('ShaderNodeOutputWorld'); l.new(background.outputs[0], output.inputs[0])
    return result

def material(name, images, color, metal=0, rough=1, alpha='OPAQUE', opacity=1, emission=0):
    result = build_material(images)
    result.name = name
    result['id'] = 'material:' + name
    result['profile'] = json.dumps({'baseColorFactor':[*color, opacity], 'metallicFactor':metal,
        'roughnessFactor':rough, 'alphaMode':alpha, 'alphaCutoff':.5,
        'doubleSided':alpha == 'MASK', 'emissiveFactor':[1,1,1] if emission else [0,0,0],
        'emissiveStrength':emission or 1})
    result.use_backface_culling = alpha != 'MASK'
    n, l, p = result.node_tree.nodes, result.node_tree.links, result.node_tree.nodes['PBR']
    tint = n.new('ShaderNodeMixRGB'); tint.blend_type = 'MULTIPLY'; tint.inputs[0].default_value = 1
    tint.inputs[2].default_value = (*color, 1)
    l.new(n['Texture_Albedo'].outputs['Color'], tint.inputs[1]); l.new(tint.outputs[0], p.inputs['Base Color'])
    for channel, socket, factor in (('Green','Roughness',rough), ('Blue','Metallic',metal)):
        mul = n.new('ShaderNodeMath'); mul.operation = 'MULTIPLY'; mul.inputs[1].default_value = factor
        sep = next(node for node in n if node.bl_idname == 'ShaderNodeSeparateColor')
        l.new(sep.outputs[channel], mul.inputs[0]); l.new(mul.outputs[0], p.inputs[socket])
    if alpha != 'OPAQUE':
        node = n.new('ShaderNodeMath'); node.operation = 'GREATER_THAN' if alpha == 'MASK' else 'MULTIPLY'
        node.inputs[1].default_value = .5 if alpha == 'MASK' else opacity
        l.new(n['Texture_Albedo'].outputs['Alpha'], node.inputs[0]); l.new(node.outputs[0], p.inputs['Alpha'])
        result.surface_render_method = 'DITHERED'
    p.inputs['Emission Strength'].default_value = emission
    p.inputs['Specular IOR Level'].default_value = .5
    return result

def png_set(directory, name, size, kind):
    v, u = np.mgrid[0:size,0:size].astype(np.float32) / size
    wave = .5 + .5*np.sin(2*np.pi*(u*13 + .3*np.sin(v*2*np.pi)))
    fine = np.sin(u*2*np.pi*127)*np.sin(v*2*np.pi*113)
    height = fine*.00008
    rgb = np.ones((size,size,3), np.float32)*.8
    rough, metallic, alpha = np.full((size,size),.8), np.zeros((size,size)), np.ones((size,size))
    emissive = np.zeros_like(rgb)
    if kind == 'wood':
        grain = .78 + .18*wave + .025*fine
        rgb = grain[...,None]*np.array([.58,.32,.15]); height += wave*.006; rough[:] = .75
    elif kind == 'concrete':
        seam = ((u*8)%1 < .012) | ((v*8)%1 < .012)
        rgb[:] = (.49,.53,.55); rgb *= (.98+.025*fine)[...,None]; rgb[seam] *= .6
        height += (~seam)*.006; rough[:] = .8
    elif kind == 'metal':
        rgb *= (.98+.015*np.sin(v*2*np.pi*300))[...,None]
        height += np.sin(v*2*np.pi*220)*.000025; metallic[:] = 1; rough[:] = .9
    elif kind == 'paint':
        rgb *= (.99+.012*fine)[...,None]; rough[:] = .7
    elif kind == 'rubber':
        rgb[:] = (.22,.24,.26); height += np.sin(u*2*np.pi*38)*.01; rough[:] = .95
    elif kind == 'grid':
        alpha = (((u*12)%1 < .12) | ((v*12)%1 < .12)).astype(np.float32)
        metallic[:] = 1; rgb[:] = .65; rough[:] = .75
    elif kind == 'display':
        rgb[:] = (.04,.08,.12)
        line = ((v > .16)&(v < .19)&(u > .08)&(u < .9)) | ((u>.08)&(u<.1)&(v>.16)&(v<.86))
        curve = (np.abs(v-(.48+.12*np.sin(u*22))) < .007)&(u>.1)&(u<.88)
        ticks = ((u*20)%1 < .06)&(v>.12)&(v<.15)
        bars = ((u*7)%1 < .55)&(u>.15)&(u<.9)&(v>.75)&(v<.79)
        emissive[line|curve|ticks|bars] = (.07,.65,1)
        rgb[line|curve|ticks|bars] = (.12,.55,.8); rough[:] = .6
    elif kind == 'amber':
        stripe = ((u+v)*7)%1 < .4
        rgb[:] = (.85,.58,.12); emissive[stripe] = (1,.38,.025); rough[:] = .7
    elif kind == 'label':
        rgb[:] = (.82,.79,.69)
        border = (u<.04)|(u>.96)|(v<.04)|(v>.96)
        rgb[border] = (.09,.16,.18)
        glyphs = {'W':['10101','10101','10101','10101','01010'], 'O':['01110','10001','10001','10001','01110'],
            'R':['11110','10001','11110','10100','10010'], 'K':['10001','10010','11100','10010','10001'],
            'S':['01111','10000','01110','00001','11110'], 'H':['10001','10001','11111','10001','10001'],
            'P':['11110','10001','11110','10000','10000']}
        unit = max(1,size//64)
        for i,c in enumerate('WORKSHOP'):
            for y,row in enumerate(glyphs[c]):
                for x,bit in enumerate(row):
                    if bit=='1': rgb[int(size*.42)+y*unit:int(size*.42)+(y+1)*unit,
                        int(size*.1)+(i*6+x)*unit:int(size*.1)+(i*6+x+1)*unit] = (.035,.14,.17)
    elif kind == 'glass':
        rgb[:] = (.8,.95,1); rough[:] = .2
    dx = (np.roll(height,-1,1)-np.roll(height,1,1))*size*.2
    dy = (np.roll(height,-1,0)-np.roll(height,1,0))*size*.2
    normal = np.stack((-dx,-dy,np.ones_like(dx)),axis=-1)
    normal /= np.linalg.norm(normal,axis=-1,keepdims=True)
    values = {'Albedo':np.dstack((rgb,alpha)), 'Normal':np.dstack((normal*.5+.5,np.ones_like(alpha))),
        'ORM':np.stack((np.ones_like(alpha),rough,metallic,np.ones_like(alpha)),axis=-1),
        'Emissive':np.dstack((emissive,np.ones_like(alpha)))}
    images = {}
    for key,value in values.items():
        path = directory/f'{name}_{key}.png'
        write_png(path,np.rint(np.clip(value,0,1)*255).astype(np.uint8))
        image = bpy.data.images.load(str(path),check_existing=False)
        image.name = f'{name}_{key}'
        image.colorspace_settings.name = 'sRGB' if key in ('Albedo','Emissive') else 'Non-Color'
        image.pack(); images[key] = image
    return images

def projected_uv(mesh):
    """Face projection avoids singular UV triangles, including bevel faces."""
    mesh.update()
    uv = mesh.uv_layers.new(name='UV0') if not mesh.uv_layers else mesh.uv_layers[0]
    minimum = [min(v.co[i] for v in mesh.vertices) for i in range(3)]
    extent = [max(v.co[i] for v in mesh.vertices)-minimum[i] for i in range(3)]
    for face in mesh.polygons:
        normal = face.normal
        axis = max(range(3),key=lambda i:abs(normal[i]))
        a,b = ((1,2),(0,2),(0,1))[axis]
        for loop_index in face.loop_indices:
            p = mesh.vertices[mesh.loops[loop_index].vertex_index].co
            u,v = (p[a]-minimum[a])/max(extent[a],1e-8),(p[b]-minimum[b])/max(extent[b],1e-8)
            if (normal[axis]<0) != (axis==1): u = 1-u
            uv.data[loop_index].uv = (u,v)

def prepare(obj, name, materials):
    import bmesh
    obj.name = name; obj.data.name = name
    for mat in materials: obj.data.materials.append(mat)
    bm = bmesh.new(); bm.from_mesh(obj.data)
    bmesh.ops.recalc_face_normals(bm,faces=list(bm.faces))
    bmesh.ops.triangulate(bm,faces=list(bm.faces)); bm.to_mesh(obj.data); bm.free()
    projected_uv(obj.data)
    obj.data['id'] = 'mesh:' + name
    return obj

def custom_mesh(name, vertices, faces, mat):
    data = bpy.data.meshes.new(name)
    data.from_pydata(vertices,[],faces); data.update()
    obj = bpy.data.objects.new(name,data); bpy.context.scene.collection.objects.link(obj)
    for p in data.polygons: p.use_smooth = True
    return prepare(obj,name,[mat])

def box(name, size, mat, bevel=.025):
    bpy.ops.mesh.primitive_cube_add(size=1)
    obj = bpy.context.object; obj.dimensions = size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    if bevel:
        mod = obj.modifiers.new('Physical edge bevel','BEVEL'); mod.width = min(bevel,min(size)*.2); mod.segments = 3
        bpy.ops.object.modifier_apply(modifier=mod.name)
    return prepare(obj,name,[mat])

def cylinder(name, radius, depth, mat, segments=64):
    bpy.ops.mesh.primitive_cylinder_add(vertices=segments,radius=radius,depth=depth)
    obj = bpy.context.object
    for p in obj.data.polygons: p.use_smooth = len(p.vertices)==4
    return prepare(obj,name,[mat])

def torus(name, major, minor, mat, segments=160, sides=40):
    v,f = [],[]
    for i in range(segments):
        a = i*2*math.pi/segments
        for j in range(sides):
            b = j*2*math.pi/sides; r = major+minor*math.cos(b)
            v.append((r*math.cos(a),r*math.sin(a),minor*math.sin(b)))
    for i in range(segments):
        for j in range(sides): f.append((i*sides+j,((i+1)%segments)*sides+j,
            ((i+1)%segments)*sides+(j+1)%sides,i*sides+(j+1)%sides))
    return custom_mesh(name,v,f,mat)

def housing(name, mat):
    # Closed radial profile: inner bore, flared lips, outer tapered casing.
    corners = [(-.58,.62),(-.58,.79),(-.5,.83),(-.36,.77),(.35,.69),(.5,.74),(.58,.74),(.58,.59),(.4,.58),(-.4,.62)]
    profile = []
    for i,p in enumerate(corners):
        previous = corners[(i-1)%len(corners)]
        q = corners[(i+1)%len(corners)]
        following = corners[(i+2)%len(corners)]
        # Catmull-Rom cast profile gives real curved fillets/flared lips; the
        # cross-section samples do not just subdivide straight walls.
        for k in range(5):
            t=k/5
            profile.append(tuple(.5*((2*p[c])+(-previous[c]+q[c])*t+
                (2*previous[c]-5*p[c]+4*q[c]-following[c])*t*t+
                (-previous[c]+3*p[c]-3*q[c]+following[c])*t*t*t) for c in range(2)))
    vertices,faces,n = [],[],192
    for i in range(n):
        a = i*2*math.pi/n
        for x,r in profile: vertices.append((x,r*math.cos(a),r*math.sin(a)))
    m = len(profile)
    for i in range(n):
        for j in range(m): faces.append((i*m+j,((i+1)%n)*m+j,((i+1)%n)*m+(j+1)%m,i*m+(j+1)%m))
    return custom_mesh(name,vertices,faces,mat)

def rotor(name, mat, reverse=False):
    vertices,faces = [],[]
    radial,chord,blades = 18,8,28
    for blade in range(blades):
        start = len(vertices)
        for side in (-1,1):
            for i in range(radial+1):
                t = i/radial; r = .23+.49*t
                for j in range(chord+1):
                    u = j/chord; a = blade*2*math.pi/blades + (u-.5)*(.2-.07*t) + .08*t*t
                    x = (u-.5)*(.2+.13*t)*(1 if reverse else -1)+.045*math.sin(math.pi*u)+side*.009
                    vertices.append((x,r*math.cos(a),r*math.sin(a)))
        layer = (radial+1)*(chord+1)
        def ix(s,i,j): return start+s*layer+i*(chord+1)+j
        for s in range(2):
            for i in range(radial):
                for j in range(chord): faces.append((ix(s,i,j),ix(s,i+1,j),ix(s,i+1,j+1),ix(s,i,j+1)))
        for i in range(radial):
            for j in (0,chord): faces.append((ix(0,i,j),ix(1,i,j),ix(1,i+1,j),ix(0,i+1,j)))
        for j in range(chord):
            for i in (0,radial): faces.append((ix(0,i,j),ix(0,i,j+1),ix(1,i,j+1),ix(1,i,j)))
    return custom_mesh(name,vertices,faces,mat)
