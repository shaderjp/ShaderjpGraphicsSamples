"""Deterministic CPU IBL baking, scene-linear RGB. MIT License.

DDS face order: +X,-X,+Y,-Y,+Z,-Z, left-handed Y-up. No display transform.
Diffuse stores irradiance E (not E/pi). GGX uses perceptual roughness squared,
Schlick Fresnel and height-correlated Smith, matching the tutorial HLSL.
"""
import json
import math
from pathlib import Path
import struct
import numpy as np

FACES = ('+X', '-X', '+Y', '-Y', '+Z', '-Z')

def directions(size, face):
    p = (np.arange(size, dtype=np.float64) + .5) * (2 / size) - 1
    u, v = np.meshgrid(p, p)
    one = np.ones_like(u)
    components = ((one,-v,-u),(-one,-v,u),(u,one,v),(u,-one,-v),(u,-v,one),(-u,-v,-one))
    d = np.stack(components[face], axis=-1)
    return d / np.linalg.norm(d, axis=-1, keepdims=True)

def write_dds(path, faces, cube=True):
    # faces[face][mip], tightly packed uncompressed float16 DX10 DDS.
    first = faces[0][0]
    h, w, channels = first.shape
    if channels not in (2,4) or (cube and len(faces) != 6):
        raise ValueError('DDS requires RG/RGBA and six cubemap faces')
    mip_count = len(faces[0])
    flags = 0x100F | (0x20000 if mip_count > 1 else 0)
    caps = 0x1000 | (0x400008 if mip_count > 1 else 0) | (8 if cube else 0)
    header = [124, flags, h, w, w*channels*2, 0, mip_count] + [0]*11
    header += [32,4,struct.unpack('<I', b'DX10')[0],0,0,0,0,0,caps,0xFE00 if cube else 0,0,0,0]
    data = b'DDS ' + struct.pack('<31I', *header)
    data += struct.pack('<5I', 10 if channels == 4 else 34, 3, 4 if cube else 0, 1, 0)
    for face in faces:
        for mip, pixels in enumerate(face):
            expected = (max(1,h>>mip), max(1,w>>mip), channels)
            if pixels.shape != expected or not np.isfinite(pixels).all() or np.max(np.abs(pixels)) > 65504:
                raise ValueError(f'Invalid DDS pixels: {path}, mip {mip}')
            data += np.asarray(pixels, dtype='<f2').tobytes()
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(data)

def read_dds(path):
    data = Path(path).read_bytes()
    if data[:4] != b'DDS ' or len(data) < 148:
        raise ValueError('Invalid DDS')
    h, w = struct.unpack_from('<2I', data, 12)
    mips = struct.unpack_from('<I', data, 28)[0] or 1
    fmt, _, misc, _, _ = struct.unpack_from('<5I', data, 128)
    c = {10:4,34:2}[fmt]
    faces, offset = [], 148
    for _ in range(6 if misc & 4 else 1):
        levels = []
        for mip in range(mips):
            shape = (max(1,h>>mip),max(1,w>>mip),c)
            count = math.prod(shape)
            levels.append(np.frombuffer(data, dtype='<f2', count=count, offset=offset).astype(np.float32).reshape(shape))
            offset += count*2
        faces.append(levels)
    if offset != len(data):
        raise ValueError('DDS byte count mismatch')
    return faces

def sample_latlong(image, d):
    h,w,_ = image.shape
    u = (np.arctan2(d[...,0], d[...,2])/(2*np.pi)+.5)*w-.5
    v = np.arccos(np.clip(d[...,1],-1,1))/np.pi*h-.5
    v = np.clip(v,0,h-1)
    x,y = np.floor(u).astype(int), np.floor(v).astype(int)
    a,b = (u-x)[...,None], (v-y)[...,None]
    return ((image[y,x%w]*(1-a)+image[y,(x+1)%w]*a)*(1-b) +
            (image[np.minimum(y+1,h-1),x%w]*(1-a)+image[np.minimum(y+1,h-1),(x+1)%w]*a)*b)[...,:3]

def hammersley(count):
    i = np.arange(count, dtype=np.uint32)
    bits = i.copy()
    bits = (bits << 16) | (bits >> 16)
    bits = ((bits & 0x55555555)<<1) | ((bits & 0xAAAAAAAA)>>1)
    bits = ((bits & 0x33333333)<<2) | ((bits & 0xCCCCCCCC)>>2)
    bits = ((bits & 0x0F0F0F0F)<<4) | ((bits & 0xF0F0F0F0)>>4)
    bits = ((bits & 0x00FF00FF)<<8) | ((bits & 0xFF00FF00)>>8)
    return np.stack(((i+.5)/count, bits.astype(float)*2.3283064365386963e-10),axis=-1)

def basis(n):
    up = np.zeros_like(n)
    up[:,2] = 1
    up[np.abs(n[:,2])>.999] = (1,0,0)
    t = np.cross(up,n)
    t /= np.linalg.norm(t,axis=-1,keepdims=True)
    return t, np.cross(n,t)

