"""Export the edited saved course into the existing Workshop scene profile."""
import bpy
import hashlib
import json
import math
from pathlib import Path
import sys
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
if Path(bpy.data.filepath).name!='TraversalCourse.blend':raise RuntimeError('Open the saved course')
out=ROOT/'Assets/Models/TraversalCourse'
if '--' in sys.argv:out=Path(sys.argv[sys.argv.index('--')+1]).resolve()
out.mkdir(parents=True,exist_ok=True)
sys.path.insert(0,str(HERE.parent/'Workshop'))
from workshop_tools import json_write
from export import canonical_tangents
selected=[]
for o in bpy.context.scene.objects:
    o.select_set(o.type in ['MESH','LIGHT'])
    if o.select_get():selected.append(o)
bpy.context.view_layer.objects.active=selected[0]
bpy.ops.export_scene.gltf(filepath=str(out/'Course.gltf'),export_format='GLTF_SEPARATE',use_selection=True,export_animations=False,export_skins=False,export_yup=True,export_normals=True,export_tangents=True,export_lights=True,export_extras=True)
data=json.loads((out/'Course.gltf').read_text())
for i,(name,color) in enumerate([('White',(1,1,1,1)),('Normal',(.5,.5,1,1)),('ORM',(1,.72,0,1)),('Black',(0,0,0,1))]):
    image=bpy.data.images.new('Course'+name,width=2,height=2)
    if name in ['Normal','ORM']:image.colorspace_settings.name='Non-Color'
    image.pixels=list(color)*4
    image.filepath_raw=str(out/(name+'.png'));image.file_format='PNG';image.save()
data['images']=[dict(uri=n+'.png') for n in ['White','Normal','ORM','Black']]
data['samplers']=[dict(magFilter=9729,minFilter=9987,wrapS=10497,wrapT=10497)]
data['textures']=[dict(source=i,sampler=0) for i in range(4)]
for mat in data['materials']:
    mat['extras']={'id':'material:'+mat['name']};pbr=mat['pbrMetallicRoughness'];pbr.update(baseColorTexture={'index':0},metallicRoughnessTexture={'index':2},metallicFactor=0,roughnessFactor=1)
    mat.update(normalTexture={'index':1},occlusionTexture={'index':2},emissiveTexture={'index':3},emissiveFactor=[0,0,0]);mat.pop('extensions',None)
for mesh in data['meshes']:
    mesh['extras']={'id':'mesh:'+mesh['name']}
    for i,p in enumerate(mesh['primitives']):p['extras']={'id':f"primitive:{mesh['name']}:{i}"}
for node in data['nodes']:node['extras']={'id':node['name']}
for scene in data['scenes']:scene['extras']={'id':'scene:'+scene['name']}
data['extensionsUsed']=['KHR_lights_punctual'];data.pop('extensionsRequired',None)
for light in data['extensions']['KHR_lights_punctual']['lights']:light['intensity']=2049
json_write(out/'Course.gltf',data);canonical_tangents(data,out)
camera=dict(position=[0,2,5],target=[0,1,0],fovDegrees=50,near=.05,far=100)
json_write(out/'Scene.json',dict(profile='workshop-scene-1',model='Course.gltf',cameras={'Hero':camera},environment={'prefix':'../../Environments/LightingLab/Studio'},exposure=1))
json_write(out/'CameraPath.json',{'frames':[camera]})
def lh(v):return [float(v[0]),float(v[2]),float(v[1])]
boxes=[]
for o in bpy.data.collections['Collision'].objects:
    if abs(o.rotation_euler.x)>1e-5 or abs(o.rotation_euler.y)>1e-5 or min(o.scale)<=0:raise RuntimeError('Collision boxes require positive dimensions and horizontal rotation only')
    boxes.append(dict(id=o['obstacle_id'],center=lh(o.location),half=lh(o.scale),yaw=-float(o.rotation_euler.z)))
targets=[]
for o in bpy.data.collections['Targets'].objects:
    targets.append(dict(id=o.name,role=o.get('role',''),obstacle=o.get('obstacle_id',''),position=lh(o.matrix_world.translation)))
json_write(out/'Traversal.json',dict(version=1,boxes=boxes,targets=targets,sourceBlendSha256=hashlib.sha256(Path(bpy.data.filepath).read_bytes()).hexdigest()))
print('Saved course exported:',len(boxes),'collision boxes')
