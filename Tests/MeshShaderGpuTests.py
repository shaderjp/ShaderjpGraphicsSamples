"""Independent topology/visibility and analytic image checks against GPU captures."""
from pathlib import Path
import json,struct,sys,zlib
import numpy as np
work,api=Path(sys.argv[1]),sys.argv[2]
for capture in work.glob(f'{api}-*-gpu/metadata.json'):
    report=json.loads(capture.read_text())
    assert all(item['finite'] for item in report.get('images',[])),(capture,'NaN / Inf in HDR or Bloom')
def png(label):
    data=(work/f'{api}-{label}.png').read_bytes();offset=8;compressed=b''
    while offset<len(data):
        length=int.from_bytes(data[offset:offset+4],'big');kind=data[offset+4:offset+8];body=data[offset+8:offset+8+length];offset+=12+length
        if kind==b'IHDR':w,h,depth,color,*_=struct.unpack('>IIBBBBB',body)
        if kind==b'IDAT':compressed+=body
    assert depth==8 and color in (2,6),(depth,color)
    channels=3 if color==2 else 4;stride=w*channels;raw=zlib.decompress(compressed);result=np.empty((h,stride),np.uint8)
    for row in range(h):
        mode=raw[row*(stride+1)];source=raw[row*(stride+1)+1:(row+1)*(stride+1)];current=bytearray(stride);above=result[row-1] if row else np.zeros(stride,np.uint8)
        for x,b in enumerate(source):
            left=current[x-channels] if x>=channels else 0;up=int(above[x]);corner=int(above[x-channels]) if x>=channels else 0
            if mode==0:predict=0
            elif mode==1:predict=left
            elif mode==2:predict=up
            elif mode==3:predict=(left+up)//2
            else:
                p=left+up-corner;dist=[abs(p-left),abs(p-up),abs(p-corner)];predict=[left,up,corner][dist.index(min(dist))]
            current[x]=(b+predict)&255
        result[row]=np.frombuffer(current,np.uint8)
    return result.reshape(h,w,channels)[...,:3]
def folder(label):return work/f'{api}-{label}-gpu'
def read(label,name,dtype):return np.fromfile(folder(label)/(name+'.bin'),dtype=dtype)
def meta(label):return json.loads((folder(label)/'metadata.json').read_text())
def hdr(label):
    m=meta(label);return read(label,'hdr',np.float16).reshape(m['height'],m['width'],4).astype(np.float32)
def prove_depth_ties(label,coords):
    """Confirm differing pixels have multiple surfaces in one depth quantization interval."""
    data=read(label,'mesh-vertices',np.float32).reshape(-1,12);indices=read(label,'mesh-original-indices',np.uint32);primitives=read(label,'mesh-primitives',np.uint32).reshape(-1,8)
    objects=read(label,'mesh-instances',np.float32).reshape(-1,36);active=np.unique(read(label,'mesh-candidates',np.uint32).reshape(-1,4)[:,0]);triangles=[];uvs=[];materials=[]
    for objectId in active:
        obj=objects[objectId];p=primitives[obj[32:36].view(np.uint32)[0]];ids=indices[p[1]:p[1]+p[3]].reshape(-1,3)+p[0]
        v=data[ids];positions=np.concatenate([v[...,:3],np.ones((*ids.shape,1))],axis=2)@obj[:16].reshape(4,4).astype(np.float64)
        triangles.append(positions[...,:3]);uvs.append(v[...,[3,7]]);materials.append(np.full(len(ids),p[2],np.uint32))
    triangles=np.concatenate(triangles);uvs=np.concatenate(uvs);materials=np.concatenate(materials);v0=triangles[:,0];e1=triangles[:,1]-v0;e2=triangles[:,2]-v0
    globals=read(label,'mesh-global',np.float32);inverse=globals[48:64].reshape(4,4).astype(np.float64);vp=globals[16:32].reshape(4,4).astype(np.float64);origin=globals[92:95].astype(np.float64)
    # camera follows five matrices, Base/Material/Emission: float offset 92.
    m=meta(label);h,w=m['height'],m['width'];depth=read(label,'depth',np.float32).reshape(h,w)
    for y,x in coords:
        far=np.array([(x+.5)/w*2-1,1-(y+.5)/h*2,1,1])@inverse;direction=far[:3]/far[3]-origin;direction/=np.linalg.norm(direction)
        pvec=np.cross(direction,e2);det=np.einsum('ij,ij->i',e1,pvec);valid=np.abs(det)>1e-12;recip=np.divide(1,det,out=np.zeros_like(det),where=valid);tvec=origin-v0;u=np.einsum('ij,ij->i',tvec,pvec)*recip;qvec=np.cross(tvec,e1);v=qvec@direction*recip;t=np.einsum('ij,ij->i',e2,qvec)*recip
        valid&=(u>=-2e-4)&(v>=-2e-4)&(u+v<=1.0002)&(t>0);hit=origin+direction*t[:,None];projected=np.concatenate([hit,np.ones((len(hit),1))],axis=1)@vp;z=np.divide(projected[:,2],projected[:,3],out=np.zeros(len(hit)),where=projected[:,3]!=0);close=np.flatnonzero(valid&(np.abs(z-depth[y,x])<2e-7))
        assert len(close)>=2,(label,'Unexplained shading mismatch',int(y),int(x),len(close))
        normals=np.cross(e1[close],e2[close]);normals/=np.maximum(np.linalg.norm(normals,axis=1)[:,None],1e-12)
        tex=(1-u[close]-v[close])[:,None]*uvs[close,0]+u[close,None]*uvs[close,1]+v[close,None]*uvs[close,2]
        ambiguous=len(np.unique(materials[close]))>1 or np.max(np.linalg.norm(normals-normals[0],axis=1))>1e-3 or np.max(np.linalg.norm(tex-tex[0],axis=1))>1e-3
        assert ambiguous,(label,'Identical attributes at supposed depth tie',int(y),int(x))
    print(api,label,'independent ray/triangle test confirmed depth ties at',len(coords),'pixels')
