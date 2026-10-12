"""Numeric checks against actual readbacks; no position render target is used."""
from pathlib import Path
import json, sys, struct, zlib
from functools import lru_cache
import numpy as np

work, api, repo = Path(sys.argv[1]), sys.argv[2], Path(sys.argv[3])

def folder(label):
    return work / f'{api}-{label}-gpu'

def raw(label, name, dtype):
    return np.fromfile(folder(label) / (name + '.bin'), dtype=dtype)

def hdr(label):
    return raw(label, 'hdr', np.float16).astype(np.float32).reshape(-1, 4)

@lru_cache(maxsize=12)
def png(path):
    # WIC captures are non-interlaced RGB/RGBA8; use only stdlib + bundled NumPy.
    data = path.read_bytes(); assert data[:8] == b'\x89PNG\r\n\x1a\n'
    offset = 8; compressed = bytearray()
    while offset < len(data):
        count = struct.unpack_from('>I', data, offset)[0]
        kind = data[offset+4:offset+8]; body = data[offset+8:offset+8+count]
        if kind == b'IHDR':
            width, height, bits, color, compression, filtering, interlace = struct.unpack('>IIBBBBB', body)
            assert bits == 8 and color in (2,6) and interlace == 0
        if kind == b'IDAT': compressed.extend(body)
        offset += count + 12
    channels = 4 if color == 6 else 3; stride = width * channels
    filtered = zlib.decompress(compressed); output = bytearray(); previous = bytearray(stride)
    for y in range(height):
        start = y * (stride + 1); mode = filtered[start]; row = bytearray(filtered[start+1:start+1+stride])
        for x in range(stride):
            left = row[x-channels] if x >= channels else 0; up = previous[x]; diagonal = previous[x-channels] if x >= channels else 0
            if mode == 0: predictor = 0
            elif mode == 1: predictor = left
            elif mode == 2: predictor = up
            elif mode == 3: predictor = (left+up)//2
            else:
                assert mode == 4
                p = left+up-diagonal; a,b,c = abs(p-left),abs(p-up),abs(p-diagonal)
                predictor = left if a <= b and a <= c else up if b <= c else diagonal
            row[x] = (row[x]+predictor)&255
        output.extend(row); previous = row
    return np.frombuffer(output, np.uint8).reshape(height,width,channels)[:,:,:3].astype(np.int16)

def difference(a, b):
    x = png(work / f'{api}-{a}.png')
    y = png(work / f'{api}-{b}.png')
    return np.max(abs(x[:, :, :3] - y[:, :, :3]), axis=2)

for suffix in ['base', 'stress', 'full', 'odd']:
    a, b = 'ps-' + suffix, 'cs-' + suffix
    for name in ['base-ao', 'normal', 'metal-rough', 'emissive', 'depth', 'shadow-depth']:
        assert (folder(a) / (name + '.bin')).read_bytes() == (folder(b) / (name + '.bin')).read_bytes(), (suffix, name, 'G-buffer mismatch')
    x, y = hdr(a), hdr(b)
    assert np.isfinite(x).all() and np.isfinite(y).all()
    error = abs(x - y)
    assert np.all(error <= 1e-3 + 2e-3 * np.maximum(abs(x), abs(y))), (suffix, error.max(), 'HDR PS/CS tolerance')
    print(f'{api} {suffix}: G-buffer/depth exact, HDR max difference {error.max():.8g}')

for label in ['ps-base', 'cs-base', 'ps-stress', 'cs-stress', 'ps-full', 'cs-full', 'black', 'constant-odd']:
    meta = json.loads((folder(label) / 'metadata.json').read_text())
    assert meta['bytesPerPixel'] == 18
    for im in meta['images']:
        values = np.fromfile(folder(label) / (im['name'] + '.bin'), dtype=np.float16)
        assert np.isfinite(values).all(), (label, im['name'], 'NaN/Inf')

