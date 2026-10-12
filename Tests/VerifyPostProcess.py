"""Validate GPU readbacks against independent image and motion invariants. MIT License."""
from pathlib import Path
import json,struct,math,sys
root=Path(sys.argv[1])
def data(name):
    p=root/(name+'.gpu');m=json.loads((p/'metadata.json').read_text());return p,m
def pixels(p,m,name):
    image=next(x for x in m['images'] if x['name']==name)
    raw=(p/(name+'.rgba16f')).read_bytes()
    assert len(raw)==image['width']*image['height']*8
    return image,raw
def at(raw,w,x,y):return struct.unpack_from('<4e',raw,(y*w+x)*8)[:3]
def particle(raw,i):return struct.unpack_from('<12f',raw,i*48)
def near(a,b,tolerance=.003):assert all(abs(x-y)<=tolerance for x,y in zip(a,b)),(a,b)
def half(v):return struct.unpack('<e',struct.pack('<e',v))[0]
for api in ('Direct3D12','Vulkan'):
    p,m=data(api+'-14-final');assert m['images'][0]['max']>1 and m['images'][1]['width']==512
    p1,m1=data(api+'-14-monitor-moved');q,n=data(api+'-14-monitor')
    assert (p1/'monitor.rgba16f').read_bytes()!=(q/'monitor.rgba16f').read_bytes(),'Secondary camera did not update'
    for mode in ('black','constant','impulse','constant-odd'):
        p,m=data(api+'-15-'+mode)
        w,h=m['images'][0]['width'],m['images'][0]['height']
        if mode.endswith('-odd'):assert (w,h)==(641,479)
        for name in ('extract','horizontal','bloom'):
            image,raw=pixels(p,m,name);hw,hh=image['width'],image['height'];assert (hw,hh)==((w+1)//2,(h+1)//2)
            if mode=='black':assert image['min']==image['max']==0
            elif mode.startswith('constant'):
                for pix in struct.iter_unpack('<4e',raw):near(pix[:3],[3,1.5,.375])
            else:
                weights=[.227027027,.194594595,.121621622,.054054054,.016216216]
                def blur(index,value):return half(sum(value*weights[abs(k)] for k in range(-4,5) if index+k<=0))
                for y in range(min(hh,7)):
                    for x in range(min(hw,7)):
                        expected=3.0 if name=='extract' and x==y==0 else 0.0
                        if name=='horizontal' and y==0:expected=blur(x,3)
                        if name=='bloom':expected=blur(y,blur(x,3))
                        near(at(raw,hw,x,y),[expected]*3)
                assert at(raw,hw,hw-1,hh-1)==(0,0,0),'Opposite edge corrupted'
    p,m=data(api+'-16-paused');q,n=data(api+'-16-moving')
    a=(p/'particles.bin').read_bytes();b=(q/'particles.bin').read_bytes()
    assert m['particles']['active']==n['particles']['active']==257
    assert m['particles']['steps']==0 and n['particles']['steps']==7
    assert a[257*48:]==b[257*48:],'Out-of-range threads changed inactive particles'
    old,new=particle(a,0),particle(b,0);dt=1/60;steps=7;gravity=3
    near(new[:3],[old[0]+old[4]*dt*steps,old[1]+old[5]*dt*steps-gravity*dt*dt*steps*(steps-1)/2,old[2]+old[6]*dt*steps],2e-5)
    near(new[4:8],[old[4],old[5]-gravity*dt*steps,old[6],old[7]+dt*steps],2e-5)
    assert n['particles']['sameBuffer'] and len(b)==65536*48
    for count in (1,65536):
        p,m=data(api+'-16-count-'+str(count));raw=(p/'particles.bin').read_bytes()
        assert m['particles']['active']==count and m['particles']['steps']==7 and m['particles']['sameBuffer']
        assert len(raw)==65536*48 and all(math.isfinite(v) for row in struct.iter_unpack('<12f',raw) for v in row)
        near(particle(raw,0)[:3],new[:3],2e-5)
        if count==1:assert raw[48:]==a[48:],'Minimum count changed inactive particle data'
    # GPU state must remain at the same step during pause and return exactly on reset.
    p,m=data(api+'-16-ui-baseline');q,n=data(api+'-16-ui-edited');z,k=data(api+'-16-ui-reset')
    assert m['particles']['steps']==0 and n['particles']['steps']==1 and k['particles']['steps']==0
    assert (p/'particles.bin').read_bytes()==(z/'particles.bin').read_bytes()
    assert (p/'particles.bin').read_bytes()!=(q/'particles.bin').read_bytes()
    print(api+': HDR, live monitor, black/constant/edge bloom, odd extent, particle Euler step, bounds and UI Reset PASS')
