"""Independent GPU readback checks, including CPU barycentric UV probes."""
from pathlib import Path
import json
import sys
import numpy as np

ROOT=Path(__file__).resolve().parents[1]
def require(ok, message):
    if not ok:raise AssertionError(message)
def load(directory, field):
    m=json.loads((directory/'metadata.json').read_text())
    a=np.fromfile(directory/(field+'.bin'),dtype='<f4').reshape(m['height'],m['width'],4)
    require(np.isfinite(a).all(),str(directory)+' nonfinite '+field)
    return a,m
def id_color(ids):
    h=(ids.astype(np.uint32)*np.uint32(747796405)+np.uint32(2891336453))
    h=((h >> ((h >> np.uint32(28))+np.uint32(4))) ^ h)*np.uint32(277803737)
    return .15+.85*np.stack([h&255,(h>>8)&255,(h>>16)&255],axis=-1)/255
class CpuGeometry:
    def __init__(self):
        p=ROOT/'Assets/Models/BindlessGallery';self.j=json.loads((p/'BindlessGallery.gltf').read_text());self.binary=(p/'BindlessGallery.bin').read_bytes()
        self.mapping=json.loads((ROOT/'Assets/Scenes/BindlessGallery/BindlessGallery.json').read_text())['gallery']['primitiveGeometry'];self.cache={}
    def accessor(self,index):
        a=self.j['accessors'][index];v=self.j['bufferViews'][a['bufferView']];w={'VEC2':2,'VEC3':3,'VEC4':4,'SCALAR':1}[a['type']];dt={5126:'<f4',5125:'<u4',5123:'<u2'}[a['componentType']]
        return np.frombuffer(self.binary,dtype=dt,count=a['count']*w,offset=v.get('byteOffset',0)+a.get('byteOffset',0)).reshape(-1,w).copy()
    def geometry(self,id):
        if id not in self.cache:
            p=self.j['meshes'][self.mapping.index(id)]['primitives'][0];pos=self.accessor(p['attributes']['POSITION']).astype(np.float64);pos[:,2]*=-1;uv=self.accessor(p['attributes']['TEXCOORD_0']).astype(np.float64);ind=self.accessor(p['indices']).reshape(-1,3)
            self.cache[id]=(pos[ind],uv[ind])
        return self.cache[id]
    def probe(self,directory,rt):
        hdr,m=load(directory,'hdr');surface,_=load(directory,'surface' if rt else 'position');valid=surface[...,3]>0
        ys,xs=np.nonzero(valid);require(len(ys)>10000,'UV coverage')
        config=json.loads((ROOT/'Assets/Scenes/BindlessGallery/BindlessGallery.json').read_text())['cameras']['MiniHero'];eye=np.array(config['position']);target=np.array(config['target']);eye[2]*=-1;target[2]*=-1
        forward=target-eye;forward/=np.linalg.norm(forward);right=np.cross([0,1,0],forward);right/=np.linalg.norm(right);up=np.cross(forward,right);tan=np.tan(np.deg2rad(config['fovDegrees'])/2)
        passed=0
        for i in np.linspace(0,len(ys)-1,48,dtype=int):
            y,x=ys[i],xs[i];instance=m['instances'][int(surface[y,x,3])-1];matrix=np.array(instance['world']).reshape(4,4)
            if rt:
                direction=forward+right*((x+.5)/1280*2-1)*(1280/720)*tan+up*(1-(y+.5)/720*2)*tan;direction/=np.linalg.norm(direction);point=eye+direction*surface[y,x,2]
            else:point=surface[y,x,:3]
            local=(np.r_[point,1]@np.linalg.inv(matrix))[:3];tri,uv=self.geometry(instance['geometry']);e0=tri[:,1]-tri[:,0];e1=tri[:,2]-tri[:,0];v=local-tri[:,0]
            d00=np.einsum('ij,ij->i',e0,e0);d01=np.einsum('ij,ij->i',e0,e1);d11=np.einsum('ij,ij->i',e1,e1);d20=np.einsum('ij,ij->i',v,e0);d21=np.einsum('ij,ij->i',v,e1);den=d00*d11-d01*d01
            b=(d11*d20-d01*d21)/den;c=(d00*d21-d01*d20)/den;a=1-b-c
            distance=np.linalg.norm(v-b[:,None]*e0-c[:,None]*e1,axis=1);candidates=(a>=-.001)&(b>=-.001)&(c>=-.001)&(distance<.002)
            expected=a[:,None]*uv[:,0]+b[:,None]*uv[:,1]+c[:,None]*uv[:,2];actual=hdr[y,x,:2]
            delta=np.abs((expected%1)-actual);delta=np.minimum(delta,1-delta)
            require(np.any(candidates & (delta.max(axis=1)<.006)),f'CPU UV mismatch {directory} pixel {x},{y}')
            if rt:require(np.max(np.abs(surface[y,x,:2]%1-actual))<.002,'RT surface UV differs')
            passed+=1
        return passed

