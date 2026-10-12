"""Independent numeric checks against actual GPU G-buffer, HDR and light-list readbacks."""
from pathlib import Path
import json,sys
import numpy as np
work,api=Path(sys.argv[1]),sys.argv[2]

def directory(label):return work/f'{api}-{label}-gpu'
def metadata(label):return json.loads((directory(label)/'metadata.json').read_text())
def read(label,name,dtype):return np.fromfile(directory(label)/(name+'.bin'),dtype=dtype)
def hdr(label):return read(label,'hdr',np.float16).astype(np.float32)
def compare(a,b):
    for name in ['base-ao','normal','metal-rough','emissive','depth','shadow-depth']:
        assert (directory(a)/(name+'.bin')).read_bytes()==(directory(b)/(name+'.bin')).read_bytes(),(a,b,name)
    x,y=hdr(a),hdr(b);assert np.isfinite(x).all() and np.isfinite(y).all()
    error=np.abs(x-y);assert np.all(error<=1e-3+2e-3*np.maximum(np.abs(x),np.abs(y))),(a,b,error.max())
    print(api,a,b,'G-buffer exact, HDR max difference',float(error.max()))

for suffix in ['base','stress','odd','full','inside','maximum','moving']:
    for mode in ['tile','cluster']:
        if directory(f'{mode}-{suffix}').exists():compare(f'all-{suffix}',f'{mode}-{suffix}')
compare('all-base','overflow-base')
compare('cluster-base','chapter20-base')
for prefix in ['switch-live','switch20-live']:
    if directory(prefix+'-switch-baseline').exists():
        for suffix in ['cluster','all','reset']:
            compare(prefix+'-switch-baseline',prefix+'-switch-'+suffix)

reports=[]
for label in ['tile-base','cluster-base','tile-stress','cluster-stress','tile-odd','cluster-odd','cluster-full','tile-inside','cluster-inside','cluster-maximum','overflow-base','cluster-moving']:
    path=directory(label)
    if not path.exists():continue
    meta=metadata(label);w,h=meta['width'],meta['height'];capacity=meta['listCapacity']
    raw=(path/'grid-constants.bin').read_bytes();assert len(raw)==128
    floats=np.frombuffer(raw,np.float32);ints=np.frombuffer(raw,np.uint32)
    view=floats[:16].reshape(4,4).astype(np.float64);projection=floats[16:20].astype(np.float64)
    near,far=floats[20:22].astype(np.float64);tx,ty,slices,stride=map(int,ints[24:28]);mode=int(ints[28])
    assert tx==(w+15)//16 and ty==(h+15)//16 and slices==24 and stride==256
    cells=tx*ty*(slices if mode==2 else 1);assert cells==meta['cells']
    headers=read(label,'light-headers',np.uint32).reshape(cells,2)
    indices=read(label,'light-indices',np.uint32).reshape(cells,stride)
    bounds=read(label,'tile-depth-bounds',np.float32).reshape(ty,tx,2)
    masks=read(label,'tile-slice-masks',np.uint32).reshape(ty,tx)
    lights=read(label,'lights',np.float32).reshape(-1,8).astype(np.float64)
    assert len(lights)==meta['pointLights'] and np.isfinite(lights).all()
    centers=np.concatenate([lights[:,:3],np.ones((len(lights),1))],axis=1)@view
    radii=lights[:,3]
    assert np.isfinite(bounds).all()
    depth=read(label,'depth',np.float32).reshape(h,w)
    for y in range(ty):
        for x in range(tx):
            d=depth[y*16:min((y+1)*16,h),x*16:min((x+1)*16,w)]
            valid=d[d<1].astype(np.float64)
            if not len(valid):
                assert masks[y,x]==0 and (bounds[y,x]==0).all()
            else:
                z=projection[3]/(valid-projection[2])
                assert np.allclose(bounds[y,x],[z.min(),z.max()],rtol=4e-4,atol=1e-3),(label,x,y,'depth bounds')
    active=0;total=0;overflow=0;maximum=0
    for cell,(count,flag) in enumerate(headers):
        count=int(count);flag=int(flag);zslice,rest=divmod(cell,tx*ty);y,x=divmod(rest,tx)
        is_active=bool(masks[y,x]&(1<<zslice)) if mode==2 else bool(masks[y,x])
        assert flag==int(count>capacity) and count<=len(lights)
        if not is_active:
            assert count==0 and flag==0
            continue
        active+=1;total+=count;maximum=max(maximum,count);overflow+=flag
        values=indices[cell,:min(count,capacity)]
        assert (values<len(lights)).all() and len(set(map(int,values)))==len(values)
        if flag:continue
        zl,zh=(near*(far/near)**(zslice/slices),near*(far/near)**((zslice+1)/slices)) if mode==2 else bounds[y,x]
        if mode==2:zl,zh=max(zl,bounds[y,x,0]),min(zh,bounds[y,x,1])
        slopes=np.array([x*16/w*2-1,min((x+1)*16,w)/w*2-1,1-min((y+1)*16,h)/h*2,1-y*16/h*2])*projection[[0,0,1,1]]
        cx,cy,cz=centers[:,:3].T
        distances=np.array([cx-slopes[0]*cz,slopes[1]*cz-cx,cy-slopes[2]*cz,slopes[3]*cz-cy]).T
        # Strict interior spheres must be present; the shader deliberately pads its planes.
        required=(radii<=0)|((cz+radii>zl+.002)&(cz-radii<zh-.002)&np.all(distances>-(radii[:,None]-.002)*np.sqrt(1+slopes**2),axis=1))
        assert set(map(int,np.flatnonzero(required))).issubset(set(map(int,values))),(label,cell,'missing light')
    assert active>0 and total>0
    if label=='overflow-base':assert overflow==active
    for name in ['hdr','extract','horizontal','bloom']:assert np.isfinite(read(label,name,np.float16)).all()
    report={'api':api,'label':label,'lights':len(lights),'cells':cells,'active':active,'averageCandidates':total/active,'maximumCandidates':maximum,'overflowCells':overflow,'listBytes':cells*(stride*4+8)}
    reports.append(report);print(report)
(work/f'{api}-light-list-verification.json').write_text(json.dumps(reports,indent=2)+'\n')
print('Conservative sphere/plane coverage, indices, overflow, HDR and partial tiles passed')
