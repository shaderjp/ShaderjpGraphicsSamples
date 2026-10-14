"""Check real raster IDs, reconstructed attributes and conservative light-culling results."""
from pathlib import Path
import json
import sys
import numpy as np

work, api = Path(sys.argv[1]), sys.argv[2]

def folder(label):
    return work / f'{api}-{label}-gpu'

def load(label, name, dtype):
    return np.fromfile(folder(label) / (name + '.bin'), dtype=dtype)

def meta(label):
    return json.loads((folder(label) / 'metadata.json').read_text())

def hdr(label):
    m = meta(label)
    return load(label, 'hdr', np.float16).astype(np.float64).reshape(m['height'], m['width'], 4)

def verify_ids(label):
    m = meta(label); w, h = m['width'], m['height']
    assert m['bytesPerPixel'] == 12
    assert not (folder(label) / 'base-ao.bin').exists()
    ids = load(label, 'visibility', np.uint32).reshape(h, w, 2)
    depth = load(label, 'depth', np.float32).reshape(h, w)
    assert np.array_equal(ids[:, :, 0] != 0, depth < 1), (label, 'coverage/background')
    assert (ids[depth == 1] == 0).all()
    instances = load(label, 'instances', np.uint32).reshape(-1, 36)
    selected = ids[depth < 1]
    assert len(selected) > 1000 and selected[:, 0].max() <= len(instances)
    triangles = selected[:, 1] & 0x7fffffff
    records = instances[selected[:, 0] - 1]
    assert (triangles < records[:, 35]).all()
    vertices = load(label, 'vertices', np.float32).reshape(-1, 12)
    indices = load(label, 'indices', np.uint32)
    assert (records[:, 33] + triangles * 3 + 2 < len(indices)).all()
    for corner in range(3):
        assert (records[:, 32] + indices[records[:, 33] + triangles * 3 + corner] < len(vertices)).all()
    material_count = load(label, 'materials', np.float32).size // 20
    assert (records[:, 34] < material_count).all()
    materials = load(label, 'materials', np.float32).reshape(-1, 20)
    backfaces = (selected[:, 1] & 0x80000000) != 0
    assert (materials[records[backfaces, 34], 18] == 1).all(), 'Back-face IDs require a double-sided material'
    assert np.isfinite(depth).all() and np.isfinite(vertices).all()
    for name in ['hdr', 'extract', 'horizontal', 'bloom']:
        assert np.isfinite(load(label, name, np.float16)).all(), (label, name)
    for name in ['position', 'world-position', 'normal', 'metal-rough', 'emissive']:
        assert not (folder(label) / (name + '.bin')).exists()
    if m['lightCulling'] != 0:
        headers = load(label, 'light-headers', np.uint32).reshape(-1, 2)
        listed = load(label, 'light-indices', np.uint32).reshape(-1, 256)
        assert len(headers) == m['cells'] and (headers[:, 0] <= m['pointLights']).all()
        assert np.array_equal(headers[:, 1], headers[:, 0] > m['listCapacity'])
        unbounded = np.flatnonzero(load(label, 'lights', np.float32).reshape(-1, 8)[:, 3] <= 0)
        for cell in np.flatnonzero((headers[:, 0] > 0) & (headers[:, 1] == 0)):
            values = listed[cell, :headers[cell, 0]]
            assert (values < m['pointLights']).all() and len(np.unique(values)) == len(values)
            assert set(unbounded).issubset(set(values)), 'An unbounded point light must not be culled'
    print(api, label, len(selected), 'covered pixels; IDs, indices, references and finite HDR passed')
    return ids, depth

def compare_lists(reference, candidate):
    for name in ['visibility', 'depth', 'shadow-depth']:
        assert (folder(reference)/(name+'.bin')).read_bytes() == (folder(candidate)/(name+'.bin')).read_bytes(), (reference, candidate, name)
    a, b = hdr(reference), hdr(candidate)
    error = np.abs(a-b)
    assert np.all(error <= 1e-3 + 2e-3*np.maximum(np.abs(a), np.abs(b))), (candidate, error.max())
    print(api, reference, candidate, 'exact visibility/depth; HDR max error', error.max())

if len(sys.argv) > 3 and sys.argv[3] == 'camera':
    verify_ids('camera-baseline')
    verify_ids('camera-reset')
    compare_lists('camera-baseline', 'camera-reset')
    for name in ['constants', 'instances', 'materials', 'vertices', 'indices', 'grid-constants', 'lights']:
        assert (folder('camera-baseline')/(name+'.bin')).read_bytes() == (folder('camera-reset')/(name+'.bin')).read_bytes(), ('camera Reset', name)
    for first, second in [('camera-baseline', 'camera-move'), ('camera-move', 'camera-look')]:
        assert not np.array_equal(load(first, 'constants', np.float32), load(second, 'constants', np.float32))
        assert np.count_nonzero(load(first, 'depth', np.float32) != load(second, 'depth', np.float32)) > 1000
    print(api, 'camera input, exact CPU state/ID/depth Reset and bounded HDR Reset passed')
    sys.exit(0)

for label in ['base', 'all', 'cluster', 'overflow', 'stress', 'odd', 'full', 'near', 'barycentrics', 'uv-gradient', 'backface', 'black', 'moving']:
    if folder(label).exists():
        verify_ids(label)
for label in ['all', 'cluster', 'overflow']:
    if folder(label).exists():
        compare_lists('base', label)
