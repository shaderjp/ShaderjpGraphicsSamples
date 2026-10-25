"""Independent float64 optics equations, numerical BSDF integration and actual GPU/UI readbacks."""
from pathlib import Path
import json,sys,struct,zlib
import numpy as np
work,api=Path(sys.argv[1]),sys.argv[2]
optics_only='--optics-only' in sys.argv
def raw(label,name='hdr'):
    folder=work/f'{api}-{label}-gpu';m=json.loads((folder/'metadata.json').read_text())
    a=np.fromfile(folder/(name+'.bin'),'<f4').reshape(m['height'],m['width'],4)
    assert np.isfinite(a).all(),(label,name,'NaN/Inf')
    return a,m

def read_png(path):
    data=path.read_bytes();offset=8;compressed=bytearray()
    assert data[:8]==b'\x89PNG\r\n\x1a\n'
    while offset<len(data):
        count=struct.unpack_from('>I',data,offset)[0];kind=data[offset+4:offset+8];body=data[offset+8:offset+8+count]
        if kind==b'IHDR':width,height,bits,color,_,_,interlace=struct.unpack('>IIBBBBB',body);assert bits==8 and color in (2,6) and interlace==0
        if kind==b'IDAT':compressed.extend(body)
        offset+=count+12
    channels=4 if color==6 else 3;stride=width*channels;filtered=zlib.decompress(compressed);output=bytearray();previous=bytearray(stride)
    for y in range(height):
        start=y*(stride+1);mode=filtered[start];row=bytearray(filtered[start+1:start+1+stride])
        for x in range(stride):
            left=row[x-channels] if x>=channels else 0;up=previous[x];diagonal=previous[x-channels] if x>=channels else 0
            if mode==0:predictor=0
            elif mode==1:predictor=left
            elif mode==2:predictor=up
            elif mode==3:predictor=(left+up)//2
            else:
                p=left+up-diagonal;dl,du,dd=abs(p-left),abs(p-up),abs(p-diagonal);predictor=left if dl<=du and dl<=dd else up if du<=dd else diagonal
            row[x]=(row[x]+predictor)&255
        output.extend(row);previous=row
    return np.frombuffer(output,np.uint8).reshape(height,width,channels)[:,:,:3]

def ui_camera_checks():
    paused,pm=raw('ui-flow-paused');later,lm=raw('ui-flow-paused-later');resumed,rm=raw('ui-flow-resumed')
    assert np.array_equal(paused,later) and pm['spp']==lm['spp'],'uncapped pause preserves accumulation'
    assert rm['spp']>lm['spp'],'resume adds samples'
    assert not np.array_equal(resumed,later),'resume changes the estimator'
    baseline,bm=raw('camera-camera-baseline');moved,mm=raw('camera-camera-move')
    turned,tm=raw('camera-camera-look');reset,rr=raw('camera-camera-reset')
    assert bm['spp']==rr['spp'],'camera Reset returns to the same sample count'
    assert np.array_equal(baseline,reset),'camera Reset at equal spp is reproducible'
    assert np.mean(np.abs(baseline[...,:3]-moved[...,:3]))>.001,'W key changes rendering'
    assert np.mean(np.abs(moved[...,:3]-turned[...,:3]))>.001,'right drag changes rendering'
    print(api,'uncapped UI pause/resume and native camera input PASS')

if '--extras-only' in sys.argv:
    ui_camera_checks()
    sys.exit(0)

def widget_checks():
    for label in ['baseline','edited','reset','exposure','paused','paused-later','resumed','reflection','legacy','full-again']:
        raw('ui-'+label)
    baseline,_=raw('ui-baseline');edited,_=raw('ui-edited');reset,_=raw('ui-reset');exposure,_=raw('ui-exposure')
    assert np.array_equal(baseline,reset),'UI Reset restored original estimator'
    assert np.mean(np.abs(baseline[...,:3]-edited[...,:3]))>.00001,'IOR widget affected GPU pixels'
    assert np.array_equal(baseline,exposure),'exposure preserved linear HDR accumulation'
    display_reset=read_png(work/f'{api}-ui-reset.png').astype(np.int16)
    display_exposure=read_png(work/f'{api}-ui-exposure.png').astype(np.int16)
    assert np.max(np.abs(display_reset[:,400:]-display_exposure[:,400:]))>10,'exposure affects display outside the UI'
    assert np.array_equal(raw('ui-paused')[0],raw('ui-paused-later')[0]),'pause preserved samples'
    assert raw('ui-reflection')[1]['mode']=='reflection' and raw('ui-legacy')[1]['mode']=='legacy-pbr' and raw('ui-full-again')[1]['mode']=='full'
    assert np.array_equal(raw('ui-full-again')[0],baseline),'mode round trip is reproducible'
    if (work/f'{api}-ui-custom-reset-gpu/metadata.json').is_file():
        before,bm=raw('ui-custom-baseline');after,am=raw('ui-custom-reset')
        assert np.array_equal(before,after),'Reset all preserves custom CLI defaults'
        assert am['mode']=='reflection' and am['bounces']==6 and am['sampling']=='bsdf'
        assert am['cameraName']=='ThicknessOblique' and am['scene']=='Thickness'
        assert am['materials']==bm['materials'],'CLI material values restored'
        assert np.mean(np.abs(before[...,:3]-raw('ui-custom-edited')[0][...,:3]))>.00001
        print(api,'custom material/mode/preset/camera/bounces/sampling Reset PASS')

