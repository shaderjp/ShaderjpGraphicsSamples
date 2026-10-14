"""Actual GPU comparisons: VS/CS/MS, independent CPU vertex reference and moving bounds."""
import argparse,json,os,subprocess,time,sys
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'SourceArt/Blender/SilverRobot'))
from validate import read,matrices,skin_positions
TOPICS=['22_VertexSkinning','23_ComputeSkinningCache','Advanced10_MeshShaderSkinning']
CLIPS=['UAL_Idle','UAL_Walk','UAL_Run','UAL_JumpStart','UAL_JumpLoop','UAL_JumpLand']

def main():
    p=argparse.ArgumentParser();p.add_argument('--configuration',choices=['Debug','Release'],default='Debug')
    p.add_argument('--quick',action='store_true');p.add_argument('--api',choices=['Direct3D12','Vulkan']);args=p.parse_args()
    work=ROOT/'.cache/skinning/gpu'/args.configuration;work.mkdir(parents=True,exist_ok=True)
    results={};startup=None
    document,accessor=read(ROOT/'Assets/Models/SilverRobotAnimated/SilverRobot.gltf')
    if os.name=='nt':startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    def run(api,topic,label,clip='UAL_Walk',at=.3,extra=(),ui=False,play=False):
        name=api+'_'+topic;exe=ROOT/'bin/x64'/args.configuration/name/(name+'.exe')
        image=work/(api+'-'+label+'.png');log=image.with_suffix('.log')
        command=[str(exe),'--frames','4','--fixed-dt','.016666667','--pause-animation','--animation',clip,
                 '--animation-time',str(at),'--no-ui','--capture-gpu','--screenshot',str(image),'--log',str(log),*extra]
        if ui:command[command.index('--frames')+1]='36';command.remove('--no-ui');command.append('--test-ui')
        if play:command.remove('--pause-animation')
        if '--test-window' in extra:command[command.index('--frames')+1]='48'
        env=os.environ.copy()
        if api=='Vulkan' and args.configuration=='Debug':env['VK_LAYER_VALIDATE_SYNC']='1'
        start=time.monotonic();completed=subprocess.run(command,cwd=work,env=env,startupinfo=startup,timeout=60)
        text=log.read_text(encoding='utf-8',errors='replace') if log.exists() else 'Missing log'
        assert completed.returncode==0 and image.exists(),(name,label,completed.returncode,text)
        assert 'Validation errors: 0' in text and 'ERROR:' not in text,(name,label,text)
        folder=image.parent/(image.stem+'-gpu');meta=json.loads((folder/'metadata.json').read_text())
        assert all(im['finite'] for im in meta.get('images',[])),(name,label,'Non-finite image')
        skin=json.loads((folder/'skinning.json').read_text());assert skin['actors'] in (1,64)
        # Independent glTF evaluator previously checked against Blender; compare C++ handedness and palette math.
        animation=next(a for a in document['animations'] if a['name']==clip)
        duration=max(float(accessor(s['input'])[-1,0]) for s in animation['samplers'])
        independent=[]
        for actor in range(skin['actors']):
            t=skin['time']+actor*duration/64
            t=t%duration if clip not in ('UAL_JumpStart','UAL_JumpLand') else min(t,duration)
            positions=np.concatenate(skin_positions(document,accessor,matrices(document,accessor,animation,t)))
            positions[:,2]*=-1;independent.append(positions[::skin['sampleStride']])
        cpu=np.fromfile(folder/'skin-reference.bin',np.float32).reshape(-1,12)[:,:3]
        error=float(np.max(np.abs(cpu-np.concatenate(independent))))
        assert error<3e-5,(api,label,'C++ vs Blender-verified glTF',error)
        results[api+'-'+label+'-independentMaxError']=error
        if topic==TOPICS[1]:
            reference=np.fromfile(folder/'skin-reference.bin',np.float32).reshape(-1,12)
            cache=np.fromfile(folder/'skin-cache.bin',np.float32).reshape(-1,12)
            assert reference.shape==cache.shape and np.isfinite(cache).all()
            error=np.max(np.abs(reference-cache),axis=0)
            assert error.max()<3e-5,(api,label,'CS vs CPU',error.tolist())
            results[api+'-'+label+'-cacheMaxError']=float(error.max())
        print(api,label,'PASS',round(time.monotonic()-start,2),'s',flush=True)
        return folder
    def compare(a,b,label):
        report={}
        for file,dtype,tolerance in [('depth',np.float32,3e-6),('shadow-depth',np.float32,3e-6),('hdr',np.float16,.012)]:
            x=np.fromfile(a/(file+'.bin'),dtype).astype(np.float32);y=np.fromfile(b/(file+'.bin'),dtype).astype(np.float32)
            assert x.shape==y.shape and np.isfinite(x).all() and np.isfinite(y).all()
            diff=np.abs(x-y);bad=diff>tolerance
            # Raster order and float arithmetic can differ at silhouette/depth ties.
            allowance=max(16,int(x.size*.001))
            assert int(bad.sum())<=allowance,(label,file,int(bad.sum()),float(diff.max()))
            report[file]={'maxDifference':float(diff.max()),'aboveTolerance':int(bad.sum()),'tolerance':tolerance}
        results[label]=report
    for api in ([args.api] if args.api else ['Direct3D12','Vulkan']):
        clips=['UAL_Walk'] if args.quick else CLIPS
        for clip in clips:
            paths=[run(api,topic,clip+'-'+str(i),clip,extra=('--meshlet-culling','off') if i==2 else ()) for i,topic in enumerate(TOPICS)]
            compare(paths[0],paths[1],api+'-'+clip+'-VS-CS');compare(paths[0],paths[2],api+'-'+clip+'-VS-MS')
            task=run(api,TOPICS[2],clip+'-task',clip)
            compare(paths[2],task,api+'-'+clip+'-animated-culling')
        if not args.quick:
            stress=[run(api,topic,'stress-'+str(i),extra=('--characters','64','--meshlet-culling','off') if i==2 else ('--characters','64')) for i,topic in enumerate(TOPICS)]
            compare(stress[0],stress[1],api+'-stress-VS-CS');compare(stress[0],stress[2],api+'-stress-VS-MS')
            task=run(api,TOPICS[2],'stress-task',extra=('--characters','64'));compare(stress[2],task,api+'-stress-culling')
            off=run(api,TOPICS[0],'shadow-off',extra=('--no-shadow',));on=work/(api+'-UAL_Walk-0-gpu')
            difference=np.abs(np.fromfile(off/'hdr.bin',np.float16).astype(np.float32)-np.fromfile(on/'hdr.bin',np.float16).astype(np.float32))
            assert (difference>.01).sum()>100,(api,'Shadow must change receiver pixels')
            caster=[run(api,topic,'offscreen-'+str(i),extra=('--camera','OffscreenCaster','--meshlet-culling','off') if i==2 else ('--camera','OffscreenCaster')) for i,topic in enumerate(TOPICS)]
            task=run(api,TOPICS[2],'offscreen-task',extra=('--camera','OffscreenCaster',));compare(caster[2],task,api+'-offscreen-caster-culling')
            for i,topic in enumerate(TOPICS):run(api,topic,'resize-'+str(i),extra=('--test-window','--test-odd'))
            # Playback produces a changed pose; a non-looping clip holds its final state.
            a=run(api,TOPICS[1],'walk-later',at=.8);b=work/(api+'-UAL_Walk-1-gpu')
            assert np.max(np.abs(np.fromfile(a/'skin-cache.bin',np.float32)-np.fromfile(b/'skin-cache.bin',np.float32)))>.01
            start=run(api,TOPICS[1],'jump-end','UAL_JumpStart',100);end=run(api,TOPICS[1],'jump-end-again','UAL_JumpStart',101)
            assert (start/'skin-cache.bin').read_bytes()==(end/'skin-cache.bin').read_bytes()
            duration=max(float(accessor(s['input'])[-1,0]) for s in next(a for a in document['animations'] if a['name']=='UAL_Walk')['samplers'])
            wrapped=run(api,TOPICS[1],'walk-wrapped',at=duration+.3)
            assert np.max(np.abs(np.fromfile(wrapped/'skin-cache.bin',np.float32)-np.fromfile(b/'skin-cache.bin',np.float32)))<3e-5
            play=run(api,TOPICS[1],'play-speed-one',at=0,play=True)
            faster=run(api,TOPICS[1],'play-speed-two',at=0,extra=('--animation-speed','2'),play=True)
            t1=json.loads((play/'skinning.json').read_text())['time'];t2=json.loads((faster/'skinning.json').read_text())['time']
            assert t1>0 and abs(t2-2*t1)<1e-6,(api,'Playback speed',t1,t2)
            for i,topic in enumerate(TOPICS):
                run(api,topic,'ui-'+str(i),at=0,ui=True)
                base=work/(api+'-ui-'+str(i)+'-baseline-gpu');edited=work/(api+'-ui-'+str(i)+'-edited-gpu');reset=work/(api+'-ui-'+str(i)+'-reset-gpu')
                first=np.fromfile(base/'hdr.bin',np.float16);changed=np.fromfile(edited/'hdr.bin',np.float16);restored=np.fromfile(reset/'hdr.bin',np.float16)
                assert (np.abs(first.astype(np.float32)-changed.astype(np.float32))>.01).sum()>100,(api,topic,'UI slider must deform scene')
                assert np.array_equal(first,restored),(api,topic,'UI Reset must restore baseline HDR')
    (work/'results.json').write_text(json.dumps(results,indent=2)+'\n')
    print('GPU skinning comparisons: PASS',flush=True)
if __name__=='__main__':main()