if folder('black').exists():
    assert not hdr('black')[:, :, :3].any(), 'Black input must remain exactly black'
if folder('moving').exists():
    baseline = load('base', 'shadow-depth', np.float32)
    moved = load('moving', 'shadow-depth', np.float32)
    changed = int(np.count_nonzero(baseline != moved))
    assert changed > 100, 'The shadow map must follow the moved occluder'
    print(api, 'moved occluder changed', changed, 'shadow-depth pixels')

# Independent linear system, rather than the shader's cofactor formula.
# Evaluate thousands of actual triangles, including near-plane crossings.
for label in ['barycentrics', 'uv-gradient', 'near-barycentrics']:
    if not folder(label).exists():
        continue
    ids, depth = verify_ids(label)
    m = meta(label); w, h = m['width'], m['height']
    y, x = np.nonzero(depth < 1)
    step = max(1, len(x)//12000); x, y = x[::step], y[::step]
    instance_id = ids[y, x, 0]-1
    triangle = ids[y, x, 1] & 0x7fffffff
    raw = load(label, 'instances', np.float32).reshape(-1, 36)
    u = raw.view(np.uint32)
    vertices = load(label, 'vertices', np.float32).reshape(-1, 12).astype(np.float64)
    indices = load(label, 'indices', np.uint32)
    offsets = u[instance_id, 33] + triangle*3
    tri = vertices[u[instance_id, 32, None] + indices[offsets[:, None] + np.arange(3)]]
    world = raw[instance_id, :16].reshape(-1, 4, 4).astype(np.float64)
    constants = load(label, 'constants', np.float32).astype(np.float64)
    vp = constants[16:32].reshape(4, 4)
    clip = np.einsum('nki,nij->nkj', np.concatenate([tri[:, :, :3], np.ones((len(tri), 3, 1))], axis=2), world) @ vp
    def weights(px, py):
        q = np.stack([(px+.5)/w*2-1, 1-(py+.5)/h*2], axis=1)
        a = np.stack([clip[:, :, 0]-q[:, 0, None]*clip[:, :, 3], clip[:, :, 1]-q[:, 1, None]*clip[:, :, 3], np.ones((len(clip), 3))], axis=1)
        rhs = np.tile([0., 0., 1.], (len(clip), 1))
        return np.linalg.solve(a, rhs[:, :, None])[:, :, 0]
    bary = weights(x, y)
    assert np.allclose(bary.sum(axis=1), 1) and bary.min() >= -.015
    output = hdr(label)[y, x, :3]
    if label.endswith('barycentrics'):
        error = np.abs(output-np.clip(bary, 0, 1))
        assert np.quantile(error, .999) < .005 and error.max() < .03, (label, error.max())
        # Reproject the interpolated position and compare to hardware raster depth.
        q = np.einsum('nk,nki->ni', bary, clip)
        assert np.quantile(np.abs(q[:, 2]/q[:, 3]-depth[y, x]), .999) < 2e-5
    else:
        eps = .001
        dx = (weights(x+eps, y)-weights(x-eps, y))/(2*eps)
        dy = (weights(x, y+eps)-weights(x, y-eps))/(2*eps)
        du = np.einsum('nk,nki->ni', dx, tri[:, :, 3:5]); dv = np.einsum('nk,nki->ni', dy, tri[:, :, 3:5])
        expected = np.clip(np.stack([np.linalg.norm(du, axis=1)*32, np.linalg.norm(dv, axis=1)*32, np.zeros(len(du))], axis=1), 0, 1)
        assert np.quantile(np.abs(output-expected), .999) < .005
    print(api, label, len(x), 'independent interpolation/projection/gradient samples passed')

if folder('forward').exists():
    a, b = hdr('base'), hdr('forward')
    assert (folder('base')/'depth.bin').read_bytes() == (folder('forward')/'depth.bin').read_bytes()
    # Analytic per-pixel gradients differ from the rasterizer's quad finite differences.
    # Measure this difference instead of requiring MRT-quantized bit identity.
    difference = np.abs(a[:,:,:3]-b[:,:,:3])
    normalized = difference/(.02+np.maximum(a[:,:,:3], b[:,:,:3]))
    p99 = float(np.quantile(normalized, .99))
    assert p99 < .03, ('forward reference', p99)
    report = {'api': api, 'forwardRelativeP99': p99, 'forwardAbsoluteMean': float(difference.mean()), 'forwardAbsoluteMax': float(difference.max())}
    (work/f'{api}-visibility-verification.json').write_text(json.dumps(report, indent=2)+'\n')
    print(report)

for label in ['chapter19', 'chapter20']:
    if folder(label).exists():
        assert (folder('base')/'depth.bin').read_bytes() == (folder(label)/'depth.bin').read_bytes()
        assert (folder('base')/'shadow-depth.bin').read_bytes() == (folder(label)/'shadow-depth.bin').read_bytes()
        error = np.abs(hdr('base')[:,:,:3]-hdr(label)[:,:,:3])
        relative = error/(.02+np.maximum(hdr('base')[:,:,:3], hdr(label)[:,:,:3]))
        report = {'api': api, 'reference': label, 'absoluteMean': float(error.mean()), 'absoluteMax': float(error.max()), 'relativeP99': float(np.quantile(relative, .99))}
        assert report['relativeP99'] < .03, report
        (work/f'{api}-visibility-{label}.json').write_text(json.dumps(report, indent=2)+'\n')
        print(report)
print('Visibility Buffer GPU tests passed')
