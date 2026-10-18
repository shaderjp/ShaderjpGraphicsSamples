"""Compare independent analytic fixtures and readbacks from the actual GPU."""
from pathlib import Path
import json,sys,struct,zlib
import numpy as np
root,api=Path(sys.argv[1]),sys.argv[2]
comparisons=[]
def folder(label): return root/f'{api}-{label}-gpu'
def image(label,name='hdr',gallery=False):
    meta=json.loads((folder(label)/'metadata.json').read_text())
    a=np.fromfile(folder(label)/(name+'.bin'),dtype='<f4' if gallery else '<f2').astype(np.float32)
    a=a.reshape(meta['height'],meta['width'],4)
    assert np.isfinite(a).all(),(label,'NaN/Inf')
    return a
def close(a,b,label):
    error=np.abs(a-b)
    failed=np.count_nonzero(error>.001+.002*np.abs(b))
    comparisons.append({'label':label,'maximumAbsoluteError':float(np.max(error)),
                        'comparedScalars':int(error.size),'failedScalars':int(failed)})
    assert failed==0,(label,float(np.max(error)))
def center(a):
    h,w=a.shape[:2];return a[h//2-3:h//2+3,w//2-3:w//2+3,:3].mean((0,1))
close(image('map'),image('deferred18'),'Shadow Map vs basic18')
close(image('reflection-off'),image('basic26'),'Reflection OFF')
for name in ['base-ao','normal','metal-rough','emissive','depth']:
    assert (folder('map')/(name+'.bin')).read_bytes()==(folder('deferred18')/(name+'.bin')).read_bytes(),('G-buffer modified',name)
assert np.max(abs(image('basic26')-image('no-shadow')))>.01,'No shadow effect'
assert np.max(abs(image('basic27')-image('reflection-off')))>.01,'No reflection effect'
assert np.max(center(image('fixture-sun-shadow')))<.001
assert np.min(center(image('fixture-sun-shadow-clear')))>.01
close(center(image('fixture-sun-unoccluded')),center(image('fixture-sun-unoccluded-clear')),'Self intersection on unoccluded plane')
assert np.min(center(image('fixture-sun-unoccluded')))>.01
close(center(image('fixture-reflection')),np.array([.25,.5,1]),'Reflected offscreen emissive target')
for case in ['reflection-moved','reflection-mask-hole']:
    assert np.max(center(image('fixture-'+case)))<.001,(case,'Unexpected reflected hit')
close(center(image('fixture-reflection-mask-solid')),np.array([.25,.5,1]),'Solid MASK reflection')
close(center(image('fixture-reflection-mask-back')),np.array([.25,.5,1]),'Double-sided MASK back-face hit')
assert np.max(center(image('fixture-reflection-single-back')))<.001,'Single-sided back-face was committed'
assert np.max(center(image('fixture-environment')))>.01,'Missing reflected environment'
assert np.max(center(image('fixture-environment-black')))<.001,'Environment OFF ignored'
assert np.max(center(image('fixture-reflection-sun-shadow')))<.001,'Reflected hit lacks shadow'
assert np.min(center(image('fixture-reflection-sun-shadow-clear')))>.01,'Reflected direct light missing'
for mode in ['bound','textures','resources']:
    close(image('hybrid-'+mode,gallery=True),image('pipeline-'+mode,gallery=True),'RT Pipeline vs RayQuery '+mode)
close(image('hybrid-driven-single',gallery=True),image('pipeline-driven-single',gallery=True),'GPU Driven single object')
assert np.count_nonzero(np.fromfile(folder('hybrid-driven-single')/'depth.bin','<f4')<1)>50,'Single-object comparison did not render geometry'
# Dense Gallery contains coincident surfaces. Atomic visible-list compaction can
# choose another instance at identical depth, independently of the RT method.
p=image('hybrid-driven','position',True);q=image('pipeline-driven','position',True)
assert (folder('hybrid-driven')/'depth.bin').read_bytes()==(folder('pipeline-driven')/'depth.bin').read_bytes(),'GPU Driven depth mismatch'
same=(p[:,:,3]==q[:,:,3]) & np.all(abs(p[:,:,:3]-q[:,:,:3])<=2e-5,axis=2)
for channel in ['base','normal','emission']:
    same &= np.all(abs(image('hybrid-driven',channel,True)-image('pipeline-driven',channel,True))<=2e-6,axis=2)
assert np.count_nonzero(same)>=same.size*.99,'Too many ambiguous GPU Driven primary surfaces'
close(image('hybrid-driven',gallery=True)[same],image('pipeline-driven',gallery=True)[same],'Dense GPU Driven matching surfaces')
direct_depth=np.fromfile(folder('pipeline-textures')/'depth.bin','<f4')
driven_depth=np.fromfile(folder('hybrid-driven')/'depth.bin','<f4')
assert np.allclose(direct_depth,driven_depth,atol=2e-6,rtol=0),'GPU Driven geometry differs from direct mesh rendering'
print(api+': dense GPU Driven coincident-instance pixels:',np.count_nonzero(~same))
close(image('many-all8'),image('many-tile8'),'All lights vs tiled')
close(image('many-overflow'),image('many-tile8'),'Overflow fallback')
close(image('many-map0'),image('mesh09'),'Point shadows OFF vs advanced09')
for case in ['point-shadow','mask-shadow-solid']:
    assert np.max(center(image('fixture-'+case)))<.001,(case,'Missing shadow')
for case in ['point-beyond','mask-shadow-hole']:
    close(center(image('fixture-'+case)),center(image('fixture-'+case+'-clear')),case)
for label in ['moved','stress26','many-all','many-moving','many-empty','many-stress']:image(label)
assert np.max(image('many-ray-count')[:,:,:2])>0,'Missing shadow-ray diagnostic'
def png(path):
    b=path.read_bytes();o=8;compressed=bytearray()
    while o<len(b):
        n=struct.unpack_from('>I',b,o)[0];kind=b[o+4:o+8];body=b[o+8:o+8+n]
        if kind==b'IHDR':w,h,bits,color,_,_,interlace=struct.unpack('>IIBBBBB',body);assert bits==8 and color in (2,6) and interlace==0
        if kind==b'IDAT':compressed.extend(body)
        o+=n+12
    channels=4 if color==6 else 3;stride=w*channels;data=zlib.decompress(compressed);previous=bytearray(stride);out=bytearray()
    for y in range(h):
        start=y*(stride+1);mode=data[start];row=bytearray(data[start+1:start+1+stride])
        for x in range(stride):
            left=row[x-channels] if x>=channels else 0;up=previous[x];diagonal=previous[x-channels] if x>=channels else 0
            if mode==0:p=0
            elif mode==1:p=left
            elif mode==2:p=up
            elif mode==3:p=(left+up)//2
            else:
                q=left+up-diagonal;dl,du,dd=abs(q-left),abs(q-up),abs(q-diagonal);p=left if dl<=du and dl<=dd else up if du<=dd else diagonal
            row[x]=(row[x]+p)&255
        out.extend(row);previous=row
    return np.frombuffer(out,np.uint8).reshape(h,w,channels)[:,:,:3]
for topic in ['26','27','16','17']:
    assert png(root/f'{api}-odd-{topic}.png').shape[:2]==(479,641)
    label='ui-17-uv-hdr' if topic=='17' else 'ui-'+topic+'-controls'
    baseline=png(root/f'{api}-{label}-baseline.png')[:,430:]
    edited=png(root/f'{api}-{label}-edited.png')[:,430:]
    reset=png(root/f'{api}-{label}-reset.png')[:,430:]
    assert np.count_nonzero(np.max(abs(baseline.astype(int)-edited.astype(int)),axis=2)>2)>20,('UI has no scene effect',topic)
    if topic=='17':
        close(image(label+'-reset'),image(label+'-baseline'),'Tiled UI Reset HDR')
        assert np.max(abs(baseline.astype(int)-reset.astype(int)))<=1,'Tiled UI Reset exceeds PNG quantization'
    else:
        assert np.array_equal(baseline,reset),('Reset mismatch',topic)
print(api+': inline shadows, MASK, reflections, pipeline equivalence, tiled/overflow, UI and odd-size PASS')
(root/f'{api}-verification.json').write_text(json.dumps({
    'api':api,'configuration':root.name,'passed':True,
    'hdrTolerance':{'absolute':.001,'relative':.002},
    'denseGpuDrivenAmbiguousPrimaryPixels':int(np.count_nonzero(~same)),
    'denseGpuDrivenComparedPrimaryPixels':int(np.count_nonzero(same)),
    'comparisons':comparisons,
    'uiReset':'26/27/16 exact PNG; 17 HDR tolerance and at most one PNG code step',
},indent=2)+'\n',encoding='utf-8')
