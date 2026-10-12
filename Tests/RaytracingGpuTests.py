"""Analytic fixtures and actual GPU readbacks, plus interactive/lifecycle regressions."""
from pathlib import Path
import sys, json, struct, zlib
import numpy as np

work,api=Path(sys.argv[1]),sys.argv[2]
def raw(label,name,dtype=np.float32):
    folder=work/f'{api}-{label}-gpu'
    metadata=json.loads((folder/'metadata.json').read_text())
    width,height=metadata['width'],metadata['height']
    result=np.fromfile(folder/(name+'.bin'),dtype=dtype).astype(np.float32)
    if name in ('hdr','surface','reflection'):result=result.reshape(height,width,4)
    assert np.isfinite(result).all(),(label,name,'NaN/Inf')
    return result
def hdr(label):return raw(label,'hdr',np.float16)
def center(image):
    h,w=image.shape[:2];return image[h//2-3:h//2+3,w//2-3:w//2+3,:3].mean((0,1))
def png(path):
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

for label in ('basic','shadows','reflections','no-reflections','no-shadow'):
    hdr(label);raw(label,'surface');raw(label,'reflection')
assert np.allclose(hdr('shadows'),hdr('no-reflections'),atol=.001,rtol=.002),'Reflection OFF must return to sample 02'
assert np.max(np.abs(hdr('shadows')-hdr('no-shadow')))>0.01,'Workshop shadows have no effect'
assert np.max(np.abs(hdr('reflections')-hdr('no-reflections')))>0.01,'Workshop reflections have no effect'
for t in range(1,4):
    meta=json.loads((work/f'{api}-stress-{t}-gpu/metadata.json').read_text());assert meta['instances']>1000 and meta['verticesShared']
    odd=json.loads((work/f'{api}-odd-{t}-gpu/metadata.json').read_text());assert (odd['width'],odd['height'])==(641,479)
    for suffix in ('baseline','edited','reset'):
        assert (work/f'{api}-ui-{t}-{suffix}.png').exists()
    baseline=png(work/f'{api}-ui-{t}-baseline.png')[:,340:]
    edited=png(work/f'{api}-ui-{t}-edited.png')[:,340:]
    reset=png(work/f'{api}-ui-{t}-reset.png')[:,340:]
    assert np.count_nonzero(np.max(np.abs(baseline.astype(int)-edited.astype(int)),axis=2)>2)>20,('UI has no GPU scene effect',t)
    assert np.array_equal(baseline,reset),('Reset did not restore baseline',t)

# A ray through a known plane independently determines UV and distance.
s=raw('fixture-albedo','surface');h,w=s.shape[:2]
y,x=np.mgrid[:h,:w];tan=np.tan(np.pi/6)
rx=((x+.5)/w*2-1)*(w/h)*tan;ry=(1-(y+.5)/h*2)*tan
expected_u=(rx*4+2)/4;expected_v=(2-ry*4)/4
inside=(np.abs(rx*4)<1.8)&(np.abs(ry*4)<1.8)
assert np.max(np.abs(s[:,:,0][inside]-expected_u[inside]))<2e-5
assert np.max(np.abs(s[:,:,1][inside]-expected_v[inside]))<2e-5
assert np.max(np.abs(s[:,:,2][inside]-4*np.sqrt(1+rx[inside]**2+ry[inside]**2)))<2e-5
# Repeat+linear 2x2 sampling. Texels are linear primary colors and white.
u,v=s[:,:,0],s[:,:,1];fx=u*2-.5;fy=v*2-.5;ix=np.floor(fx).astype(int);iy=np.floor(fy).astype(int);wx=fx-ix;wy=fy-iy
tex=np.array([[[1,0,0],[0,1,0]],[[0,0,1],[1,1,1]]],np.float32)
expected=tex[iy%2,ix%2]*(1-wx[:,:,None])*(1-wy[:,:,None])+tex[iy%2,(ix+1)%2]*wx[:,:,None]*(1-wy[:,:,None])+tex[(iy+1)%2,ix%2]*(1-wx[:,:,None])*wy[:,:,None]+tex[(iy+1)%2,(ix+1)%2]*wx[:,:,None]*wy[:,:,None]
# GPU filtering has finite coordinate/weight/color precision; the HDR target is FP16.
# UV and hit distance above are checked separately at much tighter precision.
assert np.max(np.abs(hdr('fixture-albedo')[:,:,:3][inside]-expected[inside]))<.01
assert np.max(np.abs(raw('fixture-transform','surface')[:,:,2]-s[:,:,2]))>1,'Instance transform did not affect hits'
for label in ('fixture-mask','fixture-mask-back'):
    s=raw(label,'surface');assert np.count_nonzero(s[:,:,3]==0)>10000 and np.count_nonzero(s[:,:,3]==1)>10000,('MASK/back-face holes missing',label)
for kind in ('sun-shadow','point-shadow'):
    a=center(hdr('fixture-'+kind));b=center(hdr('fixture-'+kind+'-clear'))
    assert np.max(a)<.001 and np.min(b)>.01,(kind,a,b)
a=center(hdr('fixture-point-beyond'));b=center(hdr('fixture-point-beyond-clear'))
assert np.allclose(a,b,atol=.001,rtol=.002),('Point ray extends beyond light',a,b)
assert np.allclose(center(hdr('fixture-mask-shadow-hole')),center(hdr('fixture-mask-shadow-hole-clear')),atol=.001,rtol=.002),'Shadow Any-hit did not ignore MASK hole'
assert np.max(center(hdr('fixture-mask-shadow-solid')))<.001,'Shadow Any-hit ignored solid MASK'
incoming=center(raw('fixture-reflection','reflection'))
assert np.allclose(incoming,[.25,.5,1],atol=.001),('Offscreen reflected target',incoming)
assert not np.any(raw('fixture-reflection','surface')[:,:,3]==1),'Reflection target must be outside primary camera'
assert np.max(center(raw('fixture-reflection-moved','reflection')))<.001,'Moved target still reflected at center'
assert np.max(center(raw('fixture-reflection-mask-hole','reflection')))<.001,'Reflection Any-hit did not ignore MASK hole'
assert np.allclose(center(raw('fixture-reflection-mask-solid','reflection')),[.25,.5,1],atol=.001),'Reflection Any-hit ignored solid MASK'
a=center(raw('fixture-reflection-shadow','reflection'));b=center(raw('fixture-reflection-shadow-clear','reflection'))
assert np.max(a)<.001 and np.min(b)>.01,('Reflected hit lacks shadow',a,b)
assert np.max(center(raw('fixture-environment','reflection')))>.01,'Reflection miss did not sample environment'
assert np.max(center(raw('fixture-environment-black','reflection')))<.001,'Environment OFF not applied to reflected miss'
print(api+': analytic UV/material/MASK/transform/shadow/reflection, finite HDR, Stress, resize, UI/Reset passed')
