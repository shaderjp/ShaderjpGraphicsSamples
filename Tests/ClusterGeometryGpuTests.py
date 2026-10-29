"""Numerical comparisons of independent conventional/clustered AS builds.
Input files are read back from the actual GPU; PNG/UI pixels are not used as an oracle.
"""
import json
from pathlib import Path
import sys
import numpy as np


def load(root, label):
    directory = root / (label + '-gpu')
    meta = json.loads((directory / 'metadata.json').read_text())
    shape = (meta['height'], meta['width'], 4)
    return (meta, np.fromfile(directory/'hdr.bin', '<f2').reshape(shape).astype(np.float32),
            np.fromfile(directory/'surface.bin', '<f4').reshape(shape),
            np.fromfile(directory/'ids.bin', '<f4').reshape(shape), directory)


def validate_geometry(item):
    meta, hdr, surface, ids, directory = item
    assert np.isfinite(hdr).all() and np.isfinite(surface).all() and np.isfinite(ids).all()
    gpu = np.fromfile(directory/'clusters.bin', '<u4').reshape(-1, 8)
    cpu = np.asarray(meta['clusterCpuReference'], dtype=np.uint32)
    assert np.array_equal(gpu, cpu), 'GPU LOD/template selection differs from independent CPU selection'
    rest = np.fromfile(directory/'rest-vertices.bin', '<f4').reshape(-1, 12)
    current = np.fromfile(directory/'vertices.bin', '<f4').reshape(-1, 12)
    assert np.isfinite(current).all()
    expected = rest.copy()
    for cluster in cpu:
        first, _, _, count, _, _, motion, _ = map(int, cluster)
        if not motion:
            continue
        count = 81 if motion == 2 else count
        src = rest[first:first+count]
        dst = expected[first:first+count]
        x, z = src[:, 0], src[:, 2]
        if motion == 1:
            phase = x*1.7 + meta['time']
            weight = src[:, 7]
            span = np.asarray([cluster[7]], dtype='<u4').view('<f4')[0]
            assert span > 0
            gx = -src[:, 4]/src[:, 6] + .85*np.cos(phase)*weight
            gy = -src[:, 5]/src[:, 6] + .5*np.sin(phase)/span
            dst[:, 2] += .5*np.sin(phase)*weight
            normal = np.stack([-gx, -gy, np.ones(count)], axis=1)
            tangent = np.stack([np.ones(count), np.zeros(count), gx], axis=1)
        else:
            a, b, c = x*1.3+meta['time']*.5, z*1.1, x*8+z*6
            dst[:, 1] = .8 + .3*np.sin(a)*np.cos(b)+.08*np.sin(c)
            dx, dz = .39*np.cos(a)*np.cos(b)+.64*np.cos(c), -.33*np.sin(a)*np.sin(b)+.48*np.cos(c)
            normal = np.stack([-dx, np.ones(count), -dz], axis=1)
            tangent = np.stack([np.ones(count), dx, np.zeros(count)], axis=1)
        dst[:, 4:7] = normal/np.linalg.norm(normal, axis=1, keepdims=True)
        dst[:, 8:11] = tangent/np.linalg.norm(tangent, axis=1, keepdims=True)
    np.testing.assert_allclose(current, expected, atol=2e-5, rtol=2e-5, err_msg='GPU vertex/normal generation')
    assert np.count_nonzero(surface[:, :, 2] > 0) > meta['width']*meta['height']*.1


def compare(first, second):
    ma, ha, sa, ia, _ = first
    mb, hb, sb, ib, _ = second
    assert (ma['width'], ma['height']) == (mb['width'], mb['height'])
    valid = sa[:, :, 2] > 0
    # Exclude a one-pixel primary-geometry boundary; exact edge tie breaking is unspecified.
    for axis in [0, 1]:
        for direction in [-1, 1]:
            valid &= np.all(ia == np.roll(ia, direction, axis=axis), axis=2)
    valid[[0, -1], :] = False
    valid[:, [0, -1]] = False
    assert valid.sum() > 1000
    assert np.mean(np.all(ia[valid] == ib[valid], axis=1)) > .999, 'Hit IDs differ away from edges'
    np.testing.assert_allclose(sa[valid, 2], sb[valid, 2], rtol=5e-5, atol=1e-4)
    error = np.max(np.abs(ha[:, :, :3]-hb[:, :, :3]), axis=2)
    scale = np.max(np.abs(ha[:, :, :3]), axis=2)
    # A subpixel change at a texture, shadow or reflection edge can have a large
    # radiance change. Check smooth pixels tightly and retain a whole-region RMS bound.
    smooth = valid.copy()
    for axis in [0, 1]:
        for direction in [-1, 1]:
            gradient = np.max(np.abs(ha[:, :, :3]-np.roll(ha[:, :, :3], direction, axis=axis)), axis=2)
            smooth &= gradient < .03+.03*scale
    assert smooth.sum() > 1000
    fraction = np.mean(error[smooth] <= .002+.002*scale[smooth])
    assert fraction > .995, f'Linear HDR agreement {fraction:.6f}'
    rms = np.sqrt(np.mean((ha[valid, :3]-hb[valid, :3])**2))/max(np.sqrt(np.mean(ha[valid, :3]**2)), .01)
    assert rms < .025, f'Linear HDR normalized RMS {rms:.6f}'
    return {'smoothAgreement': float(fraction), 'normalizedRms': float(rms)}


