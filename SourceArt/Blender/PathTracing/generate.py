"""Initial authoring only; never overwrite an edited source. MIT, masafumi 2026."""
from pathlib import Path
import sys, json
import bpy
import numpy as np
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'Workshop'))
from workshop_tools import material, camera, rh, render_settings, write_png

def main():
    source=HERE/'PathTracing.blend'
    if source.exists(): raise RuntimeError('Saved source exists: edit the blend and use export.py')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    scene=bpy.context.scene;scene.name='Room';scene['id']='scene:Room'
    temp=HERE/'.generated';temp.mkdir(exist_ok=True)
    images={}
    for name,color in [('Albedo',(255,255,255,255)),('Normal',(128,128,255,255)),('ORM',(255,255,255,255)),('Emissive',(255,255,255,255))]:
        path=temp/(name+'.png');write_png(path,np.tile(np.array(color,dtype=np.uint8),(8,8,1)))
        image=bpy.data.images.load(str(path));image.name='PathRoom_'+name
        image.colorspace_settings.name='sRGB' if name in ('Albedo','Emissive') else 'Non-Color';image.pack();images[name]=image
    white=material('RoomWhite',images,(.72,.72,.72),rough=.8)
    red=material('RoomRed',images,(.65,.045,.035),rough=.9)
    green=material('RoomGreen',images,(.045,.52,.10),rough=.9)
    smooth=material('SmoothMetal',images,(.85,.72,.47),metal=1,rough=.12)
    rough=material('RoughMetal',images,(.65,.72,.8),metal=1,rough=.55)
    emitter=material('CeilingEmitter',images,(1,1,1),rough=1,emission=12)
    def finish(obj,name,mat):
        obj.name=name;obj['id']='instance:'+name;obj.data.name=name+'Mesh';obj.data['id']='mesh:'+name;obj.data.materials.clear();obj.data.materials.append(mat)
        return obj
    def box(name,pos,size,mat):
        bpy.ops.mesh.primitive_cube_add(size=1,location=pos);obj=finish(bpy.context.object,name,mat);obj.scale=size
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);return obj
    box('Floor',(0,0,-.05),(5,5,.1),white)
    box('BackWall',(0,2.55,1.75),(5,.1,3.5),white)
    box('LeftWall',(-2.55,0,1.75),(.1,5,3.5),red)
    box('RightWall',(2.55,0,1.75),(.1,5,3.5),green)
    box('Ceiling',(0,0,3.55),(5,5,.1),white)
    cube=box('WhiteBox',(-1.3,.85,.6),(1.1,1.1,1.2),white);cube.rotation_euler.z=.25
    for name,pos,mat in [('SmoothSphere',(-1.25,-.95,.65),smooth),('RoughSphere',(1.25,.9,.7),rough)]:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=48,ring_count=24,radius=.65,location=pos)
        obj=finish(bpy.context.object,name,mat)
        for poly in obj.data.polygons:poly.use_smooth=True
    mesh=bpy.data.meshes.new('CeilingLightMesh');mesh.from_pydata([(-.9,-.7,3.48),(-.9,.7,3.48),(.9,.7,3.48),(.9,-.7,3.48)],[],[(0,1,2,3)]);mesh.update()
    obj=bpy.data.objects.new('CeilingLight',mesh);scene.collection.objects.link(obj);finish(obj,'CeilingLight',emitter)
    uv=mesh.uv_layers.new();coords=[(0,0),(0,1),(1,1),(1,0)]
    for loop in mesh.loops:uv.data[loop.index].uv=coords[loop.vertex_index]
    with bpy.data.libraries.load(str(ROOT/'SourceArt/Blender/ShaderBall/ShaderBall.blend'),link=False) as (src,dst):
        dst.objects=['ShaderBall']
    ball=dst.objects[0];scene.collection.objects.link(ball)
    ball.location=(.65,-.8,0);ball.scale=(.55,)*3
    ball_images={}
    for name in ('Albedo','Normal','ORM','Emissive'):
        image=bpy.data.images.load(str(ROOT/f'Assets/Models/ShaderBall/Textures/{name}.png'),check_existing=True)
        image.name='ShaderBall_'+name;image.colorspace_settings.name='sRGB' if name in ('Albedo','Emissive') else 'Non-Color';image.pack();ball_images[name]=image
    finish(ball,'ShaderBall',material('ShaderBallReused',ball_images,(1,1,1),metal=1,rough=1,emission=1))
    # Exporters may round UV-pole tangents; triangulate and keep authored UVs.
    for obj in list(scene.objects):
        if obj.type=='MESH':
            bpy.context.view_layer.objects.active=obj;mod=obj.modifiers.new('Triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
    cam=camera(scene,'Room',(0,-7.6,2.55),(0,.35,1.55),42);scene.camera=cam
    camera(scene,'RoomClose',(1.7,-4.4,1.8),(0,0,1),48)
    config={'profile':'pathtracing-scene-1','model':'../../Models/PathTracing/PathTracing.gltf','defaultScene':'Room','exposure':1,
        'coordinateSystem':'right-handed Y-up meters','cameras':{}}
    for obj in scene.objects:
        if obj.type=='CAMERA':config['cameras'][obj.name]={'position':rh(obj.location),'target':list(obj['targetRH']),'fovDegrees':42 if obj==cam else 48,'near':.05,'far':100}
    scene['pathtracing_config']=json.dumps(config)
    scene.world=bpy.data.worlds.new('BlackWorld');scene.world.use_nodes=True;scene.world.node_tree.nodes.get('Background').inputs['Strength'].default_value=0
    render_settings(scene,256,False)
    bpy.ops.wm.save_as_mainfile(filepath=str(source))

if __name__=='__main__':main()