if '--ui-only' in sys.argv:
    widget_checks();ui_camera_checks();sys.exit(0)
def fresnel(c,e):
    c=np.clip(np.abs(c),0,1);st=np.maximum(0,1-c*c)/(e*e);ct=np.sqrt(np.maximum(0,1-st))
    return np.where(st>=1,1,.5*((e*c-ct)/np.maximum(e*c+ct,1e-30))**2+.5*((c-e*ct)/np.maximum(c+e*ct,1e-30))**2)
def evaluate(d,V,eta,rough,mode,base=None):
    # This reference evaluates world directions directly; it does not use the GPU sampler.
    d=np.asarray(d,dtype=np.float64);V=np.array(V,dtype=np.float64);V/=np.linalg.norm(V)
    nv=V[2];nl=d[...,2];reflection=nl>0
    legacy=mode=='legacy-pbr' or base is not None
    if legacy:eta=1
    a=max(rough*rough,1e-4);b=np.asarray([1,1,1] if base is None else base,dtype=np.float64)
    h=d*np.where(reflection,1,eta)[...,None]+V
    h/=np.maximum(np.linalg.norm(h,axis=-1,keepdims=True),1e-30);h*=np.where(h[...,2]<0,-1,1)[...,None]
    vh=np.sum(h*V,axis=-1);lh=np.sum(h*d,axis=-1)
    D=a*a/(np.pi*(h[...,2]**2*(a*a-1)+1)**2)
    g1=2*nv/(nv+np.sqrt(a*a+(1-a*a)*nv*nv))
    absnl=np.maximum(np.abs(nl),1e-30)
    G=2/(np.sqrt(1+a*a*(1-nv*nv)/(nv*nv))+np.sqrt(1+a*a*np.maximum(0,1-nl*nl)/(absnl*absnl)))
    valid=(vh>0)&(lh*nl>0)&(nl!=0)
    pdfh=D*g1*np.abs(vh)/nv
    if legacy:
        F=.04+.96*np.maximum(0,1-vh)**5
        p=np.clip(.04/(.04+float(np.dot(b,[.2126,.7152,.0722]))),.1,.9)
        pdf=(1-p)*np.maximum(0,nl)/np.pi+p*D*g1/(4*nv)
        value=(1-F)[...,None]*b/np.pi+(D*G*F/(4*nv*absnl))[...,None]
        valid &= reflection
    else:
        F=fresnel(vh,eta)
        probability=np.where(F>0,1,0) if mode=='reflection' else F
        denom=lh+vh/eta
        jac=np.abs(lh)/np.maximum(denom*denom,1e-30)
        pdf=np.where(reflection,probability*pdfh/np.maximum(4*np.abs(vh),1e-30),(1-F)*pdfh*jac)
        value=np.where(reflection,F*D*G/(4*nv*absnl),(1-F)*D*G*np.abs(lh*vh)/(nv*absnl*np.maximum(denom*denom,1e-30)*eta*eta))[...,None]*np.ones(3)
        if mode=='reflection':valid &= reflection
    return np.where(valid[...,None],value,0),np.where(valid,pdf,0)

