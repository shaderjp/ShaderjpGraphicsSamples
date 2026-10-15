"""Sample24: GPU readback of the VS-shared function, independent NumPy DQS, and real ImGui input."""
import argparse, json, os, subprocess, sys
from pathlib import Path
import numpy as np

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'SourceArt/Blender/SilverRobot'))
from validate import read, matrices

def multiply(a, b):
    return np.concatenate((a[...,3,None]*b[...,:3]+b[...,3,None]*a[...,:3]+np.cross(a[...,:3],b[...,:3]),
                           (a[...,3]*b[...,3]-np.sum(a[...,:3]*b[...,:3],axis=-1))[...,None]),axis=-1)

def quaternion(matrix):
    # Eigenvector of the symmetric rotation matrix construction; independent of DirectXMath.
    r=matrix[:3,:3];xx,xy,xz=r[0];yx,yy,yz=r[1];zx,zy,zz=r[2]
    k=np.array([[xx-yy-zz,xy+yx,xz+zx,zy-yz],
                [xy+yx,yy-xx-zz,yz+zy,xz-zx],
                [xz+zx,yz+zy,zz-xx-yy,yx-xy],
                [zy-yz,xz-zx,yx-xy,xx+yy+zz]])/3
    return np.linalg.eigh(k)[1][:,-1]

def reference(folder, skin):
    source=np.fromfile(folder/'mesh-vertices.bin',np.float32).reshape(-1,12)[:skin['verticesPerActor']].astype(float)
    raw=(folder/'skin-influences.bin').read_bytes()
    joints=np.ndarray((skin['verticesPerActor'],4),np.uint32,buffer=raw,strides=(32,4))
    weights=np.ndarray((skin['verticesPerActor'],4),np.float32,buffer=raw,offset=16,strides=(32,4)).astype(float)
    palette=np.fromfile(folder/'skin-palettes.bin',np.float32).reshape(skin['actors'],skin['jointCount'],4,4).transpose(0,1,3,2).astype(float)
    output=[]
    for actor in range(skin['actors']):
        if skin['skinning']=='dqs':
            real=np.array([quaternion(m) for m in palette[actor]])
            translation=np.concatenate((palette[actor,:,:3,3],np.zeros((skin['jointCount'],1))),axis=1)
            dual=.5*multiply(translation,real)
            r=real[joints];d=dual[joints];anchor=r[np.arange(len(r)),np.argmax(weights,axis=1)]
            sign=np.where(np.sum(r*anchor[:,None,:],axis=-1)<0,-1,1)
            r=np.sum(r*(weights*sign)[...,None],axis=1);d=np.sum(d*(weights*sign)[...,None],axis=1)
            length=np.linalg.norm(r,axis=1)[:,None];r/=length;d/=length
            d-=r*np.sum(r*d,axis=1)[:,None];conjugate=r*np.array([-1,-1,-1,1])
            def rotate(v):
                return multiply(multiply(r,np.concatenate((v,np.zeros((len(v),1))),axis=1)),conjugate)[:,:3]
            p=rotate(source[:,:3])+2*multiply(d,conjugate)[:,:3]
            n=rotate(source[:,4:7]);t=rotate(source[:,8:11])
        else:
            blend=np.sum(palette[actor][joints]*weights[...,None,None],axis=1)
            p=np.einsum('nij,nj->ni',blend,np.concatenate((source[:,:3],np.ones((len(source),1))),axis=1))[:,:3]
            n=np.einsum('nij,nj->ni',blend[:,:3,:3],source[:,4:7])
            t=np.einsum('nij,nj->ni',blend[:,:3,:3],source[:,8:11])
        n/=np.linalg.norm(n,axis=1)[:,None];t-=n*np.sum(n*t,axis=1)[:,None];t/=np.linalg.norm(t,axis=1)[:,None]
        out=source.copy();out[:,:3]=p;out[:,4:7]=n;out[:,8:11]=t
        output.append(out[::skin['sampleStride']])
    return np.concatenate(output),palette