def integrate(image, normals, roughness=None, samples=1024):
    n = normals.reshape(-1,3)
    out = np.zeros_like(n)
    xi = hammersley(samples)
    phi = 2*np.pi*xi[:,0]
    if roughness is None:
        z = np.sqrt(1-xi[:,1])
    else:
        alpha = max(roughness,.045)**2
        z = np.sqrt((1-xi[:,1])/(1+(alpha*alpha-1)*xi[:,1]))
    r = np.sqrt(np.maximum(0,1-z*z))
    local = np.stack((r*np.cos(phi),r*np.sin(phi),z),axis=-1)
    for start in range(0,len(n),128):
        ns = n[start:start+128]
        t,b = basis(ns)
        world = t[:,None]*local[None,:,0,None] + b[:,None]*local[None,:,1,None] + ns[:,None]*local[None,:,2,None]
        if roughness is None:
            out[start:start+len(ns)] = np.pi*np.mean(sample_latlong(image,world),axis=1)
        else:
            # Prefilter uses V=N; GGX sampling, weighted by NoL.
            l = 2*np.sum(ns[:,None]*world,axis=-1,keepdims=True)*world - ns[:,None]
            weight = np.maximum(np.sum(ns[:,None]*l,axis=-1),0)
            radiance = sample_latlong(image,l)
            out[start:start+len(ns)] = np.sum(radiance*weight[...,None],axis=1)/np.maximum(np.sum(weight,axis=1,keepdims=True),1e-12)
    return out.reshape(normals.shape)

def rgba(rgb):
    return np.concatenate((rgb, np.ones(rgb.shape[:-1]+(1,))),axis=-1)

def bake_lut(size=256,samples=1024):
    xi = hammersley(samples)
    result = np.zeros((size,size,2))
    nv = (np.arange(size)+.5)/size
    vx = np.sqrt(1-nv*nv)
    phi = 2*np.pi*xi[:,0]
    for y in range(size):
        roughness = max((y+.5)/size,.045)
        a2 = roughness**4
        hz = np.sqrt((1-xi[:,1])/(1+(a2-1)*xi[:,1]))
        hr = np.sqrt(np.maximum(0,1-hz*hz))
        hx,hy = hr*np.cos(phi), hr*np.sin(phi)
        vh = np.maximum(vx[:,None]*hx + nv[:,None]*hz,0)
        nl = 2*vh*hz-nv[:,None]
        nl_safe = np.maximum(nl,1e-8)
        gv = nl_safe*np.sqrt(nv[:,None]**2*(1-a2)+a2)
        gl = nv[:,None]*np.sqrt(nl_safe**2*(1-a2)+a2)
        vis = .5/np.maximum(gv+gl,1e-12)
        gvis = 4*nl_safe*vis*vh/np.maximum(hz,1e-8)
        gvis[nl<=0] = 0
        fc = (1-vh)**5
        result[y,:,0] = np.mean((1-fc)*gvis,axis=1)
        result[y,:,1] = np.mean(fc*gvis,axis=1)
    return result

def bake_environment(image, output, name, samples=1024):
    output = Path(output)
    env, diffuse, specular = [], [], []
    for face in range(6):
        print(f'{name} {FACES[face]}: environment / irradiance / roughness mips',flush=True)
        env.append([rgba(sample_latlong(image,directions(512,face)))])
        diffuse.append([rgba(integrate(image,directions(32,face),samples=samples))])
        levels = [rgba(sample_latlong(image,directions(256,face)))]
        for mip in range(1,9):
            levels.append(rgba(integrate(image,directions(max(1,256>>mip),face),mip/8,samples)))
        specular.append(levels)
    write_dds(output/f'{name}_Environment.dds', env)
    write_dds(output/f'{name}_Irradiance.dds', diffuse)
    write_dds(output/f'{name}_Specular.dds', specular)

def make_orientation(path):
    glyphs={'X':['10001','01010','00100','01010','10001'],
            'Y':['10001','01010','00100','00100','00100'],
            'Z':['11111','00010','00100','01000','11111'],
            '+':['00000','00100','11111','00100','00000'],
            '-':['00000','00000','11111','00000','00000']}
    colors=((1,.1,.1),(.1,1,1),(.1,1,.1),(1,.1,1),(.1,.1,1),(1,1,.1))
    faces=[]
    for face in range(6):
        pixels=np.ones((256,256,4));pixels[...,:3]=colors[face]
        # Up arrow and asymmetric corners expose rotations/mirroring.
        pixels[20:68,122:134,:3]=.03
        for y in range(20):pixels[20+y,128-y:129+y,:3]=.03
        pixels[225:245,12:32,:3]=1;pixels[225:245,224:244,:3]=.03
        for char_index,char in enumerate(FACES[face]):
            for y,row in enumerate(glyphs[char]):
                for x,bit in enumerate(row):
                    if bit=='1':pixels[95+y*12:107+y*12,60+char_index*78+x*12:72+char_index*78+x*12,:3]=.03
        faces.append([pixels])
    write_dds(path,faces)