summaries=[]
for mode in ['full','reflection','legacy-pbr']:
    if not (work/f'{api}-optics-{mode}-gpu/metadata.json').is_file():
        assert optics_only
        continue
    hdr,m=raw('optics-'+mode);sample,_=raw('optics-'+mode,'sample');surface,_=raw('optics-'+mode,'surface')
    assert (m['width'],m['height'])==(1920,1080)
    assert np.allclose(hdr[:,0::16,:3],.04,atol=1e-6)
    assert np.allclose(sample[:,1::16,:3],[-1/3,0,-np.sqrt(8/9)],atol=1e-6)
    assert np.allclose(hdr[:,2::16,:3],[1,0,0],atol=1e-6)
    for test,eta in [(10,1.5),(11,1/1.5)]:
        expected=fresnel((np.arange(m['height'])+.5)/m['height'],eta)
        assert np.allclose(hdr[:,test::16,0],expected[:,None],atol=2e-5)
    for test,length in [(12,.5),(13,2)]:assert np.allclose(hdr[:,test::16,:3],np.array([.18,.65,.85])**(length/.5),rtol=2e-5,atol=2e-6)
    assert np.allclose(hdr[:,14::16,:3],[.2,.8,1],atol=1e-6)
    if mode=='full':
        for test,eta in [(3,1.5),(4,1/1.5),(5,1)]:
            rows=surface[:,test::16,:];flags=rows[...,3].astype(int);trans=(flags&2)!=0
            expected=np.where(trans,1/(eta*eta),1)
            assert np.allclose(rows[...,:3],expected[...,None],atol=2e-6)
            if test!=5:assert abs(trans.mean()-.96)<.004,(test,trans.mean())
            else:assert trans.all()
    if mode=='reflection':
        assert np.allclose(surface[:,3::16,:3],.04,atol=1e-6),'removed transmission must not be redirected into reflection'
        assert np.all(surface[:,5::16,:3]==0),'index-matched interface has no reflection'
    for test,eta,V,base in [(6,1.5,[0,0,1],None),(7,1.5,[.95,0,.312],None),(8,1/1.5,[0,0,1],None),(9,1/1.5,[.8,0,.6],None),(15,1,[0,0,1],[.8,.4,.2])]:
        d=sample[:,test::16,:3].astype(np.float64);pdf=sample[:,test::16,3].astype(np.float64);weights=surface[:,test::16,:3].astype(np.float64)
        good=pdf>0
        assert good.any()
        expected,expected_pdf=evaluate(d,V,eta,.3,mode,base)
        assert np.allclose(pdf[good],expected_pdf[good],rtol=.004,atol=3e-5),(mode,test,'PDF',np.max(np.abs(pdf[good]-expected_pdf[good])))
        weight=expected*np.abs(d[...,2,None])/np.maximum(expected_pdf[...,None],1e-30)
        assert np.allclose(weights[good],weight[good],rtol=.004,atol=3e-5),(mode,test,'throughput')
        if mode=='reflection':assert (d[...,2][good]>0).all()
        # A second, independent estimator samples the entire sphere uniformly.
        # It checks branch frequencies and integrals, not just f/pdf at sampled directions.
        generator=np.random.default_rng(19019+test)
        z=generator.uniform(-1,1,262144);phi=generator.uniform(0,2*np.pi,len(z));radius=np.sqrt(1-z*z)
        directions=np.stack([radius*np.cos(phi),radius*np.sin(phi),z],axis=-1)
        qvalue,qpdf=evaluate(directions,V,eta,.3,mode,base)
        acceptance=qpdf*(4*np.pi)
        error=5*np.sqrt(acceptance.var()/len(z)+good.mean()*(1-good.mean())/good.size)+.003
        assert abs(good.mean()-acceptance.mean())<error,(mode,test,'PDF integral vs accepted samples',good.mean(),acceptance.mean(),error)
        rObserved=((d[...,2]>0)&good).mean();rExpected=acceptance*(z>0)
        branchError=5*np.sqrt(rExpected.var()/len(z)+rObserved*(1-rObserved)/good.size)+.003
        assert abs(rObserved-rExpected.mean())<branchError,(mode,test,'reflection frequency')
        energy=qvalue*np.abs(z[:,None])*(4*np.pi)
        energyError=5*np.sqrt(energy.var(0)/len(z)+weights.reshape(-1,3).var(0)/good.size)+.003
        assert np.all(np.abs(weights.mean((0,1))-energy.mean(0))<energyError),(mode,test,'BSDF integral')
        summaries.append({'mode':mode,'case':test,'accepted':float(good.mean()),'meanWeight':weights.mean((0,1)).tolist(),
            'independentPDFIntegral':float(acceptance.mean()),'acceptanceTolerance':float(error),'independentEnergyIntegral':energy.mean(0).tolist(),'energyTolerance':energyError.tolist()})