def compare(a,b,allow_depth_ties=False):
    x,y=hdr(a),hdr(b);dx=read(a,'depth',np.float32);dy=read(b,'depth',np.float32)
    assert x.shape==y.shape and np.isfinite(x).all() and np.isfinite(y).all()
    # Surface discontinuities can select a different primitive at a raster edge.
    h,w=x.shape[:2];z=np.abs(dx-dy).reshape(h,w);edge=z>1e-5
    # Primitive ordering can alter derivative helper lanes at a surface boundary,
    # even when that pixel's depth agrees. Identify boundaries from either image.
    boundary=edge.copy();depths=[dx.reshape(h,w),dy.reshape(h,w)]
    for depth in depths:
        for axis in (0,1):boundary|=np.abs(depth-np.roll(depth,1,axis=axis))>1e-4
    mask=boundary.copy()
    for oy in (-1,0,1):
        for ox in (-1,0,1):mask|=np.roll(np.roll(boundary,oy,axis=0),ox,axis=1)
    assert edge.sum()<=max(16,h*w*.001),(a,b,'depth edges',edge.sum())
    error=np.abs(x-y);limit=.001+.002*np.maximum(np.abs(x),np.abs(y));bad=np.any(error>limit,axis=2)&~mask
    if bad.any() and (a=='5-base' or allow_depth_ties):
        assert bad.sum()<=64,(a,b,'Too many ambiguous pixels',bad.sum())
        prove_depth_ties(a,np.argwhere(bad))
    else:assert not bad.any(),(a,b,'HDR interior mismatch',bad.sum(),float(error.max()))
    print(api,a,b,'matched; HDR max',float(error.max()),'edge pixels',int(edge.sum()))