def main():
    root = Path(sys.argv[1])
    summary = {'comparisons': {}, 'geometry': [], 'ui': []}
    for technique, modes in [(20, ['clusters', 'rebuild']), (21, ['templates', 'direct', 'rebuild', 'refit']),
                             (22, ['templates', 'direct', 'rebuild']), (23, ['ptlas', 'rebuild', 'refit'])]:
        baseline = None
        for mode in modes:
            label = f'{technique}-{mode}'
            item = load(root, label)
            validate_geometry(item)
            summary['geometry'].append(label)
            if baseline is None:
                baseline = item
            else:
                summary['comparisons'][label] = compare(baseline, item)
            if technique == 23:
                assert item[0]['writes'] == 0, 'Paused scene must settle to zero sparse writes'
        if technique == 20:
            meta, _, surface, ids, _ = baseline
            h, w = meta['height'], meta['width']
            eye = np.array([9, 6, 12.], dtype=np.float64)
            forward = np.array([0, 1.6, 0])-eye
            forward /= np.linalg.norm(forward)
            right = np.cross([0, 1, 0], forward)
            right /= np.linalg.norm(right)
            up = np.cross(forward, right)
            yy, xx = np.mgrid[:h, :w]
            ndcx, ndcy = (xx+.5)/w*2-1, 1-(yy+.5)/h*2
            rays = forward + ndcx[:, :, None]*right*np.tan(np.pi/8)*w/h + ndcy[:, :, None]*up*np.tan(np.pi/8)
            rays /= np.linalg.norm(rays, axis=2, keepdims=True)
            mask = (ids[:, :, 2] == 2) & (surface[:, :, 2] > 0)
            assert mask.sum() > 100
            expected = (1-eye[1])/rays[:, :, 1]
            # The GPU ray uses a float32 inverse view-projection matrix. Its
            # direction differs slightly from this independent float64 camera.
            # Keep the plane-distance bound below 0.7 mm at these scene distances.
            np.testing.assert_allclose(surface[mask, 2], expected[mask], rtol=3e-5, atol=1e-4)
            summary['analyticPlaneDistance'] = {
                'maxAbsoluteErrorMetres': float(np.max(np.abs(surface[mask, 2]-expected[mask]))),
                'maxRelativeError': float(np.max(np.abs(surface[mask, 2]-expected[mask])/expected[mask]))}
    for rate in [2, 4, 8]:
        a, b = load(root, f'22-lod{rate}-templates'), load(root, f'22-lod{rate}-rebuild')
        validate_geometry(a)
        summary['comparisons'][f'22-lod{rate}'] = compare(a, b)
    validate_geometry(load(root, '21-showcase'))
    for technique in [20, 21, 22, 23]:
        validate_geometry(load(root, f'{technique}-stress'))
        summary['geometry'].append(f'{technique}-stress')
    for policy in ['spatial', 'global']:
        a, b = load(root, f'23-{policy}-moving'), load(root, f'23-{policy}-reference')
        summary['comparisons'][f'23-{policy}'] = compare(a, b)
        assert 0 < a[0]['writes'] < a[0]['instances'], 'Sparse updates must be a nonempty subset'
    assert load(root, '23-inactive')[0]['writes'] == 0
    assert load(root, '23-all-active')[0]['writes'] > 4000
    for technique in [21, 22, 23]:
        before = load(root, f'{technique}-ui-baseline')
        stepped = load(root, f'{technique}-ui-stepped')
        reset = load(root, f'{technique}-ui-reset')
        assert stepped[0]['time'] > before[0]['time'], 'Step widget did not advance time'
        assert reset[0]['time'] == before[0]['time'], 'Reset widget did not restore initial time'
        assert np.max(np.abs(before[1]-stepped[1])) > .001, 'Step must change rendered GPU scene pixels'
        np.testing.assert_allclose(before[1], reset[1], atol=.001, rtol=.001)
        summary['ui'].append(technique)
    (root/'Summary.json').write_text(json.dumps(summary, indent=2)+'\n')
    print('GPU AS comparisons, analytic geometry, LOD, sparse updates and UI PASS')


if __name__ == '__main__': main()
