"""Real GPU oracle: compare IDs, depth and HDR against uncullled rasterization."""
from pathlib import Path
import argparse
import json
import os
import subprocess
import shutil
import copy
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
TOPICS=['31_FrustumCulling','32_OcclusionQueries','33_SoftwareOcclusion','34_HiZOcclusion']
MODES=['frustum','query','software','hiz']

def make_fixtures(directory):
    """Reuse authored primitives, with analytically known camera/occluder relationships."""
    model_dir=directory/'fixtures'
    shutil.copytree(ROOT/'Assets/Models/CullingLab',model_dir,dirs_exist_ok=True)
    source=json.loads((model_dir/'CullingLab.gltf').read_text())
    target=next(n['mesh'] for n in source['nodes'] if n.get('extras',{}).get('id')=='object:Mini:0000')
    wall=next(n['mesh'] for n in source['nodes'] if n.get('extras',{}).get('id')=='occluder:Mini_0_0_BackLeft')
    light=copy.deepcopy(next(n for n in source['nodes'] if 'KHR_lights_punctual' in n.get('extensions',{})))
    camera=dict(position=[0,1,0],target=[0,1,-6],fovDegrees=60,near=.05,far=160)
    cases=[]
    # Hidden sits away from the wall's triangle diagonal: the reference CPU rasterizer deliberately
    # leaves diagonal cells uncovered, and retaining an object on that seam is valid conservatism.
    for name,xs,target_position in [('hidden',[0],[0,2.8,-6]),('partial',[1.5],[0,1,-6]),
        ('gap',[-1.45,1.45],[0,1,-6]),('all-visible',[],[0,1,-6]),
        ('empty',[],[100,1,-6]),('inside',[],[0,1,0])]:
        data=copy.deepcopy(source)
        nodes=[light,dict(mesh=target,translation=target_position,extras={'id':'object:fixture:0000'})]
        for i,x in enumerate(xs):nodes.append(dict(mesh=wall,translation=[x,1,-3],extras={'id':f'occluder:fixture:{i}'}))
        data['nodes']=nodes;data['scenes']=[dict(name='Standard',nodes=list(range(len(nodes))),extras={'id':'scene:Standard'})];data['scene']=0
        (model_dir/(name+'.gltf')).write_text(json.dumps(data))
        scene=dict(profile='workshop-scene-1',model=name+'.gltf',cameras={'Hero':camera},
            environment={'prefix':str(ROOT/'Assets/Environments/Workshop/WorkshopSky')},exposure=1)
        path=model_dir/(name+'.json');path.write_text(json.dumps(scene))
        cases.append(('fixture-'+name,['--scene',str(path)]))
    (model_dir/'CameraPath.json').write_text(json.dumps({'frames':[camera]*1201}))
    return cases

def run(api,topic,config,directory,name,options,view='ids'):
    prefix=directory/(api+'_'+name)
    exe=ROOT/'bin/x64'/config/(api+'_'+topic)/(api+'_'+topic+'.exe')
    command=[str(exe),'--frames','2','--capture-gpu','--culling-view',view,
        '--screenshot',str(prefix.with_suffix('.png')),'--log',str(prefix.with_suffix('.log'))]+options
    if '--test-ui' not in options:command.append('--no-ui')
    env=os.environ.copy()
    if config=='Debug':env['VK_LAYER_VALIDATE_SYNC']='1'
    startup=subprocess.STARTUPINFO();startup.dwFlags=subprocess.STARTF_USESHOWWINDOW;startup.wShowWindow=0
    result=subprocess.run(command,cwd=directory,env=env,startupinfo=startup,timeout=90,capture_output=True)
    log=prefix.with_suffix('.log').read_text(encoding='utf-8',errors='replace')
    if result.returncode or 'Validation errors: 0' not in log:raise RuntimeError(str(command)+'\n'+log)
    gpu=prefix.parent/(prefix.name+'-gpu')
    hdr=np.fromfile(gpu/'hdr.rgba16f',dtype='<f2').astype(np.float32).reshape(-1,4)
    depth=np.fromfile(gpu/'depth.bin',dtype='<f4')
    stats=json.loads(prefix.with_suffix('.culling.json').read_text())
    if not np.isfinite(hdr).all() or not np.isfinite(depth).all():raise AssertionError('Nonfinite output')
    return hdr,depth,stats,gpu