def oracle(label):
    m=meta(label)['mesh'];n=m['candidateCount'];candidates=read(label,'mesh-candidates',np.uint32).reshape(-1,4)
    instances=read(label,'mesh-instances',np.float32).reshape(-1,36);ids=instances[:,32:36].view(np.uint32)
    meshlets=read(label,'meshlets',np.float32).reshape(-1,16);primitives=read(label,'mesh-primitives',np.uint32).reshape(-1,8);materials=read(label,'mesh-materials',np.float32).reshape(-1,20)
    frames=read(label,'mesh-frames',np.float32).reshape(2,8,4)
    frame=frames[1].astype(np.float64);culling=m['culling'];expected=[[],[]]
    for instance,id,bin,_ in candidates:
        world=instances[instance,:16].reshape(4,4).astype(np.float64);sphere=meshlets[id,4:8].astype(np.float64);cone=meshlets[id,8:12].astype(np.float64);apex=meshlets[id,12:15].astype(np.float64)
        center=np.append(sphere[:3],1)@world;scale=np.linalg.norm(world[0,:3]);visible=True
        if culling:
            visible=np.all(frame[:6]@center>=-sphere[3]*scale-1e-4)
            material=materials[primitives[ids[instance,0],2]]
            if visible and culling==2 and material[18]==0 and cone[3]<1:
                axis=(np.append(cone[:3],0)@world)[:3];axis/=max(np.linalg.norm(axis),1e-6);point=(np.append(apex,1)@world)[:3];direction=point-frame[6,:3];direction/=max(np.linalg.norm(direction),1e-6);visible=np.dot(direction,axis)<cone[3]+1e-4
        if visible:expected[int(bin)].append((int(instance),int(id)))
    args=read(label,'mesh-arguments',np.uint32)
    if m['renderPath']==3:
        actual=read(label,'mesh-visible',np.uint32).reshape(-1,2)
        for bin in (0,1):
            count=int(args[bin*8]);assert count==len(expected[bin]),(label,bin,count,len(expected[bin]));assert sorted(map(tuple,actual[bin*n:bin*n+count]))==sorted(expected[bin]),(label,'GPU visible IDs')
            x,y,z=args[bin*8+4:bin*8+7];assert int(x)*int(y)>=count and int(z)==1
            if not count:assert x==0
            if m.get('dispatchWidth')==256 and count>256:
                assert int(x)==256 and int(y)>1,(label,'2D indirect dispatch missing',x,y,count)
                assert int(y)==(count+255)//256,(label,'Incorrect partial dispatch row',y,count)
    elif m['renderPath']==2:
        assert args[36]==n and args[37]==sum(map(len,expected)),(label,'AS candidate/visible counts',args[36:38],n,sum(map(len,expected)))
        if args[32]:
            light_frame=frames[0].astype(np.float64);shadow_visible=0
            for instance,id,bin,_ in candidates:
                world=instances[instance,:16].reshape(4,4).astype(np.float64);sphere=meshlets[id,4:8].astype(np.float64);cone=meshlets[id,8:12].astype(np.float64)
                center=np.append(sphere[:3],1)@world;scale=np.linalg.norm(world[0,:3]);visible=True
                if culling:
                    visible=np.all(light_frame[:6]@center>=-sphere[3]*scale-1e-4)
                    material=materials[primitives[ids[instance,0],2]]
                    if visible and culling==2 and material[18]==0 and cone[3]<1:
                        axis=(np.append(cone[:3],0)@world)[:3];axis/=max(np.linalg.norm(axis),1e-6)
                        visible=np.dot(-light_frame[6,:3],axis)<cone[3]+1e-4
                shadow_visible+=int(visible)
            assert args[32]==n and args[33]==shadow_visible,(label,'Light frustum/cone visibility mismatch',args[32:34],n,shadow_visible)
    visible_count=sum(map(len,expected))
    if label.endswith('-all-visible'):assert visible_count==n and n>0,(label,'Fixture must retain every candidate')
    if label.endswith(('-empty','-backface')):
        assert visible_count==0,(label,'Fixture must reject every candidate')
        assert np.all(read(label,'depth',np.float32)==1),(label,'Rejected geometry wrote depth')
    print(api,label,'visibility matched CPU double precision oracle',visible_count,'/',n)
