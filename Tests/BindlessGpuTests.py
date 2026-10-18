"""Compare actual GPU float32 captures; no renderer implementation is imported."""
from pathlib import Path
import json
import sys
import numpy as np

TOPICS=['Advanced12_BindlessMaterials','Advanced13_BindlessMeshShader','Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid']
def check(ok,message):
    if not ok:raise AssertionError(message)
def image(directory,name):
    metadata=json.loads((directory/'metadata.json').read_text())
    a=np.fromfile(directory/(name+'.bin'),dtype='<f4').reshape(metadata['height'],metadata['width'],4)
    check(np.isfinite(a).all(),'NaN/Inf: '+str(directory/name));return a
def main():
    root=Path(sys.argv[1]);apis=sys.argv[2:] or ['Direct3D12','Vulkan'];summary={}
    for api in apis:
        for topic in TOPICS:
            modes=['bound','textures']+([] if topic.startswith('Advanced12') else ['resources'])
            reference=image(root/f'{api}-{topic}-bound-gpu','hdr')
            for mode in modes:
                directory=root/f'{api}-{topic}-{mode}-gpu';actual=image(directory,'hdr')
                difference=abs(actual-reference);tolerance=.001+.002*abs(reference)
                check(np.all(difference<=tolerance),f'{api} {topic} {mode} HDR mismatch: {difference.max()}')
                metadata=json.loads((directory/'metadata.json').read_text());check(metadata['width']==1280 and metadata['height']==720,'Capture resolution')
                if topic.startswith('Advanced14'):
                    surface=image(directory,'surface')
                    check(np.allclose(surface,image(root/f'{api}-{topic}-bound-gpu','surface'),atol=.001,rtol=.0001),'RT UV, hit distance or instance differs')
                if not topic.startswith('Advanced14'):
                    depth=np.fromfile(directory/'depth.bin',dtype='<f4').reshape(720,1280)
                    reference_depth=np.fromfile(root/f'{api}-{topic}-bound-gpu'/'depth.bin',dtype='<f4').reshape(720,1280)
                    check(np.isfinite(depth).all() and depth.min()>=0 and depth.max()<=1,'D32 range')
                    check(np.allclose(depth,reference_depth,atol=2e-6,rtol=0),'D32 differs between bindings')
                    position=image(directory,'position');base=image(directory,'base');normal=image(directory,'normal')
                    valid=position[...,3]>0
                    check(valid.sum()>10000,'Insufficient visible scene coverage')
                    check(np.array_equal(depth<1,valid),'Depth and MASK coverage differ')
                    check(len(np.unique(position[...,3][valid]))>=16,'Too few visible instances')
                    check(base[...,:3].max()>.05 and np.max(abs(np.linalg.norm(normal[valid,:3],axis=1)-1))<.003,'Material/normal data')
                    check(np.allclose(position,image(root/f'{api}-{topic}-bound-gpu','position'),atol=1e-5,rtol=1e-5),'Geometry/instance mismatch')
                summary[f'{api}-{topic}-{mode}']={'maxHdrDifference':float(difference.max())}
                if not topic.startswith('Advanced14'):summary[f'{api}-{topic}-{mode}']['maxD32Difference']=float(abs(depth-reference_depth).max())
    (root/'Comparison.json').write_text(json.dumps(summary,indent=2)+'\n')
    print(f'{len(summary)} GPU mode captures: finite HDR, equivalent scene and material access PASS')
if __name__=='__main__':main()
