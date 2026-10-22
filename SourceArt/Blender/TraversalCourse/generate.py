"""Create a new editable static traversal course. MIT, masafumi 2026."""
import bpy
import json
import math
from pathlib import Path
HERE=Path(__file__).resolve().parent
path=HERE/'TraversalCourse.blend'
if path.exists():raise RuntimeError('Course source exists; edit it instead of regenerating')
bpy.ops.wm.read_factory_settings(use_empty=True)
scene=bpy.context.scene;scene.name='TraversalCourse';scene.unit_settings.system='METRIC'
def collection(name):
    c=bpy.data.collections.new(name);scene.collection.children.link(c);return c
visual=collection('Render');collision=collection('Collision');markers=collection('Targets')
materials=[]
for name,color in [('Ground',(.18,.23,.29,1)),('Step',(.12,.55,.65,1)),('Barrier',(.86,.44,.12,1)),('Platform',(.32,.65,.26,1)),('Wall',(.6,.12,.14,1))]:
    m=bpy.data.materials.new(name);m.diffuse_color=color;m.use_nodes=True;p=m.node_tree.nodes.get('Principled BSDF');p.inputs['Base Color'].default_value=color;p.inputs['Roughness'].default_value=.72;materials.append(m)
def box(name,center,half,index,yaw=0):
    bpy.ops.mesh.primitive_cube_add(size=2,location=(center[0],center[2],center[1]))
    obj=bpy.context.object;obj.name=name;obj.scale=(half[0],half[2],half[1]);bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);obj.rotation_euler.z=-yaw
    for c in list(obj.users_collection):c.objects.unlink(obj)
    visual.objects.link(obj);obj.data.materials.append(materials[index]);obj['id']=name
    col=bpy.data.objects.new('COL_'+name,None);collision.objects.link(col);col.location=obj.location;col.rotation_euler=obj.rotation_euler;col.empty_display_type='CUBE';col.empty_display_size=1;col.scale=(half[0],half[2],half[1]);col['obstacle_id']=name
box('TrainingFloor',(0,-.25,-5),(9,.25,10),0)
box('LowStep',(0,.1,-2.6),(1.3,.1,.6),1)
box('JumpBarrier',(0,.325,-5.5),(1.3,.325,.3),2)
box('RaisedPlatform',(3,.25,-8),(1.5,.25,1.5),3)
box('RotatedWall',(-4,1,-3),(1.2,1,.3),2,.3)
box('TallWall',(5,1.3,-3),(1,1.3,1),4)
sun=bpy.data.lights.new('CourseSun','SUN');sun.energy=3;light=bpy.data.objects.new('CourseSun',sun);scene.collection.objects.link(light);light.rotation_euler=(.4,-.6,-.4)
spawn=bpy.data.objects.new('Spawn',None);markers.objects.link(spawn);spawn.location=(0,1,0);spawn['role']='spawn'
for name,p in [('Checkpoint1',(0,-3.8,0)),('Checkpoint2',(3,-7,.5)),('Finish',(0,-12,0))]:
    obj=bpy.data.objects.new(name,None);markers.objects.link(obj);obj.location=p;obj['role']='checkpoint'
scene['course_version']=1
bpy.ops.wm.save_as_mainfile(filepath=str(path))
