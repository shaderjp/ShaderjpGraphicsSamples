"""Rebuild the MIT Geometry Lab asset, its Blender source and runtime binary.
Run with Blender 5.2 --background --python generate.py. Seed: 20261029.
The binary stores little-endian position3/uv2/normal3/tangent4 and uint32 indices.
"""
import bpy
import hashlib
import json
import math
from pathlib import Path
import random
import struct

ROOT = Path(__file__).resolve().parents[3]
OUT = ROOT / 'Assets/Models/GeometryLab'
SEED = 20261029


def main():
    random.seed(SEED)
    bpy.context.preferences.filepaths.save_version=0
    OUT.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    blob = bytearray()
    entries = []

    def emit(name, positions, indices, material, motion=0, uvs=None):
        normals = [[0., 0., 0.] for _ in positions]
        for k in range(0, len(indices), 3):
            a, b, c = [positions[j] for j in indices[k:k+3]]
            e = [b[j]-a[j] for j in range(3)]
            f = [c[j]-a[j] for j in range(3)]
            n = [e[1]*f[2]-e[2]*f[1], e[2]*f[0]-e[0]*f[2], e[0]*f[1]-e[1]*f[0]]
            for v in indices[k:k+3]:
                for j in range(3): normals[v][j] += n[j]
        vertex_offset = len(blob)
        for i, p in enumerate(positions):
            n = normals[i]
            length = math.sqrt(sum(v*v for v in n)) or 1
            n = [v/length for v in n]
            uv = uvs[i] if uvs else [p[0]*.25, p[2]*.25]
            # A tangent perpendicular to the smooth normal; no normal-map distortion.
            t = [n[2], 0, -n[0]]
            tl = math.sqrt(sum(v*v for v in t))
            t = [v/tl for v in t] if tl > .0001 else [1, 0, 0]
            blob.extend(struct.pack('<12f', *p, *uv, *n, *t, 1))
        index_offset = len(blob)
        blob.extend(struct.pack('<'+'I'*len(indices), *indices))
        entries.append(dict(name=name, vertexOffset=vertex_offset, vertexCount=len(positions),
                            indexOffset=index_offset, indexCount=len(indices), material=material, motion=motion))
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([(x, -z, y) for x,y,z in positions], [],
                         [indices[k:k+3] for k in range(0,len(indices),3)])
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        mat = bpy.data.materials.get(material) or bpy.data.materials.new(material)
        colors = dict(stone=(.55,.48,.35,1), bronze=(.48,.23,.08,1), teal=(.04,.34,.32,1), floor=(.16,.19,.22,1))
        mat.diffuse_color = colors[material]
        obj.data.materials.append(mat)
        for face in mesh.polygons: face.use_smooth = name in ('Relief', 'Ribbon')

    def box(name, center, size, material):
        x,y,z=center; a,b,c=[v*.5 for v in size]
        p=[(x+sx*a,y+sy*b,z+sz*c) for sx,sy,sz in
           [(-1,-1,-1),(1,-1,-1),(1,1,-1),(-1,1,-1),(-1,-1,1),(1,-1,1),(1,1,1),(-1,1,1)]]
        ix=[0,3,2,0,2,1,4,5,6,4,6,7,0,4,7,0,7,3,1,2,6,1,6,5,3,7,6,3,6,2,0,1,5,0,5,4]
        emit(name,p,ix,material)

    box('Floor',(0,-.15,0),(36,.3,36),'floor')
    box('Plinth',(0,.3,0),(5,.6,5),'stone')
    box('Panel',(0,.6,0),(.12,1.2,.7),'teal')
    p=[];uv=[];ix=[];nu=256;nv=128
    for y in range(nv+1):
        v=y/nv; phi=.015+v*(math.pi-.03)
        for x in range(nu+1):
            u=x/nu;theta=u*math.tau
            r=1.65+.13*math.sin(18*theta+6*phi)*math.sin(phi)**2+.055*math.sin(40*phi+4*theta)
            p.append((r*math.sin(phi)*math.cos(theta),2.4+r*math.cos(phi),r*math.sin(phi)*math.sin(theta)))
            uv.append((u,v))
    for y in range(nv):
        for x in range(nu):
            a=y*(nu+1)+x;b=a+1;c=a+nu+1;d=c+1
            ix += [a,c,b,b,c,d]
    emit('Relief',p,ix,'bronze',uvs=uv)
    p=[];uv=[];ix=[];nx=128;ny=64
    for y in range(ny+1):
        for x in range(nx+1):
            u=x/nx;v=y/ny
            p.append(((u-.5)*7,1+v*3,(v-.5)*1.8))
            uv.append((u,v))
    for y in range(ny):
        for x in range(nx):
            a=y*(nx+1)+x;b=a+1;c=a+nx+1;d=c+1
            ix += [a,b,c,b,d,c]
    emit('Ribbon',p,ix,'teal',motion=1,uvs=uv)
    (OUT/'GeometryLab.bin').write_bytes(blob)
    config=dict(profile='geometry-lab-1',seed=SEED,binary='GeometryLab.bin',vertexStride=48,
                coordinateSystem='left-handed Y-up meters',primitives=entries,
                materials=dict(stone=[.6,.5,.34,0,.8],bronze=[.7,.32,.09,.85,.24],
                               teal=[.055,.5,.42,.35,.3],floor=[.22,.25,.3,.1,.36]))
    (OUT/'GeometryLab.json').write_text(json.dumps(config,indent=2)+'\n',encoding='utf-8')
    manifest=dict(profile='geometry-lab-manifest-1',seed=SEED,license='MIT',files={})
    for name in ['GeometryLab.bin','GeometryLab.json']:
        manifest['files'][name]=dict(bytes=(OUT/name).stat().st_size,sha256=hashlib.sha256((OUT/name).read_bytes()).hexdigest())
    bpy.context.scene['seed']=SEED
    bpy.context.scene['runtime_coordinates']='left-handed Y-up; Blender mapping: (x,-z,y)'
    bpy.context.scene.render.engine='BLENDER_EEVEE'
    bpy.ops.wm.save_as_mainfile(filepath=str(Path(__file__).with_name('GeometryLab.blend')))
    manifest['sources']={}
    for path in [Path(__file__).resolve(),Path(__file__).with_name('GeometryLab.blend').resolve()]:
        manifest['sources'][path.relative_to(ROOT).as_posix()]=dict(
            bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
    (OUT/'Manifest.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    print('GeometryLab exported',len(blob),'bytes',flush=True)


if __name__=='__main__': main()