def validate_pyramid(directory):
    info=json.loads((directory/'pyramid.json').read_text())
    data=np.fromfile(directory/'pyramid.f32',dtype='<f4')
    w,h=info['width'],info['height'];offset=0;previous=data[:w*h].reshape(h,w)
    assert np.all(previous[:,info['viewportWidth']:]==1) and np.all(previous[info['viewportHeight']:,:]==1),'Padding must be far'
    assert np.isfinite(data).all() and np.all((data>=0)&(data<=1))
    while w>1 or h>1:
        offset+=w*h; nw,nh=max(1,w//2),max(1,h//2)
        # Singleton dimensions duplicate their last child, just like the GPU kernel.
        padded=np.repeat(previous,2,axis=0) if h==1 else previous
        padded=np.repeat(padded,2,axis=1) if w==1 else padded
        expected=padded.reshape(nh,2,nw,2).max(axis=(1,3))
        actual=data[offset:offset+nw*nh].reshape(nh,nw)
        assert np.array_equal(actual,expected),'Hi-Z level differs from CPU maximum reduction'
        w,h,previous=nw,nh,actual
    assert offset+1==len(data)==info['count']

def verify_ui(api,config,directory):
    """Drive actual ImGui mouse input; compare HDR before edits and after both resets."""
    results=[]
    for topic,mode in zip(TOPICS,MODES):
        run(api,topic,config,directory,'ui-'+mode,['--frames','48','--test-ui'],view='final')
        prefix=directory/(api+'_ui-'+mode)
        states={}
        images={}
        for stage in ['baseline','edited','reset','off','mode-reset']:
            path=prefix.parent/(prefix.name+'-'+stage)
            states[stage]=json.loads(path.with_suffix('.culling.json').read_text())
            images[stage]=np.fromfile(path.parent/(path.name+'-gpu')/'hdr.rgba16f',dtype='<f2')
        assert states['baseline']['mode']==mode and states['off']['mode']=='off','Culling combo input did not switch mode'
        assert states['reset']['mode']==mode and states['mode-reset']['mode']==mode,'Reset did not restore default mode'
        changed=int(np.count_nonzero(images['edited']!=images['baseline']))
        assert changed>0,'Door slider did not change rendered HDR'
        reference,_,_,_=run(api,TOPICS[0],config,directory,'ui-cli-'+mode,
            ['--culling','off','--culling-door',str(states['edited']['doorX'])],view='final')
        assert np.array_equal(reference.reshape(-1),images['edited']),'UI and CLI door state differ from uncullled drawing'
        for stage in ['reset','off','mode-reset']:
            assert np.array_equal(images[stage],images['baseline']),f'{api}/{topic}: {stage} did not restore HDR'
        results.append(dict(api=api,mode=mode,changedHdrComponents=changed,resetDifference=0))
        print(api,mode,'UI / door / OFF / Reset PASS',flush=True)
    return results

def verify_motion(api,config,directory):
    """The Win32 test injects movement, a sudden look rotation, and camera Reset in one process."""
    options=['--frames','32','--test-camera','--fixed-dt','0.016666667']
    run(api,TOPICS[0],config,directory,'motion-off',options+['--culling','off'])
    results=[]
    for topic,mode in zip(TOPICS,MODES):
        run(api,topic,config,directory,'motion-'+mode,options+['--culling',mode])
        for stage in ['baseline','move','look','reset']:
            prefix=directory/(api+'_motion-'+mode+'-camera-'+stage)
            reference=directory/(api+'_motion-off-camera-'+stage)
            actual=np.fromfile(prefix.parent/(prefix.name+'-gpu')/'hdr.rgba16f',dtype='<f2').reshape(-1,4)
            expected=np.fromfile(reference.parent/(reference.name+'-gpu')/'hdr.rgba16f',dtype='<f2').reshape(-1,4)
            assert np.array_equal(actual,expected),f'{api}/{mode}/{stage}: camera motion changed ID image'
            ids=np.rint(expected[:,0].astype(np.float32)*255).astype(np.uint32)+(np.rint(expected[:,1].astype(np.float32)*255).astype(np.uint32)<<8)
            stats=json.loads(prefix.with_suffix('.culling.json').read_text())
            assert np.all(np.asarray(stats['visible'])[np.unique(ids[ids>0])-1])
            actual_z=np.fromfile(prefix.parent/(prefix.name+'-gpu')/'depth.bin',dtype='<f4')
            reference_z=np.fromfile(reference.parent/(reference.name+'-gpu')/'depth.bin',dtype='<f4')
            assert np.max(np.abs(actual_z-reference_z))<=1e-6
            results.append(dict(api=api,mode=mode,stage=stage,missingObjects=0,idMismatchPixels=0))
        print(api,mode,'camera motion / sudden turn / Reset PASS',flush=True)
    return results

def verify_resize(api,config,directory):
    options=['--frames','48','--test-odd']
    reference,z,_,_=run(api,TOPICS[0],config,directory,'resize-off',options+['--culling','off'])
    results=[]
    for topic,mode in zip(TOPICS,MODES):
        actual,depth,_,gpu=run(api,topic,config,directory,'resize-'+mode,options+['--culling',mode])
        log=(directory/(api+'_resize-'+mode+'.log')).read_text(encoding='utf-8')
        assert 'Resized: 641 x 479' in log and 'Resized: 1920 x 1080' in log and 'Minimize/restore test' in log
        assert np.array_equal(actual,reference) and np.max(np.abs(depth-z))<=1e-6
        if mode=='hiz':validate_pyramid(gpu)
        results.append(dict(api=api,mode=mode,oddResize=[641,479],restored=[1920,1080],idMismatchPixels=0))
        print(api,mode,'odd resize / minimize / restore PASS',flush=True)
    return results

def main():
    p=argparse.ArgumentParser();p.add_argument('--configuration',default='Debug');p.add_argument('--quick',action='store_true');p.add_argument('--ui-only',action='store_true');p.add_argument('--motion-only',action='store_true');p.add_argument('--resize-only',action='store_true');p.add_argument('--cases',nargs='+');p.add_argument('--modes',nargs='+',choices=MODES);p.add_argument('--api',choices=['Direct3D12','Vulkan']);args=p.parse_args()
    directory=ROOT/'.cache/culling-tests'/args.configuration;directory.mkdir(parents=True,exist_ok=True)
    if args.resize_only:
        results=[]
        for api in ([args.api] if args.api else ['Direct3D12','Vulkan']):results+=verify_resize(api,args.configuration,directory)
        (directory/('resize-results'+('-'+args.api if args.api else '')+'.json')).write_text(json.dumps(results,indent=2)+'\n')
        return
    if args.ui_only:
        results=[]
        for api in ([args.api] if args.api else ['Direct3D12','Vulkan']):results+=verify_ui(api,args.configuration,directory)
        (directory/('ui-results'+('-'+args.api if args.api else '')+'.json')).write_text(json.dumps(results,indent=2)+'\n')
        return
    if args.motion_only:
        results=[]
        for api in ([args.api] if args.api else ['Direct3D12','Vulkan']):results+=verify_motion(api,args.configuration,directory)
        (directory/('motion-results'+('-'+args.api if args.api else '')+'.json')).write_text(json.dumps(results,indent=2)+'\n')
        return
    cases=[('hero',[]),('opening',['--camera','Opening','--culling-door','2.5']),('near',['--camera','Near']),
        ('occlusion',['--camera','Occlusion']),('plaza',['--camera','Plaza']),
        ('odd',['--width','1279','--height','719']),('mini',['--culling-scene','Mini']),
        ('stress',['--culling-scene','Stress']),
        ('turn',['--camera-path','--path-frame','600']),('return',['--camera-path','--path-frame','1200'])]
    cases.append(('first-frame',['--frames','1']))
    cases.append(('moved-object',['--culling-object-x','2']))
    if not args.quick:cases+=make_fixtures(directory)
    if args.quick:cases=cases[:1]
    if args.cases:
        cases=[(name,options) for name,options in cases if name in args.cases]
        assert len(cases)==len(args.cases),'Unknown case name'
    results=[]
    for api in ([args.api] if args.api else ['Direct3D12','Vulkan']):
        for case,options in cases:
            baseline,baseline_depth,reference,_=run(api,TOPICS[0],args.configuration,directory,case+'-off',options+['--culling','off'])
            ids=np.rint(baseline[:,0]*255).astype(np.uint32)+(np.rint(baseline[:,1]*255).astype(np.uint32)<<8)
            visible_ids=np.unique(ids[ids>0])-1
            for topic,mode in zip(TOPICS,MODES):
                if args.modes and mode not in args.modes:continue
                settings=[2,4,8] if mode=='software' and not args.quick and case in ['hero','odd','fixture-gap'] else [4]
                for divisor in settings:
                    hdr,depth,stats,gpu=run(api,topic,args.configuration,directory,case+'-'+mode+str(divisor),options+['--culling',mode,'--culling-divisor',str(divisor)])
                    assert stats['instanceIds']==reference['instanceIds'],'Stable object identity'
                    missing=int(np.count_nonzero(np.asarray(stats['visible'])[visible_ids]==0))
                    assert missing==0,f'{api}/{case}/{mode}: {missing} visible objects culled'
                    if case=='fixture-hidden' and mode!='frustum':assert stats['occluded']>0,'Known hidden object was not rejected'
                    if case=='fixture-all-visible':assert stats['occluded']==0 and stats['frustumRejected']==0
                    if case=='fixture-empty':assert stats['frustumRejected']==stats['total']
                    rendered_ids=np.rint(hdr[:,0]*255).astype(np.uint32)+(np.rint(hdr[:,1]*255).astype(np.uint32)<<8)
                    mismatch=int(np.count_nonzero(rendered_ids!=ids))
                    assert mismatch==0,f'{api}/{case}/{mode}: {mismatch} ID pixels changed'
                    error=float(np.max(np.abs(depth-baseline_depth)))
                    assert error<=1e-6,f'{api}/{case}/{mode}: depth error {error}'
                    if mode=='hiz':validate_pyramid(gpu)
                    results.append(dict(api=api,case=case,mode=mode,divisor=divisor,missingObjects=missing,idMismatchPixels=mismatch,maxDepthError=error,
                        total=stats['total'],frustumRejected=stats['frustumRejected'],occluded=stats['occluded'],recordedDraws=stats['recordedDraws']))
                    print(api,case,mode,divisor,'PASS',flush=True)
            if case=='hero':
                base,_,_,_=run(api,TOPICS[0],args.configuration,directory,'color-off',['--culling','off'],view='final')
                for topic,mode in zip(TOPICS,MODES):
                    if args.modes and mode not in args.modes:continue
                    color,_,_,_=run(api,topic,args.configuration,directory,'color-'+mode,['--culling',mode],view='final')
                    assert np.array_equal(color,base),f'{api}/{mode}: HDR color differs'
        # Invalid flags must fail explicitly, including modes belonging to a different tutorial.
        exe=ROOT/'bin/x64'/args.configuration/(api+'_'+TOPICS[0])/(api+'_'+TOPICS[0]+'.exe')
        bad=directory/(api+'-invalid.log')
        r=subprocess.run([str(exe),'--frames','1','--culling','hiz','--log',str(bad)],timeout=30,capture_output=True)
        assert r.returncode!=0 and 'unsupported' in bad.read_text().lower()
    output=directory/('results'+('-'+args.api if args.api else '')+('.quick' if args.quick else '')+('.'+'-'.join(args.cases) if args.cases else '')+('.'+'-'.join(args.modes) if args.modes else '')+'.json')
    output.write_text(json.dumps(results,indent=2)+'\n')
    print('Verified',len(results),'GPU comparisons:',output,flush=True)
if __name__=='__main__':main()
