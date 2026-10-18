from pathlib import Path
import sys
import numpy as np
import json
root=Path(sys.argv[1])
for api in ['Direct3D12','Vulkan']:
 for topic in ['Advanced12_BindlessMaterials','Advanced13_BindlessMeshShader','Advanced14_BindlessRaytracing','Advanced15_BindlessHybrid']:
  label=f'{api}-{topic}-ui'
  def read(stage):return np.fromfile(root/(label+'-'+stage+'-gpu/hdr.bin'),dtype='<f4')
  a=read('baseline')
  for stage in ['switched','reset']:
   b=read(stage)
   if not np.isfinite(b).all() or not np.allclose(a,b,atol=.001,rtol=.002):raise AssertionError(label+' '+stage+' differs')
  print(label+' scene HDR round trip PASS')