print(api,'GPU Fresnel/Snell/TIR, eta radiance scaling, absorption, rough BSDF/PDF/throughput PASS')

if optics_only:
    (work/f'{api}-optics-results.json').write_text(json.dumps(summaries,indent=2)+'\n')
    sys.exit(0)

for folder in work.glob(f'{api}-*-gpu'):
    for name in ('hdr','sample','surface'):
        values=np.memmap(folder/(name+'.bin'),mode='r',dtype='<f4')
        assert np.isfinite(values).all(),(folder.name,name,'NaN/Inf')
        del values

for label in ['full','reflection','legacy','thickness','inside','dark','slab','scaled','repeat','odd','lifecycle']:
    a,m=raw(label);assert a[...,:3].min()>=-1e-6
    if label!='odd':assert a.shape[:2]==(1080,1920),(label,a.shape)
assert np.max(raw('dark')[0][...,:3])==0
assert raw('inside')[1]['initialMedium']>0
assert raw('odd')[0].shape[:2]==(479,641)
assert np.array_equal(raw('repeat')[0],raw('full-16')[0]),'same seed/spp Reset reproducibility'
for label in ['slab','scaled']:
    a,m=raw(label);diag,_=raw(label,'surface');valid=diag[...,1]==2
    last_sample,_=raw(label,'sample')
    assert valid.sum()>10000
    expected=np.array([.18,.65,.85])**(diag[...,0,None]/.5)
    # Surface diagnostics describe the last jittered path, not the temporal mean.
    assert np.allclose(last_sample[...,:3][valid],expected[valid],atol=2e-4,rtol=.001),label
    assert np.all(diag[...,3][valid]==0),'exits restore air'
furnace=[]
for seed in [1,7,19]:
    a,_=raw(f'furnace-{seed}');s,_=raw(f'furnace-{seed}','surface');mask=s[...,2]>0
    assert mask.sum()>10000
    furnace.append(a[...,:3][mask].mean(0))
furnace=np.array(furnace)
assert np.all(np.abs(furnace.mean(0)-1)<.003+5*furnace.std(0)/np.sqrt(3)),furnace
# Compare the rough glass ROI statistically; exact transparent surfaces are not caustic benchmarks.
mis=np.array([raw(f'rough-mis-{s}')[0][360:650,1120:1450,:3].mean((0,1)) for s in [1,7,19]])
bs=np.array([raw(f'rough-bsdf-{s}')[0][360:650,1120:1450,:3].mean((0,1)) for s in [1,7,19]])
tolerance=.002+5*np.sqrt(mis.var(0)/3+bs.var(0)/3)
assert np.all(np.abs(mis.mean(0)-bs.mean(0))<tolerance),(mis,bs,tolerance)
reference,reference_meta=raw('full')
reference_roi=reference[360:650,1120:1450,:3].mean((0,1))
assert np.all(np.abs(mis.mean(0)-reference_roi)<2*tolerance),'MIS versus high-spp reference'
assert np.all(np.abs(bs.mean(0)-reference_roi)<2*tolerance),'BSDF versus high-spp reference'
# The diffuse floor stays fixed across these nested sample sequences.
floor_reference=reference[960:1020,900:1200,:3]
convergence={str(s):float(np.mean((raw(f'full-{s}')[0][960:1020,900:1200,:3]-floor_reference)**2)) for s in (1,16,256)}
assert convergence['256']<convergence['16']<convergence['1'],convergence
widget_checks()
if api=='Vulkan':assert raw('capture-minimized')[0].shape[:2]==(1080,1920)
ui_camera=(work/f'{api}-ui-flow-gpu/metadata.json').is_file()
if ui_camera:ui_camera_checks()
report={'result':'PASS','optics':summaries,'furnace':furnace.tolist(),'roughMIS':mis.tolist(),'roughBSDF':bs.tolist(),'roiTolerance':tolerance.tolist(),
    'referenceSpp':reference_meta['spp'],'roughReference':reference_roi.tolist(),'diffuseFloorMSE':convergence,'nativeCameraAndPauseResume':ui_camera,
    'customDefaultsReset':(work/f'{api}-ui-custom-reset-gpu/metadata.json').is_file()}
(work/f'{api}-results.json').write_text(json.dumps(report,indent=2)+'\n')
print(api,'GPU scene integration, absorption path length, camera medium, statistical MIS, UI and lifecycle PASS')
