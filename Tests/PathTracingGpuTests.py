"""Independent area-light quadrature, statistical estimates, and actual UI/GPU readbacks."""
from pathlib import Path
import json, sys, struct, zlib
import numpy as np
work,api=Path(sys.argv[1]),sys.argv[2]
def raw(label,name='hdr'):
    folder=work/f'{api}-{label}-gpu';meta=json.loads((folder/'metadata.json').read_text())
    image=np.fromfile(folder/(name+'.bin'),'<f4').reshape(meta['height'],meta['width'],4)
    assert np.isfinite(image).all(),(label,'NaN/Inf')
    return image,meta
def center(label):
    a,_=raw(label);h,w=a.shape[:2];return a[h//2-32:h//2+32,w//2-32:w//2+32,:3].mean((0,1))
for label in ['room-1','room-16','room-256','room-bsdf','room-direct','workshop','stress','fixture-emission','fixture-dark','fixture-occluded','fixture-mask','fixture-transformed','odd']:
    a,m=raw(label);assert a[:,:,:3].min()>=-1e-6
    if label.startswith('room-') and label.split('-')[-1].isdigit():assert m['spp']==int(label.split('-')[-1])
assert np.allclose(center('fixture-emission'),[.25,.5,1],atol=2e-5)
assert np.max(center('fixture-dark'))==0
assert np.max(center('fixture-occluded'))<1e-5
assert np.max(center('fixture-mask'))>.005
assert np.min(center('fixture-transformed'))>.01
assert raw('stress')[1]['instances']>1000
assert raw('odd')[0].shape[:2]==(479,641)
if api=='Vulkan':
    restored,metadata=raw('capture-minimized')
    assert restored.shape[:2]==(720,1280),'Capture must wait for a restored surface'
    assert metadata['spp']==1 and np.array_equal(restored,raw('room-1')[0]),'Resize must restart from sample 0'
assert np.array_equal(raw('room-16')[0],raw('repeat')[0]),'same seed and spp must reproduce'
assert np.array_equal(raw('no-nee-mis')[0],raw('no-nee-bsdf')[0]),'Disabled NEE must not retain an MIS emission discount'
# Integrate the fixture's 1.5 x 1.5 rectangle independently in area measure.
# The fixture's ORM stores roughness=128/255 (sampled as linear UNORM).
def reference(transformed=False):
    q=(np.arange(256)+.5)/256*1.5-.75
    x,y=np.meshgrid(q,q);area=2.25
    if transformed:
        x,y=1.4*(x*np.cos(.4)-y*np.sin(.4))+2.2,1.4*(x*np.sin(.4)+y*np.cos(.4))+.4
        depth=-2.5;area*=1.4**2
    else:x=x+2;depth=-2
    light=np.stack([x,y,np.full_like(x,depth)],-1)
    result=[]
    for py in [-24,0,24]:
        for px in [-24,0,24]:
            position=np.array([px/1280*2*(1280/720)*np.tan(np.pi/6)*4,-py/720*2*np.tan(np.pi/6)*4,0])
            v=np.array([0,0,-4])-position;v/=np.linalg.norm(v)
            delta=light-position;d2=(delta*delta).sum(-1);l=delta/np.sqrt(d2)[...,None]
            nl=-l[...,2];nv=-v[2];h=l+v;h/=np.linalg.norm(h,axis=-1)[...,None]
            vh=(h*v).sum(-1);f=.04+.96*(1-vh)**5
            alpha=(128/255)**2;a2=alpha**2;nh=-h[...,2]
            distribution=a2/(np.pi*(nh*nh*(a2-1)+1)**2)
            visibility=.5/(nl*np.sqrt(nv*nv*(1-a2)+a2)+nv*np.sqrt(nl*nl*(1-a2)+a2))
            brdf=(1-f)[...,None]*np.array([.8,.4,.2])/np.pi+(f*distribution*visibility)[...,None]
            result.append((brdf*(3*nl*nl/d2)[...,None]).mean((0,1))*area)
    return np.mean(result,axis=0)
expected=reference();mis=np.mean([center(f'area-mis-{s}') for s in [1,7,19]],axis=0);bsdf=np.mean([center(f'area-bsdf-{s}') for s in [1,7,19]],axis=0)
assert np.allclose(center('area-reference'),expected,rtol=.025,atol=.001),(center('area-reference'),expected)
assert np.allclose(mis,expected,rtol=.035,atol=.001),(mis,expected)
assert np.allclose(bsdf,expected,rtol=.05,atol=.002),(bsdf,expected)
transformed=reference(True)
assert np.allclose(center('fixture-transformed'),transformed,rtol=.04,atol=.001),'Transformed emitter quadrature'
def mse(a,b):return float(np.mean((a-b)**2))
ref,_=raw('area-reference');mis_errors=[];bsdf_errors=[]
for seed in [1,7,19]:
    for mode,errors in [('mis',mis_errors),('bsdf',bsdf_errors)]:
        a,_=raw(f'area-{mode}-{seed}');errors.append(mse(a[300:420,540:740,:3],ref[300:420,540:740,:3]))
assert np.mean(mis_errors)<np.mean(bsdf_errors),('MIS variance',mis_errors,bsdf_errors)
for label,samples in [('baseline',7),('edited',7),('reset',7),('exposure',7)]:assert raw('ui-'+label)[1]['spp']==samples
base,_=raw('ui-baseline');edited,_=raw('ui-edited');reset,_=raw('ui-reset');exposure,_=raw('ui-exposure')
assert np.max(np.abs(base-edited))>.01,'widget must change GPU scene'
assert np.array_equal(base,reset),'Reset at equal spp'
assert np.array_equal(reset,exposure),'Exposure must preserve accumulation'
def read_png(path):
    data=path.read_bytes();offset=8;compressed=bytearray()
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

def png(label):return read_png(work/f'{api}-{label}.png').astype(float)[:,350:,:3]
assert np.max(np.abs(png('ui-reset')-png('ui-exposure')))>10,'Exposure must affect display'
paused,pm=raw('ui-flow-paused');later,lm=raw('ui-flow-paused-later');resumed,rm=raw('ui-flow-resumed')
assert pm['spp']==lm['spp'] and np.array_equal(paused,later),'Pause must preserve GPU accumulation'
assert rm['spp']>lm['spp'],'Resume must add samples'
assert raw('paused')[1]['spp']==1,'Pause before first frame initializes exactly one sample'
assert np.array_equal(raw('camera-camera-baseline')[0],raw('camera-camera-reset')[0]),'Camera Reset with equal spp'
assert raw('camera-camera-baseline')[1]['spp']==raw('camera-camera-reset')[1]['spp']==4
# More samples reduce residual variance in the selected room's diffuse back wall.
a,_=raw('room-1');b,_=raw('room-16');c,_=raw('room-256')
roi=np.s_[180:330,540:700,:3]
assert mse(b[roi],c[roi])<mse(a[roi],c[roi])
summary={'api':api,'quadrature':expected.tolist(),'misMean':mis.tolist(),'bsdfMean':bsdf.tolist(),'misMSE':mis_errors,'bsdfMSE':bsdf_errors,'transformedQuadrature':transformed.tolist(),'transformedMean':center('fixture-transformed').tolist()}
(work/f'{api}-results.json').write_text(json.dumps(summary,indent=2)+'\n')
print(api,'analytic emission, area quadrature, MIS statistics, finite HDR, accumulation/UI and lifecycle PASS')
