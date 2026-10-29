"""Retain completed Geometry Lab validation evidence without GPU readback binaries."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import zipfile


ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.cache/cluster-reference'
DESTINATION = ROOT / 'Docs/Measurements/ClusterGeometry'


def read(path):
    return json.loads(path.read_text(encoding='utf-8-sig'))


def main():
    assert 'ALL LEGACY REGRESSIONS PASS' in (WORK/'regression-final-progress.log').read_text()
    environment = read(ROOT/'Docs/ClusterGeometryEnvironment.json')
    measurements = read(DESTINATION/'Summary.json')
    assert len(measurements['cases']) == 13
    assert (measurements['warmup'], measurements['framesPerRun'], measurements['repeats']) == (120, 600, 5)
    results = {'result': 'PASS', 'environmentCapturedUtc': environment['capturedUtc'],
               'apiApplications': 102, 'supportProjects': 3, 'gpuCasesPerConfiguration': 32,
               'expectedFailureCasesPerConfiguration': 21, 'lifecycle': {}, 'gpu': {}, 'images': {},
               'limitations': ['RTX 4070 / driver 621.01 only',
                               'Unsupported-device branch tested by an explicit failure hook',
                               'Memory values are logical buffer capacities, not physical VRAM usage']}
    evidence = set()
    for configuration in ['Debug', 'Release']:
        summary = ROOT/f'.cache/cluster-gpu/{configuration}/Summary.json'
        gpu = read(summary)
        assert len(gpu['comparisons']) == 13 and len(gpu['geometry']) == 16 and gpu['ui'] == [21, 22, 23]
        results['gpu'][configuration] = gpu
        shutil.copyfile(summary, ROOT/f'Docs/ClusterGeometryGpu{configuration}.json')
        tested = []
        for api in ['Direct3D12', 'Vulkan']:
            for sample in sorted((ROOT/f'Samples/{api}').iterdir()):
                if not sample.is_dir():
                    continue
                name = f'{api}_{sample.name}'
                log = ROOT/f'.cache/validation/{configuration}/{name}.log'
                text = log.read_text(encoding='utf-8-sig')
                assert 'Validation errors: 0' in text and 'D3D12 warning:' not in text, name
                assert 'Minimize/restore test' in text and 'Screenshot:' in text, name
                if configuration == 'Debug':
                    assert 'Layer enabled' in text, name
                tested.append(name)
                evidence.add(log)
        assert len(tested) == 102
        results['lifecycle'][configuration] = tested
        cpu = WORK/f'cpu-{configuration.lower()}-tests.log'
        assert cpu.read_text(encoding='utf-8-sig').count('CPU geometry, topology and capacity PASS') == 4
        evidence.add(cpu)
        for subtree in ['cluster-gpu', 'cluster-failures', 'cluster-docs']:
            folder = ROOT/f'.cache/{subtree}/{configuration}'
            evidence.update(folder.rglob('*.log'))
            evidence.update(folder.rglob('metadata.json'))
    manifest = read(ROOT/'Assets/Models/GeometryLab/Manifest.json')
    for group in ['files', 'sources']:
        for name, info in manifest[group].items():
            path = (ROOT/'Assets/Models/GeometryLab'/name) if group == 'files' else ROOT/name
            assert path.stat().st_size == info['bytes']
            assert hashlib.sha256(path.read_bytes()).hexdigest() == info['sha256'], name
    for path in sorted((ROOT/'Docs/Direct3D12/Images').glob('Advanced2[0-3]_*.png')):
        data = path.read_bytes()
        assert data[:8] == b'\x89PNG\r\n\x1a\n' and struct.unpack('>II', data[16:24]) == (1920, 1080)
        results['images'][path.relative_to(ROOT).as_posix()] = hashlib.sha256(data).hexdigest()
    assert len(results['images']) == 11
    for name in ['complete-debug-build.log', 'complete-release-build.log', 'final-restore.log', 'projects.log',
                 'loaders.log', 'asset-regenerate.log', 'new-debug-lifecycle.log', 'capture-docs.log',
                 'new-release-lifecycle.log', 'final-profile-failure-tests.log',
                 'runtime-versions.log', 'measurement-progress.log',
                 'regression-final-progress.log']:
        evidence.add(WORK/name)
    evidence.update(WORK.glob('regression-*-*.log'))
    DESTINATION.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(DESTINATION/'ValidationLogs.zip', 'w', zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(evidence):
            archive.write(path, path.relative_to(ROOT/'.cache').as_posix())
    (ROOT/'Docs/ClusterGeometryResults.json').write_text(json.dumps(results, indent=2)+'\n', encoding='utf-8')
    print('Retained 204 lifecycle cases, GPU/CPU results, 11 image hashes and validation logs')


if __name__ == '__main__':
    main()
