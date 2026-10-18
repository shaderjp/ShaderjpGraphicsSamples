from pathlib import Path
import json
import sys
import numpy as np
work,base=map(Path,sys.argv[1:]);summary={}
for api in ['Direct3D12','Vulkan']:
 for topic in ['Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid']:
  prefix=f'{api}-{topic}';reference=base/(prefix+'-textures-gpu');hdr=np.fromfile(reference/'hdr.bin',dtype='<f4').reshape(-1,4)
  field='surface' if topic.startswith('Advanced14') else 'position';position=np.fromfile(reference/(field+'.bin'),dtype='<f4')
  for effect in ['shadow','reflections']:
   directory=work/(prefix+'-no-'+effect+'-gpu');a=np.fromfile(directory/'hdr.bin',dtype='<f4').reshape(-1,4);p=np.fromfile(directory/(field+'.bin'),dtype='<f4')
   if not np.isfinite(a).all() or not np.allclose(position,p,atol=.001,rtol=.0001):raise AssertionError(prefix+' geometry changed')
   count=np.count_nonzero(np.max(abs(a[:,:3]-hdr[:,:3]),axis=1)>.001)
   if count<500:raise AssertionError(prefix+' '+effect+' had no material effect: '+str(count))
   summary[prefix+'-'+effect]={'changedPixels':int(count),'maxHdrDifference':float(abs(a-hdr).max())}
(work/'Effects.json').write_text(json.dumps(summary,indent=2)+'\n')
print('8 independent RT shadow/reflection controls change HDR and preserve geometry PASS')