assert np.all(hdr('black')[:, :3] == 0)
for name in ['extract', 'horizontal', 'bloom']:
    assert np.all(raw('black', name, np.float16).reshape(-1, 4)[:, :3] == 0), name

depth = raw('ps-base', 'depth', np.float32).reshape(720, 1280)
assert np.isfinite(depth).all() and np.all((depth >= 0) & (depth <= 1))
shadow = raw('ps-base', 'shadow-depth', np.float32)
assert np.isfinite(shadow).all() and shadow.min() < .99 and shadow.max() == 1
assert np.all(raw('no-shadow', 'shadow-depth', np.float32) == 1)
assert np.count_nonzero(shadow != raw('moved', 'shadow-depth', np.float32)) > 100, 'moving shadow'
assert np.count_nonzero(difference('ps-base', 'hard-shadow')) > 100, 'PCF'
assert np.count_nonzero(difference('ps-base', 'no-bloom')) > 100, 'Bloom'
assert np.count_nonzero(raw('opaque', 'depth', np.float32) != depth.ravel()) > 20, 'MASK holes'

# Decode the actual SNORM normal values and the positive HDR emissive format.
e = raw('ps-base', 'normal', np.int16).reshape(-1, 2).astype(np.float64) / 32767
n = np.column_stack((e, 1 - abs(e[:, 0]) - abs(e[:, 1])))
t = np.maximum(-n[:, 2], 0)
n[:, :2] += np.where(n[:, :2] >= 0, -t[:, None], t[:, None])
n /= np.linalg.norm(n, axis=1)[:, None]
assert np.isfinite(n).all() and np.max(abs(np.linalg.norm(n, axis=1) - 1)) < 1e-6
packed = raw('ps-base', 'emissive', np.uint32)
def unpack(bits, mantissa):
    exponent = bits >> mantissa
    fraction = bits & ((1 << mantissa) - 1)
    return np.where(exponent == 0, fraction * 2.0 ** (-14 - mantissa), (1 + fraction / 2.0 ** mantissa) * np.exp2(exponent.astype(np.int32) - 15))
emission = np.column_stack((unpack(packed & 2047, 6), unpack((packed >> 11) & 2047, 6), unpack(packed >> 22, 5)))
assert np.isfinite(emission).all() and emission.max() > 1, 'HDR emissive preservation'

for kind in ['ps', 'cs']:
    assert raw(kind + '-forward', 'depth', np.float32).tobytes() == depth.ravel().tobytes(), 'Forward depth geometry'
    diff = difference(kind + '-ui-baseline', kind + '-ui-edited')
    # The UI overlays the left part; inspect only scene pixels on the right.
    assert np.count_nonzero(diff[:, 350:] > 2) > 1000, 'UI input did not change illumination'
    reset = difference(kind + '-ui-baseline', kind + '-ui-reset')
    assert reset[:, 350:].max() == 0, 'Reset did not restore scene'
    forward_error = difference(kind+'-base',kind+'-forward').max()
    assert forward_error <= 8, (kind,forward_error,'Forward reconstruction/quantization comparison')
    print(f'{api} {kind} Forward vs Deferred max PNG difference: {forward_error}')

# Reconstruct world positions from captured depth, then compare with CPU ray/triangle hits.
config = json.loads((repo / 'Assets/Scenes/Workshop/Workshop.json').read_text())
camera = config['cameras']['Hero']
origin = np.array(camera['position'], dtype=np.float64); origin[2] *= -1
target = np.array(camera['target'], dtype=np.float64); target[2] *= -1
def unit(x): return x / np.linalg.norm(x)
f = unit(target-origin); r = unit(np.cross([0,1,0],f)); u = np.cross(f,r)
view = np.eye(4); view[:3,:3] = np.column_stack((r,u,f)); view[3,:3] = -origin @ view[:3,:3]
near, far = camera['near'], camera['far']; ys = 1/np.tan(np.deg2rad(camera['fovDegrees'])/2)
projection = np.array([[ys/(1280/720),0,0,0],[0,ys,0,0],[0,0,far/(far-near),1],[0,0,-near*far/(far-near),0]])
inverse = np.linalg.inv(view @ projection)
model_path = repo / 'Assets/Models/Workshop/Workshop.gltf'
g = json.loads(model_path.read_text()); buffers = [(model_path.parent / b['uri']).read_bytes() for b in g['buffers']]
def accessor(index):
    a=g['accessors'][index];v=g['bufferViews'][a['bufferView']];c={'VEC3':3,'SCALAR':1}[a['type']]
    dtype={5126:np.float32,5123:np.uint16,5125:np.uint32}[a['componentType']];size=np.dtype(dtype).itemsize
    return np.ndarray((a['count'],c),dtype,buffer=buffers[v['buffer']],offset=v.get('byteOffset',0)+a.get('byteOffset',0),strides=(v.get('byteStride',c*size),size)).copy()