for topic in range(5,10):
    for label in [f'{topic}-base',f'{topic}-stress',f'{topic}-odd',f'{topic}-off',f'{topic}-frustum',f'{topic}-empty',f'{topic}-all-visible',f'{topic}-backface',f'{topic}-mask',f'{topic}-mask-back',f'{topic}-grid',f'{topic}-grid-stress']:
        if folder(label).exists():oracle(label)
    for other in ['vertex','mesh','off','frustum']:
        if folder(f'{topic}-{other}').exists():compare(f'{topic}-base',f'{topic}-{other}')
    for case in ['mask','mask-back']:
        label=f'{topic}-{case}'
        if folder(label).exists():
            m=meta(label);h,w=m['height'],m['width'];depth=read(label,'depth',np.float32).reshape(h,w);left,right=w//2-150,w//2+150
            hole,solid=(left,right) if case=='mask' else (right,left)
            assert depth[h//2,hole]==1 and depth[h//2,solid]<1,(label,'MASK hole and solid side')
    if (work/f'{api}-{topic}-ui-baseline.png').exists():
        b,e,r=[png(f'{topic}-ui-{name}') for name in ['baseline','edited','reset']];changed=np.any(b[:,350:]!=e[:,350:],axis=2).sum();assert changed>1000,(topic,'UI changes',changed)
        if topic==9:
            # Atomic light-list order can change FP accumulation rounding.
            compare(f'{topic}-ui-baseline',f'{topic}-ui-reset')
            assert np.max(np.abs(b[:,350:].astype(int)-r[:,350:].astype(int)))<=1,'Tiled Reset exceeds one display code value'
        else:assert np.array_equal(b[:,350:],r[:,350:]),(topic,'Scene Reset differs')
for label in [p.name[len(api)+1:-4] for p in work.glob(f'{api}-*.png') if p.name.startswith(api+'-4-')]:
    if label=='4-vertex':assert np.array_equal(png('4-base'),png(label)),'Basic Mesh / Vertex image mismatch'
if (work/f'{api}-4-uv.png').exists():
    colors=np.array([[1,0,0],[0,1,0],[0,0,1],[1,1,1]],float)
    for label in ['4-uv','4-transform']:
        image=png(label);h,w=image.shape[:2];tan=np.tan(np.deg2rad(60)/2)
        for row,col in [(h//2,w//2),(h//2-50,w//2+60),(h//2+60,w//2-50)]:
            z=5 if label.endswith('transform') else 4;x=((col+.5)/w*2-1)*tan*w/h*z;y=(1-(row+.5)/h*2)*tan*z
            if label.endswith('transform'):
                point=np.array([x-.5,y]);angle=.2;point=point@np.array([[np.cos(angle),-np.sin(angle)],[np.sin(angle),np.cos(angle)]])/.7;x,y=point
            u=x/4+.5;v=.5-y/4;tx=u*2-.5;ty=v*2-.5;x0=int(np.floor(tx));y0=int(np.floor(ty));fx=tx-x0;fy=ty-y0
            # The glTF default sampler wraps at both axes.
            c00=colors[(y0%2)*2+x0%2];c10=colors[(y0%2)*2+(x0+1)%2];c01=colors[((y0+1)%2)*2+x0%2];c11=colors[((y0+1)%2)*2+(x0+1)%2];rgb=(c00*(1-fx)+c10*fx)*(1-fy)+(c01*(1-fx)+c11*fx)*fy
            srgb=np.where(rgb<=.0031308,rgb*12.92,1.055*rgb**(1/2.4)-.055);assert np.max(np.abs(image[row,col].astype(float)/255-srgb))<.012,(api,label,row,col,image[row,col],srgb)
        print(api,label,'analytic UV and Albedo passed')
if (work/f'{api}-4-ui-baseline.png').exists():
    b,e,r=[png(f'4-ui-{name}') for name in ['baseline','edited','reset']];assert np.any(b[:,350:]!=e[:,350:],axis=2).sum()>1000;assert np.array_equal(b[:,350:],r[:,350:]),'Basic scene Reset differs'
if folder('5-grid').exists():compare('5-base','5-grid')
if folder('9-all').exists():compare('9-base','9-all');compare('9-base','9-overflow')
if folder('7-shadow-fixture').exists():
    a=hdr('7-shadow-fixture');b=hdr('7-shadow-fixture-off');assert np.count_nonzero(np.max(b[...,:3]-a[...,:3],axis=2)>.03)>100,'Off-camera caster shadow missing'
    args=read('7-shadow-fixture','mesh-arguments',np.uint32);assert args[33]>args[37],('Shadow must keep off-camera caster',args[32:38])
for topic in [7,8,9]:
    if folder(f'{topic}-mask').exists():
        opaque=np.count_nonzero(read(f'{topic}-ui-baseline','shadow-depth',np.float32)<1);masked=np.count_nonzero(read(f'{topic}-mask','shadow-depth',np.float32)<1)
        assert opaque>0 and .25<masked/opaque<.85,(topic,'MASK shadow coverage',opaque,masked)
if len(sys.argv)>3 and sys.argv[3]=='camera':
    baseline,reset='camera-baseline','camera-reset'
    assert (folder(baseline)/'mesh-global.bin').read_bytes()==(folder(reset)/'mesh-global.bin').read_bytes(),'Camera Reset did not restore scene constants'
    compare(baseline,reset,allow_depth_ties='Advanced05_' in api)
    oracle(baseline);oracle(reset)
    if 'Advanced09_' in api:
        assert np.max(np.abs(png(baseline).astype(int)-png(reset).astype(int)))<=1,'Camera Reset exceeds one display code value'
    print(api,'Camera scene constants, visibility and HDR Reset passed')
print(api,'Mesh Shader GPU numeric tests passed')
