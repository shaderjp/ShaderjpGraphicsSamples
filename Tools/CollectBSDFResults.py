"""Collect completed GPU checks and capture provenance; never edits rendered images."""
from pathlib import Path
from datetime import datetime
import argparse, hashlib, json, struct

ROOT=Path(__file__).resolve().parents[1]

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--results',type=Path,default=ROOT/'.cache/bsdf19/gpu')
    parser.add_argument('--environment',type=Path,required=True,help='Environment JSON recorded for this test run')
    args=parser.parse_args()
    records={}
    for configuration in ('Debug','Release'):
        records[configuration]={}
        for api in ('Direct3D12','Vulkan'):
            path=args.results/configuration/f'{api}-results.json'
            result=json.loads(path.read_text())
            assert result['result']=='PASS',path
            assert result['nativeCameraAndPauseResume'],path
            assert result['customDefaultsReset'],path
            records[configuration][api]=result
    captures=[]
    for api in ('Direct3D12','Vulkan'):
        for label in ('full','reflection','legacy','thickness','ior','roughness','inside','full-1','full-16','full-256','panel'):
            suffix='' if label=='panel' else '-'+label
            path=ROOT/f'Docs/{api}/Images/Advanced19_BSDFMaterials{suffix}.png'
            data=path.read_bytes()
            assert data[:8]==b'\x89PNG\r\n\x1a\n'
            assert struct.unpack('>II',data[16:24])==(1920,1080),path
            metadata=json.loads((args.results/'Release'/f'{api}-{label}-gpu/metadata.json').read_text())
            if label in ('full','reflection','legacy','thickness','ior','roughness'):
                assert metadata['spp']==1024 and metadata['bounces']==16 and metadata['seed']==1
            assert metadata['width']==1920 and metadata['height']==1080
            # Record the executable and integration conditions as durable provenance.
            conditions={key:value for key,value in metadata.items() if key!='technique'}
            conditions['exposure']=1
            captures.append({'api':api,'program':f'{api}_Advanced19_BSDFMaterials','file':path.relative_to(ROOT).as_posix(),
                'sha256':sha(path),'bytes':len(data),'conditions':conditions})
    sources=[*sorted((ROOT/'Shaders/BSDFMaterials').glob('*')),ROOT/'Shaders/PathTracing/BSDF.hlsli',
        ROOT/'Assets/Scenes/BSDFMaterials/Manifest.json']
    report={'schema':'bsdf-materials-validation-1','recordedAt':datetime.now().astimezone().isoformat(),
        'environment':json.loads(args.environment.read_text(encoding='utf-8')),
        'results':records,'captures':captures,'sourceHashes':{p.relative_to(ROOT).as_posix():sha(p) for p in sources if p.is_file()}}
    path=ROOT/'Docs/BSDFMaterialsResults.json'
    path.write_text(json.dumps(report,ensure_ascii=False,indent=2)+'\n',encoding='utf-8',newline='\n')
    print(f'Collected 4 completed API/configuration results and {len(captures)} verified 1080p captures: {path}')

if __name__=='__main__':
    main()