triangles=[];masked=[]
def visit(index,parent):
    node=g['nodes'][index];x,y,z,w=node.get('rotation',[0,0,0,1])
    rotation=np.array([[1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)],[2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)],[2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)]])
    local=np.eye(4);local[:3,:3]=rotation @ np.diag(node.get('scale',[1,1,1]));local[:3,3]=node.get('translation',[0,0,0]);world=parent @ local
    if 'mesh' in node:
        for p in g['meshes'][node['mesh']]['primitives']:
            mode=g['materials'][p['material']].get('alphaMode','OPAQUE')
            if mode=='BLEND':continue
            vertices=accessor(p['attributes']['POSITION']).astype(np.float64) @ world[:3,:3].T + world[:3,3];vertices[:,2]*=-1
            indices=accessor(p['indices']).reshape(-1,3);triangles.append(vertices[indices]);masked.append(np.full(len(indices),mode=='MASK'))
    for child in node.get('children',[]):visit(child,world)
for node in g['scenes'][0]['nodes']:visit(node,np.eye(4))
tri=np.concatenate(triangles);mask=np.concatenate(masked);edge1=tri[:,1]-tri[:,0];edge2=tri[:,2]-tri[:,0];tv=origin-tri[:,0];q=np.cross(tv,edge1)
checked=0;worst=0
for x,y in [(500,600),(600,640),(700,620),(450,560),(1000,520),(280,400),(1050,380),(900,300),(750,250),(450,300),(800,500),(600,450)]:
    if depth[y,x]==1:continue
    ndc=np.array([(x+.5)/1280*2-1,1-(y+.5)/720*2,depth[y,x],1]);p=ndc @ inverse;p=p[:3]/p[3];direction=unit(p-origin)
    h=np.cross(np.broadcast_to(direction,edge2.shape),edge2);det=np.sum(edge1*h,axis=1);invdet=np.divide(1,det,out=np.zeros_like(det),where=abs(det)>1e-12)
    a=np.sum(tv*h,axis=1)*invdet;b=np.sum(direction*q,axis=1)*invdet;distance=np.sum(edge2*q,axis=1)*invdet
    valid=(abs(det)>1e-12)&(a>=0)&(b>=0)&(a+b<=1)&(distance>0);dist=np.where(valid,distance,np.inf);index=np.argmin(dist)
    assert np.isfinite(dist[index]), 'no CPU geometry hit'
    if mask[index]:continue
    error=np.linalg.norm(p-(origin+direction*dist[index]));worst=max(worst,error)
    assert error <= 1e-3*max(1,dist[index]), (x,y,error,'depth reconstruction')
    checked+=1
assert checked>=6, checked
print(f'{api}: {checked} CPU ray/triangle depth probes passed, max position error {worst:.8g} m')
(work / f'{api}-numeric.json').write_text(json.dumps({'api':api,'depthProbes':checked,'maxPositionErrorMeters':worst,'psCsGBufferExact':True,'psCsHdrTolerancePassed':True,'hdrEmissiveMax':float(emission.max())},indent=2))
print(f'{api} Deferred numeric checks passed')
