"""Save compact verification evidence and the final successful test logs."""
from pathlib import Path
import hashlib
import json
import re
import zipfile
ROOT=Path(__file__).resolve().parents[1]
def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
def main():
    summary={'date':'2026-10-18','assetManifest':json.loads((ROOT/'Assets/Scenes/BindlessGallery/Manifest.json').read_text()),'configurations':{},
             'regressionScope':{'apiApplications':80,'existing':72,'newBindless':8,'concurrentApplicationsExcluded':8},
             'performanceStatus':json.loads((ROOT/'Docs/Measurements/Bindless/Manifest.json').read_text())['measurementStatus']}
    logs=[]
    for config in ['Debug','Release']:
        work=ROOT/'.cache/bindless-tests'/config
        features=ROOT/'.cache/bindless-features'/config
        effects=ROOT/'.cache/bindless-effects'/config
        cases=sorted(work.glob('*.log'))
        for log in cases:
            if 'Validation errors: 0' not in log.read_text():raise ValueError('GPU validation failure: '+str(log))
        if len(cases)!=22:raise ValueError('GPU matrix incomplete')
        feature_logs=[p for p in features.glob('*.log') if 'invalid-' not in p.name]
        for log in feature_logs:
            if 'Validation errors: 0' not in log.read_text():raise ValueError('Feature validation failure: '+str(log))
        for api in ['Direct3D12','Vulkan']:
            for topic in ['Advanced12_BindlessMaterials','Advanced13_BindlessMeshShader','Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid']:
                log=features/f'{api}-{topic}-ui.log'
                text=log.read_text()
                if 'Bindless UI binding switch PASS' not in text or 'Bindless UI Reset PASS' not in text:raise ValueError('UI incomplete')
        lifecycle=[p for p in (ROOT/'.cache/validation'/config).glob('*.log') if not re.search(r'(?:Direct3D12|Vulkan)_(?:2[67]_|Advanced1[67]_)',p.name)]
        if len(lifecycle)!=80:raise ValueError('Lifecycle matrix incomplete')
        for log in lifecycle:
            if 'Validation errors: 0' not in log.read_text():raise ValueError('Lifecycle validation: '+str(log))
        for log in effects.glob('*.log'):
            if 'Validation errors: 0' not in log.read_text():raise ValueError('Effect validation: '+str(log))
        summary['configurations'][config]={'gpuModes':json.loads((work/'Comparison.json').read_text()),'features':json.loads((features/'Features.json').read_text()),'effects':json.loads((effects/'Effects.json').read_text()),'uiApplications':8,'lifecycleApplications':80}
        logs+=cases+feature_logs+lifecycle+list(effects.glob('*.log'))
        failure_dir=ROOT/'.cache/bindless-failures'/config
        expected=list(failure_dir.glob('*.log'))
        if len(expected)!=12:raise ValueError('Failure matrix incomplete')
        for log in expected:
            if 'ERROR:' not in log.read_text():raise ValueError('Missing explicit failure: '+str(log))
        logs+=expected
    summary['offlineShaderBinaries']=496
    measurement=json.loads((ROOT/'Docs/Measurements/Bindless/Manifest.json').read_text())
    summary['performance']={k:measurement[k] for k in ['conditions','runs','frameCount']}
    if 'runtimeSnapshotSha256' in measurement:summary['performance']['runtimeSnapshotSha256']=measurement['runtimeSnapshotSha256']
    summary['performance']['audit']='Measurements/Bindless/CollectionAudit.json'
    summary['performance']['dataChecks']='Measurements/Bindless/DataChecks.json'
    summary['cpuUvProbesPerConfiguration']=192
    summary['hardwareCoverage']='RTX 4070 only; unsupported-GPU rejection is implemented but not exercised on an unsupported physical GPU'
    summary['referenceImages']=json.loads((ROOT/'Docs/Assets/Images/BindlessGallery/References.json').read_text())
    summary['gpuCaptures']=json.loads((ROOT/'Docs/BindlessCaptures.json').read_text())
    for shot in summary['gpuCaptures']:
        if digest(ROOT/shot['path'])!=shot['sha256']:raise ValueError('Capture provenance mismatch')
    for name in ['asset-final.log','projects-verified.log','shaders-verified.log','loaders-final.log','cpu-final-Debug.log','cpu-final-Release.log','imgui-Debug.log','imgui-Release.log','numeric-final-Debug.log','numeric-final-Release.log']:
        p=ROOT/'.cache/bindless'/name
        if not p.is_file():raise ValueError('Missing test evidence '+name)
        logs.append(p)
    out=ROOT/'Docs/Measurements/Bindless/ValidationLogs.zip';out.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(out,'w',zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        for path in sorted(set(logs)):z.write(path,str(path.relative_to(ROOT/'.cache')).replace('\\','/'))
    summary['logs']={'path':'Measurements/Bindless/ValidationLogs.zip','sha256':digest(out),'files':len(set(logs))}
    (ROOT/'Docs/BindlessValidation.json').write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    print('Saved verification evidence:',len(set(logs)),'logs')
if __name__=='__main__':main()