def main():
    parser=argparse.ArgumentParser();parser.add_argument('--configuration',choices=['Debug','Release'],default='Debug')
    parser.add_argument('--quick',action='store_true');parser.add_argument('--ui-only',action='store_true')
    parser.add_argument('--resume',action='store_true');parser.add_argument('--keep-readbacks',action='store_true');args=parser.parse_args()
    work=ROOT/'.cache/skinning/dqs-gpu'/args.configuration;work.mkdir(parents=True,exist_ok=True)
    reportPath=work/'quick-results.json' if args.quick else ROOT/'Docs'/f'DualQuaternionVerification-{args.configuration}.json'
    results=json.loads(reportPath.read_text()) if args.ui_only and reportPath.exists() else {}
    doc,accessor=read(ROOT/'Assets/Models/SilverRobotAnimated/SilverRobot.gltf')
    startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    def check(folder,clip):
        skin=json.loads((folder/'skinning.json').read_text())
        expected,palette=reference(folder,skin)
        cpp=np.fromfile(folder/'skin-reference.bin',np.float32).reshape(-1,12)
        gpu=np.fromfile(folder/'skin-cache.bin',np.float32).reshape(-1,12)
        assert np.isfinite(gpu).all() and cpp.shape==expected.shape==gpu.shape
        errors={'cpu':float(np.max(np.abs(cpp-expected))),'gpu':float(np.max(np.abs(gpu-expected)))}
        assert max(errors.values())<3e-5,(folder,errors)
        if clip!='DQS_Twist':
            animation=next(a for a in doc['animations'] if a['name']==clip)
            duration=max(float(accessor(s['input'])[-1,0]) for s in animation['samplers'])
            meshNode=next(i for i,n in enumerate(doc['nodes']) if 'skin' in n)
            definition=doc['skins'][doc['nodes'][meshNode]['skin']]
            ibm=accessor(definition['inverseBindMatrices']).reshape(-1,4,4).transpose(0,2,1)
            reflection=np.diag([1,1,-1,1])
            for actor in range(skin['actors']):
                time=skin['time']+actor*duration/64
                time=time%duration if duration>0 and clip not in ('UAL_JumpStart','UAL_JumpLand') else min(time,duration)
                pose=matrices(doc,accessor,animation,time)
                original=np.array([reflection@np.linalg.inv(pose[meshNode])@pose[node]@ibm[j]@reflection for j,node in enumerate(definition['joints'])])
                assert np.max(np.abs(original-palette[actor]))<3e-5,('palette',folder)
        return errors
    def run(api,label,mode='dqs',clip='UAL_Walk',time=.3,actors=1,extra=(),topic='24_DualQuaternionSkinning',ui=False,root_ui=False):
        name=f'{api}_{topic}';exe=ROOT/'bin/x64'/args.configuration/name/(name+'.exe')
        image=work/f'{api}-{label}.png';log=image.with_suffix('.log')
        command=[str(exe),'--frames','44' if ui else '36' if root_ui else '4','--fixed-dt','.016666667','--pause-animation',
                 '--animation',clip,'--animation-time',str(time),'--characters',str(actors),
                 '--skinning',mode,'--capture-gpu','--screenshot',str(image),'--log',str(log),*extra]
        command+=['--test-dqs'] if ui else ['--test-root-motion'] if root_ui else ['--no-ui']
        env=os.environ.copy()
        if api=='Vulkan' and args.configuration=='Debug':env['VK_LAYER_VALIDATE_SYNC']='1'
        folder=image.with_name(image.stem+'-gpu')
        oldLog=log.read_text(encoding='utf-8',errors='replace') if log.exists() else ''
        required=['metadata.json','skinning.json','depth.bin','shadow-depth.bin','hdr.bin']
        if topic=='24_DualQuaternionSkinning':required+=['skin-cache.bin','skin-reference.bin','skin-palettes.bin','skin-influences.bin','mesh-vertices.bin']
        binaryTime=max([exe.stat().st_mtime]+[file.stat().st_mtime for file in (exe.parent/'Shaders').iterdir()])
        reusable=args.resume and image.exists() and image.stat().st_mtime>=binaryTime and 'Validation errors: 0' in oldLog and 'ERROR:' not in oldLog and all((folder/file).exists() for file in required)
        done=None if reusable else subprocess.run(command,cwd=work,env=env,startupinfo=startup,timeout=60)
        text=log.read_text(encoding='utf-8',errors='replace') if log.exists() else 'No log'
        assert (done is None or done.returncode==0) and 'Validation errors: 0' in text and 'ERROR:' not in text,(label,text)
        metadata=json.loads((folder/'metadata.json').read_text())
        assert all(item['finite'] for item in metadata['images']),('nonfinite image',folder)
        if topic=='24_DualQuaternionSkinning':results[api+'-'+label]=check(folder,'UAL_Walk' if ui else clip)
        # The generic capture also emits unused Bloom/lighting dumps. Bound test disk usage.
        keep=set(required+['skin-dual-quaternions.bin','mesh-instances.bin'])
        for file in folder.iterdir():
            if file.suffix in ('.bin','.rgba16f') and file.name not in keep:file.unlink()
        print(api,label,'PASS',flush=True)
        return folder
    def compare(a,b,label,exact=True):
        report={}
        for file,dtype in [('depth','f4'),('shadow-depth','f4'),('hdr','f2')]:
            x=np.fromfile(a/(file+'.bin'),dtype);y=np.fromfile(b/(file+'.bin'),dtype)
            different=int(np.count_nonzero(x!=y));error=float(np.max(np.abs(x.astype(float)-y.astype(float))))
            assert not exact or different==0,(label,file,different,error)
            report[file]={'differentComponents':different,'maxError':error}
            if label=='API-DQS':
                tolerance={'depth':3e-6,'shadow-depth':5e-6,'hdr':.012}[file]
                outside=int(np.count_nonzero(np.abs(x.astype(float)-y.astype(float))>tolerance))
                assert outside<=max(16,int(x.size*.001)),(label,file,outside,error)
                report[file].update(tolerance=tolerance,aboveTolerance=outside)
        results[label]=report
        return report
    outputs={}
    for api in ['Direct3D12','Vulkan']:
        if not args.ui_only:
            clips=['UAL_Walk','DQS_Twist'] if args.quick else [a['name'] for a in doc['animations']]+['DQS_Twist']
            for clip in clips:
                at=2 if clip=='DQS_Twist' else .3
                outputs[api,clip]=run(api,clip,clip=clip,time=at)
            lbs=run(api,'walk-lbs',mode='lbs');old=run(api,'walk-22',mode='lbs',topic='22_VertexSkinning')
            compare(lbs,old,api+'-LBS-22')
            twistLbs=run(api,'twist-lbs',mode='lbs',clip='DQS_Twist',time=2)
            delta=compare(outputs[api,'DQS_Twist'],twistLbs,api+'-twist-difference',False)
            assert delta['depth']['differentComponents']>0 and delta['shadow-depth']['differentComponents']>0
            for mode in ['lbs','dqs']:
                run(api,'64-'+mode,mode=mode,actors=64)
        if not args.quick:
            if not args.ui_only:
                run(api,'root-on',extra=('--root-motion','on'))
                default=outputs[api,'UAL_Walk'];off=run(api,'root-off',extra=('--root-motion','off'))
                compare(default,off,api+'-root-default-off')
                for clip in ['UAL_Walk','UAL_JumpStart','DQS_Twist']:
                    run(api,clip+'-end',clip=clip,time=100)
            run(api,'UI',ui=True,time=0)
            def uiFolder(suffix):return work/f'{api}-UI-{suffix}-gpu'
            compare(uiFolder('baseline'),uiFolder('dqs'),api+'-UI-back')
            compare(uiFolder('baseline'),uiFolder('reset'),api+'-UI-reset')
            delta=compare(uiFolder('baseline'),uiFolder('lbs'),api+'-UI-LBS-change',False)
            assert delta['depth']['differentComponents']>0 and delta['shadow-depth']['differentComponents']>0
            state=json.loads((uiFolder('lbs')/'skinning.json').read_text());assert state['skinning']=='lbs'
            state=json.loads((uiFolder('twist')/'skinning.json').read_text());assert state['animation']=='DQS_Twist'
            assert state['time']==2 and not state['rootMotion']
            run(api,'RootUI',root_ui=True,time=0)
            rootBase=work/f'{api}-RootUI-baseline-gpu'
            compare(rootBase,work/f'{api}-RootUI-reset-gpu',api+'-RootUI-reset')
            delta=compare(work/f'{api}-RootUI-enabled-gpu',work/f'{api}-RootUI-disabled-gpu',api+'-RootUI-toggle',False)
            assert delta['depth']['differentComponents']>0
            enabled=json.loads((work/f'{api}-RootUI-enabled-gpu/skinning.json').read_text())
            assert enabled['rootMotion'] and enabled['rootOffset'][2]<0
            reset=json.loads((work/f'{api}-RootUI-reset-gpu/skinning.json').read_text())
            assert reset['skinning']=='dqs' and not reset['rootMotion']
    # Rasterization differences between APIs can affect a few edge pixels; retain measured counts.
    if not args.ui_only:
        compare(outputs['Direct3D12','UAL_Walk'],outputs['Vulkan','UAL_Walk'],'API-DQS',False)
    reportPath.write_text(json.dumps(results,indent=2)+'\n')
    if not args.keep_readbacks:
        # Keep PNGs, logs and JSON evidence; bulky dumps are only needed until comparisons finish.
        for folder in work.glob('*-gpu'):
            for file in folder.iterdir():
                if file.suffix in ('.bin','.rgba16f'):file.unlink()
    print('DQS GPU suite PASS',args.configuration)
if __name__=='__main__':main()
