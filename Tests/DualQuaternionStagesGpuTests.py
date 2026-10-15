"""DQS VS / Compute cache / Mesh: independent deformation, conservative bounds and rendered passes."""
import argparse,json,os,subprocess
from pathlib import Path
import numpy as np
from DualQuaternionGpuTests import reference

ROOT=Path(__file__).resolve().parents[1]
TOPICS=['24_DualQuaternionSkinning','25_DualQuaternionComputeCache','Advanced11_DualQuaternionMeshSkinning']
CLIPS=['Bind_Apose','Retarget_Tpose','UAL_Idle','UAL_Walk','UAL_Run','UAL_JumpStart','UAL_JumpLoop','UAL_JumpLand','DQS_Twist']

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--configuration',choices=['Debug','Release'],default='Debug')
    parser.add_argument('--quick',action='store_true');parser.add_argument('--api',choices=['Direct3D12','Vulkan']);args=parser.parse_args()
    work=ROOT/'.cache/skinning/dqs-stages'/args.configuration;work.mkdir(parents=True,exist_ok=True)
    results={};startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    def purge(folder):
        for file in folder.iterdir():
            if file.suffix in ('.bin','.rgba16f'):file.unlink()
    def bounds(folder,skin,expected):
        spheres=np.fromfile(folder/'skin-bounds.bin','f4').reshape(skin['actors'],-1,4)
        meshlets=np.fromfile(folder/'meshlets.bin','u4').reshape(-1,16)
        vertices=np.fromfile(folder/'meshlet-vertices.bin','u4')
        primitives=np.fromfile(folder/'mesh-primitives.bin','u4').reshape(-1,8)
        points=expected.reshape(skin['actors'],-1,12)[:,:,:3]
        worst=-float('inf');checked=0
        for primitive in primitives[:-1]:
            for m in range(int(primitive[4]),int(primitive[4]+primitive[5])):
                vi=primitive[0]+vertices[meshlets[m,0]:meshlets[m,0]+meshlets[m,2]]
                vi=vi[vi%skin['sampleStride']==0]//skin['sampleStride']
                if not len(vi):continue
                distance=np.linalg.norm(points[:,vi]-spheres[:,m,None,:3],axis=-1)
                violation=distance-spheres[:,m,None,3]
                worst=max(worst,float(violation.max()));checked+=vi.size*skin['actors']
        assert checked and worst<3e-5,(folder,'non-conservative bounds',worst)
        return {'verticesChecked':int(checked),'maximumEscape':max(0,worst)}
    def run(api,index,label,clip='UAL_Walk',at=.3,mode='dqs',actors=1,extra=(),ui=False,root=False):
        topic=TOPICS[index];name=api+'_'+topic;exe=ROOT/'bin/x64'/args.configuration/name/(name+'.exe')
        image=work/(api+'-'+label+'.png');log=image.with_suffix('.log')
        command=[str(exe),'--frames','44' if ui else '36' if root else '4','--fixed-dt','.016666667',
                 '--pause-animation','--animation',clip,'--animation-time',str(at),'--characters',str(actors),
                 '--skinning',mode,'--capture-gpu','--screenshot',str(image),'--log',str(log),*extra]
        command+=['--test-dqs'] if ui else ['--test-root-motion'] if root else ['--no-ui']
        env=os.environ.copy()
        if api=='Vulkan' and args.configuration=='Debug':env['VK_LAYER_VALIDATE_SYNC']='1'
        done=subprocess.run(command,cwd=work,env=env,startupinfo=startup,timeout=90)
        text=log.read_text(encoding='utf-8',errors='replace') if log.exists() else 'No log'
        assert done.returncode==0 and 'Validation errors: 0' in text and 'ERROR:' not in text,(label,text)
        folder=image.with_name(image.stem+'-gpu');metadata=json.loads((folder/'metadata.json').read_text())
        assert all(item['finite'] for item in metadata['images']),(label,'nonfinite render')
        skin=json.loads((folder/'skinning.json').read_text());expected,_=reference(folder,skin)
        cpu=np.fromfile(folder/'skin-reference.bin','f4').reshape(-1,12)
        gpu=np.fromfile(folder/'skin-cache.bin','f4').reshape(-1,12)
        assert np.isfinite(gpu).all() and gpu.shape==cpu.shape==expected.shape
        error={'cpuMaxError':float(np.abs(cpu-expected).max()),'gpuMaxError':float(np.abs(gpu-expected).max())}
        assert max(error.values())<3e-5,(label,error)
        if index==1:assert skin['cacheBytes']==actors*skin['verticesPerActor']*48
        else:assert skin['cacheBytes']==0
        expectedRoot='--root-motion' in extra and extra[extra.index('--root-motion')+1]=='on'
        assert skin['rootMotion']==expectedRoot,(label,'root default / reset')
        if index==2 and metadata['mesh']['renderPath']==2 and metadata['mesh']['culling']:
            error['bounds']=bounds(folder,skin,expected)
        if index==2 and metadata['mesh']['renderPath']==2:
            counters=np.fromfile(folder/'mesh-arguments.bin','u4')
            tested=[int(counters[32]),int(counters[36])];visible=[int(counters[33]),int(counters[37])]
            assert tested==[metadata['mesh']['candidateCount']]*2,(label,'task counters',tested)
            assert all(0<=v<=t for v,t in zip(visible,tested))
            error['task']={'testedShadowCamera':tested,'visibleShadowCamera':visible}
            if metadata['mesh']['culling']==0:assert visible==tested
        results[api+'-'+label]=error
        # Keep only inputs needed by later comparisons. Child UI captures are removed after assertions.
        keep={'depth.bin','shadow-depth.bin','hdr.bin','skin-palettes.bin'}
        for file in folder.iterdir():
            if file.suffix in ('.bin','.rgba16f') and file.name not in keep:file.unlink()
        print(api,label,'PASS',flush=True)
        return folder
    def compare(a,b,label,exact=False):
        report={}
        for name,dtype,tolerance in [('depth','f4',3e-6),('shadow-depth','f4',5e-6),('hdr','f2',.012)]:
            x=np.fromfile(a/(name+'.bin'),dtype).astype(float);y=np.fromfile(b/(name+'.bin'),dtype).astype(float)
            assert x.shape==y.shape and np.isfinite(x).all() and np.isfinite(y).all()
            delta=np.abs(x-y);outside=int(np.count_nonzero(delta>tolerance))
            assert (not np.any(delta)) if exact else outside<=max(16,int(x.size*.001)),(label,name,outside,float(delta.max()))
            report[name]={'maxError':float(delta.max()),'aboveTolerance':outside,'tolerance':tolerance}
        pa=a/'skin-palettes.bin';pb=b/'skin-palettes.bin'
        if pa.exists() and pb.exists():assert pa.read_bytes()==pb.read_bytes(),(label,'pose palette mismatch')
        results[label]=report
    def batch(api,label,clip='UAL_Walk',at=.3,mode='dqs',actors=1,extra=(),paths=False):
        vs=run(api,0,label+'-VS',clip,at,mode,actors,extra)
        cs=run(api,1,label+'-CS',clip,at,mode,actors,extra);compare(vs,cs,api+'-'+label+'-VS-CS');purge(cs)
        ms=run(api,2,label+'-Task',clip,at,mode,actors,extra);compare(vs,ms,api+'-'+label+'-VS-Task')
        if paths:
            for path,culling in [('mesh','off'),('task','off'),('vertex','off')]:
                folder=run(api,2,label+'-'+path+'-off',clip,at,mode,actors,(*extra,'--render-path',path,'--meshlet-culling',culling))
                compare(vs,folder,api+'-'+label+'-'+path+'-off');purge(folder)
        purge(ms);purge(vs)
    for api in ([args.api] if args.api else ['Direct3D12','Vulkan']):
        for clip in (['UAL_Walk','DQS_Twist'] if args.quick else CLIPS):
            batch(api,clip,clip,2 if clip=='DQS_Twist' else .3,paths=clip in ('UAL_Walk','DQS_Twist'))
        batch(api,'64-dqs',actors=64,paths=True)
        batch(api,'LBS',mode='lbs',paths=True)
        if not args.quick:
            batch(api,'64-lbs',mode='lbs',actors=64)
            batch(api,'root-on',extra=('--root-motion','on'),paths=True)
            batch(api,'offscreen',extra=('--camera','OffscreenCaster'),paths=True)
            task=results[api+'-offscreen-Task']['task']
            assert task['visibleShadowCamera'][0]>task['visibleShadowCamera'][1],('Camera must not remove offscreen shadow casters',task)
            for index in [1,2]:
                label='UI-'+str(index);folder=run(api,index,label,at=0,ui=True)
                def child(suffix):return work/(api+'-'+label+'-'+suffix+'-gpu')
                compare(child('baseline'),child('dqs'),api+'-'+label+'-back',True)
                compare(child('baseline'),child('reset'),api+'-'+label+'-reset',True)
                state=json.loads((child('lbs')/'skinning.json').read_text());assert state['skinning']=='lbs'
                state=json.loads((child('twist')/'skinning.json').read_text());assert state['animation']=='DQS_Twist' and state['time']==2
                x=np.fromfile(child('baseline')/'depth.bin','f4');y=np.fromfile(child('lbs')/'depth.bin','f4');assert np.any(x!=y)
                for suffix in ['baseline','lbs','dqs','twist','reset']:purge(child(suffix))
                purge(folder)
                label='RootUI-'+str(index);folder=run(api,index,label,at=0,root=True)
                base=work/(api+'-'+label+'-baseline-gpu');enabled=work/(api+'-'+label+'-enabled-gpu');disabled=work/(api+'-'+label+'-disabled-gpu');reset=work/(api+'-'+label+'-reset-gpu')
                compare(base,reset,api+'-'+label+'-reset',True)
                state=json.loads((enabled/'skinning.json').read_text());assert state['rootMotion'] and state['rootOffset'][2]<0
                state=json.loads((disabled/'skinning.json').read_text());assert not state['rootMotion']
                assert np.any(np.fromfile(enabled/'depth.bin','f4')!=np.fromfile(disabled/'depth.bin','f4'))
                for folder in [base,enabled,disabled,reset,folder]:purge(folder)
    output=work/((args.api+'-' if args.api else '')+'quick-results.json') if args.quick else work/(args.api+'-results.json') if args.api else ROOT/'Docs'/f'DualQuaternionStagesVerification-{args.configuration}.json'
    output.write_text(json.dumps(results,indent=2)+'\n')
    print('DQS VS / Compute / Mesh GPU suite PASS',args.configuration)
if __name__=='__main__':main()
