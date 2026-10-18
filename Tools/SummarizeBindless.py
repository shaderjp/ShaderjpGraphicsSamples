"""Archive measured frames and summarize medians across repeated real runs."""
from pathlib import Path
import csv
import hashlib
import json
import math
import statistics
import sys
import zipfile
import shutil

ROOT=Path(__file__).resolve().parents[1]

def check_csv(path,report):
    with path.open() as stream:rows=list(csv.DictReader(stream))
    if len(rows)!=1200 or [int(r['frame']) for r in rows]!=list(range(1200)):raise ValueError('Incomplete frame sequence: '+path.name)
    largest=0.0
    for column in rows[0]:
        if column=='frame':continue
        category,name=column.split('-',1)
        values=sorted(float(row[column]) for row in rows)
        if not all(math.isfinite(x) and x>=0 for x in values):raise ValueError('Invalid timing value: '+path.name)
        calculated={'averageMs':statistics.fmean(values),'medianMs':values[600],'p95Ms':values[1140]}
        for stat,value in calculated.items():
            expected=report[category][name][stat];delta=abs(value-expected);largest=max(largest,delta)
            # Native CSV uses six significant digits; JSON retains the double values.
            if delta>max(1e-5,abs(expected)*1e-5):raise ValueError('CSV/JSON statistic mismatch: '+path.name+' '+column+' '+stat)
    return largest