def main():
    work=Path(sys.argv[1]);base=Path(sys.argv[2]);cpu=CpuGeometry();probes=0;comparisons=[]
    for api in ['Direct3D12','Vulkan']:
        for n,name in [(12,'BindlessMaterials'),(14,'BindlessRaytracing')]:
            prefix=f'{api}-Advanced{n}_{name}-'
            for field in ['material','geometry']:
                directory=work/(prefix+field+'-gpu');hdr,m=load(directory,'hdr');surface,_=load(directory,'surface' if n==14 else 'position');ids=surface[...,3].astype(int)-1;valid=ids>=0
                values=np.array([instance[field] for instance in m['instances']]);expected=id_color(values[np.maximum(ids,0)])
                require(np.allclose(hdr[valid,:3],expected[valid],atol=.0002),prefix+field+' diagnostic differs from CPU ID')
            probes+=cpu.probe(work/(prefix+'uv-gpu'),n==14)
        prefix=f'{api}-Advanced12_BindlessMaterials-ui'
        reference,_=load(work/(prefix+'-baseline-gpu'),'hdr')
        for stage in ['switched','reset']:
            actual,_=load(work/(prefix+'-'+stage+'-gpu'),'hdr');require(np.allclose(reference,actual,atol=.001,rtol=.002),prefix+' UI '+stage)
        for n,name in [(13,'BindlessMeshShader'),(15,'BindlessHybrid')]:
            prefix=f'{api}-Advanced{n}_{name}'
            reference,_=load(base/(prefix+'-resources-gpu'),'hdr');position,_=load(base/(prefix+'-resources-gpu'),'position')
            for mode in ['textures','resources']:
                actual,_=load(work/(prefix+'-driven-'+mode+'-gpu'),'hdr');p,_=load(work/(prefix+'-driven-'+mode+'-gpu'),'position')
                # As in MeshShaderGpuTests, exclude a one-pixel surface-boundary neighborhood.
                # Indirect atomic compaction changes ordering at coplanar/intersecting triangles.
                equal=position[...,3]==p[...,3]
                require(equal.mean()>.998,prefix+' excessive differing geometry IDs')
                boundary=~equal
                normal,_=load(base/(prefix+'-resources-gpu'),'normal');other,_=load(work/(prefix+'-driven-'+mode+'-gpu'),'normal')
                for surface,normals in [(position,normal),(p,other)]:
                    for axis in [0,1]:
                        boundary |= surface[...,3]!=np.roll(surface[...,3],1,axis)
                        boundary |= np.max(abs(normals[...,:3]-np.roll(normals[...,:3],1,axis)),axis=2)>.2
                excluded=boundary.copy()
                for dy in [-1,0,1]:
                    for dx in [-1,0,1]:excluded |= np.roll(boundary,(dy,dx),(0,1))
                valid=~excluded
                require(valid.mean()>.85,prefix+' excessive boundary exclusion')
                delta=abs(actual-reference);require(np.all(delta[valid]<=.002+.003*abs(reference[valid])),prefix+' driven shading mismatch')
                comparisons.append({'case':prefix,'binding':mode,'idAgreement':float(equal.mean()),'excludedBoundaryFraction':float(excluded.mean()),'maxHdrDifferenceAwayFromBoundary':float(delta[valid].max())})
                print(f'{prefix} {mode}: ID agreement {equal.mean():.6%}, boundary exclusion {excluded.mean():.3%}, HDR max {delta[valid].max():.6g}')
            empty,_=load(work/(prefix+'-empty-gpu'),'position');require(np.count_nonzero(empty[...,3])==0,prefix+' empty geometry output')
    (work/'Features.json').write_text(json.dumps({'cpuUvProbes':probes,'comparisons':comparisons,'passed':True},indent=2)+'\n')
    print(f'GPU input/Reset, ID colors, {probes} independent barycentric UV probes and driven/empty comparisons PASS')
if __name__=='__main__':main()