def main():
    directory=Path(sys.argv[1]);groups={}
    for path in sorted(directory.glob('*.json')):
        d=json.loads(path.read_text(encoding='utf-8-sig'))
        if d.get('measuredFrames')!=1200:continue
        if d['configuration']!='Release' or d['validation'] or d['warmupFrames']!=60:raise ValueError('Invalid measurement conditions: '+path.name)
        key=f"{d['api']}-A{d['technique']}-{d['objects']}-{d['residentMaterials']}-{d['binding']}-{'driven' if d['gpuDriven'] else 'direct'}"
        groups.setdefault(key,[]).append((path,d))
    all_runs=[d for runs in groups.values() for p,d in runs]
    if not all_runs:raise ValueError('No measured runs')
    guarded=all(d.get('provenance',{}).get('executableVerifiedBeforeAndAfter') and d.get('provenance',{}).get('foreignActivityDetected') is False for d in all_runs)
    snapshot_ids={d.get('provenance',{}).get('runtimeSnapshotSha256') for d in all_runs}
    if guarded:
        if len(snapshot_ids)!=1 or None in snapshot_ids:raise ValueError('Mixed or missing runtime snapshots')
        audit=json.loads((directory/'CollectionAudit.json').read_text(encoding='utf-8-sig'))
        snapshot_id=hashlib.sha256((directory/'RuntimeSnapshot.json').read_bytes()).hexdigest()
        if snapshot_id not in snapshot_ids or audit['runtimeSnapshotSha256']!=snapshot_id or not audit['snapshotVerifiedAfterCollection']:raise ValueError('Runtime audit mismatch')
        for p,d in [item for runs in groups.values() for item in runs]:
            if 'Validation errors: 0' not in p.with_suffix('.log').read_text(encoding='utf-8-sig'):raise ValueError('Missing clean run log: '+p.name)
    status='collected-with-guards' if guarded else 'reference-only'
    if len({d['assetHash'] for d in all_runs})!=1:raise ValueError('Mixed asset revisions')
    for api in ['Direct3D12','Vulkan']:
        for n,topic in [(12,'BindlessMaterials'),(13,'BindlessMeshShader'),(14,'BindlessRaytracing'),(15,'BindlessHybrid')]:
            name=f'{api}_Advanced{n}_{topic}'
            fingerprints={d['environment']['binaries'][name]['executableSha256'] for d in all_runs}
            if len(fingerprints)!=1:raise ValueError('Mixed executable revisions: '+name)
    max_csv_delta=max(check_csv(p.with_suffix('.csv'),d) for runs in groups.values() for p,d in runs)
    summary={}
    for key,runs in groups.items():
        first=runs[0][1]
        if len({d['assetHash'] for p,d in runs})!=1:raise ValueError('Mixed assets: '+key)
        summary[key]={k:first[k] for k in ['api','technique','objects','residentMaterials','binding','gpuDriven','images','geometries','assetHash','comparison','memory','descriptorCount','sceneBufferResources','visibility']}
        conditions=['gpuName','driver','msvcFullVersion','windowsSdk','presentMode','configuration','validation','fixedDelta','width','height','rtShadows','rtReflections','culling','usedMaterials']
        for field in conditions:
            if any(d[field]!=first[field] for p,d in runs):raise ValueError('Mixed '+field+': '+key)
            summary[key][field]=first[field]
        if guarded:
            if any(d['measurementConditions']!=first['measurementConditions'] for p,d in runs):raise ValueError('Mixed measurement conditions: '+key)
            summary[key]['measurementConditions']=first['measurementConditions']
        summary[key].update({'measurementStatus':status,'runs':len(runs),'files':[p.name for p,d in runs],
            'cpuRecordMedianMs':statistics.median(d['cpu']['record']['medianMs'] for p,d in runs),
            'cpuRecordRunRangeMs':[min(d['cpu']['record']['medianMs'] for p,d in runs),max(d['cpu']['record']['medianMs'] for p,d in runs)],
            'gpuMedianMs':statistics.median(d['gpu']['total']['medianMs'] for p,d in runs),
            'gpuRunRangeMs':[min(d['gpu']['total']['medianMs'] for p,d in runs),max(d['gpu']['total']['medianMs'] for p,d in runs)],
            'gpuP95MedianMs':statistics.median(d['gpu']['total']['p95Ms'] for p,d in runs),
            'descriptorBinds':first['descriptorBinds'],'draws':first['draws'],'dispatches':first['dispatches']})
        if guarded:summary[key]['runtimeSnapshotSha256']=snapshot_id
        summary[key]['cpu']={field:{stat:statistics.median(d['cpu'][field][stat] for p,d in runs) for stat in ['averageMs','medianMs','p95Ms']} for field in first['cpu']}
        summary[key]['gpu']={field:{stat:statistics.median(d['gpu'][field][stat] for p,d in runs) for stat in ['averageMs','medianMs','p95Ms']} for field in first['gpu']}
    out=ROOT/'Docs'/'BindlessPerformance.json';out.write_text(json.dumps(summary,indent=2)+'\n',encoding='utf-8')
    archive=ROOT/'Docs/Measurements/Bindless/Frames.zip';archive.parent.mkdir(parents=True,exist_ok=True)
    with zipfile.ZipFile(archive,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9) as z:
        if (directory/'Environment.json').is_file():z.write(directory/'Environment.json','Environment.json')
        for name in ['RuntimeSnapshot.json','CollectionAudit.json']:
            if (directory/name).is_file():z.write(directory/name,name)
        for runs in groups.values():
            for p,d in runs:
                z.write(p,p.name);csv=p.with_suffix('.csv')
                if len(csv.read_text().splitlines())!=1201:raise ValueError('Frame CSV incomplete: '+csv.name)
                z.write(csv,csv.name)
                if p.with_suffix('.log').is_file():z.write(p.with_suffix('.log'),p.with_suffix('.log').name)
    manifest={'measurementStatus':status,'archive':'Frames.zip','sha256':hashlib.sha256(archive.read_bytes()).hexdigest(),'runs':sum(map(len,groups.values())),'conditions':len(groups),'repeats':sorted(set(len(r) for r in groups.values())),'frameCount':sum(len(r)*1200 for r in groups.values())}
    if guarded:
        for name in ['RuntimeSnapshot.json','CollectionAudit.json']:shutil.copyfile(directory/name,archive.parent/name)
        manifest['runtimeSnapshotSha256']=snapshot_id
        manifest['collectionAudit']='CollectionAudit.json'
    (archive.parent/'Manifest.json').write_text(json.dumps(manifest,indent=2)+'\n')
    first_pass=archive.parent/'FirstPass/Manifest.json'
    first_preserved=first_pass.is_file() and hashlib.sha256((first_pass.parent/'Frames.zip').read_bytes()).hexdigest()==json.loads(first_pass.read_text())['sha256']
    checks={'runs':len(all_runs),'conditions':len(groups),'measuredFrames':len(all_runs)*1200,'csvMetricsChecked':len(all_runs)*11*3,
            'maximumCsvRoundingDeltaMs':max_csv_delta,'csvToleranceAbsoluteMs':1e-5,'csvToleranceRelative':1e-5,
            'runtimeSnapshotSha256':manifest.get('runtimeSnapshotSha256'),'archiveSha256':manifest['sha256'],
            'allFramesFiniteAndNonnegative':True,'actualMaterialCountsMatch':all(d['usedMaterials']==d['residentMaterials'] for d in all_runs),
            'allLogsClean':guarded,'firstPassPreserved':first_preserved}
    (archive.parent/'DataChecks.json').write_text(json.dumps(checks,indent=2)+'\n')
    print(f'Summarized {sum(len(v) for v in groups.values())} benchmark runs in {len(summary)} conditions')
if __name__=='__main__':main()
