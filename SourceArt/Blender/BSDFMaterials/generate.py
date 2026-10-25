"""Initial BSDF teaching assets. Never overwrites the editable source. MIT, masafumi 2026."""
from pathlib import Path
import json,sys
import bpy
import numpy as np
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'Workshop'))
from workshop_tools import material,camera,rh,write_png,render_settings

def main():
    source=HERE/'BSDFMaterials.blend'
    if source.exists(): raise RuntimeError('Edit the saved source and use export.py; do not regenerate over it')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    lab=bpy.context.scene;lab.name='Lab';lab['id']='scene:BSDFLab'
    images={};temp=HERE/'.generated';temp.mkdir(exist_ok=True)
    for name,color in [('Albedo',(255,255,255,255)),('Normal',(128,128,255,255)),('ORM',(255,255,255,255)),('Emissive',(255,255,255,255))]:
        path=temp/(name+'.png');write_png(path,np.tile(np.array(color,dtype=np.uint8),(8,8,1)))
        im=bpy.data.images.load(str(path));im.name='BSDF_'+name;im.colorspace_settings.name='sRGB' if name in ('Albedo','Emissive') else 'Non-Color';im.pack();images[name]=im
    grid=np.full((512,512,4),255,dtype=np.uint8)
    palette=np.array([[210,215,225],[35,45,65]],dtype=np.uint8)
    for y in range(512):
        for x in range(512):
            grid[y,x,:3]=palette[((x//32)+(y//32))%2]
            if x%32<2 or y%32<2:grid[y,x,:3]=(85,125,165)
    grid[:,250:262,:3]=(220,65,45);grid[250:262,:,:3]=(40,180,100)
    path=temp/'Grid.png';write_png(path,grid)
    im=bpy.data.images.load(str(path));im.name='BSDF_Grid';im.pack();grid_images=dict(images,Albedo=im)
    mats={}
    for name,rough,color,distance in [('ClearGlass',0,(1,1,1),0),('RoughGlass',.3,(1,1,1),0),('AbsorbingGlass',0,(.18,.65,.85),.5)]:
        m=material(name,images,(1,1,1),rough=rough)
        props={'transmission':1,'ior':1.5,'thickness':.5,'attenuationColor':color,'attenuationDistance':distance}
        m['bsdf']=json.dumps(props);m.node_tree.nodes['PBR'].inputs['Transmission Weight'].default_value=1
        m.node_tree.nodes['PBR'].inputs['IOR'].default_value=1.5
        if distance:
            vol=m.node_tree.nodes.new('ShaderNodeVolumeAbsorption');vol.inputs['Color'].default_value=(*color,1);vol.inputs['Density'].default_value=1/distance
            output=next(n for n in m.node_tree.nodes if n.type=='OUTPUT_MATERIAL');m.node_tree.links.new(vol.outputs[0],output.inputs['Volume'])
        mats[name]=m
    mats['Grid']=material('Grid',grid_images,(1,1,1),rough=.85)
    def finish(o,name,m):
        o.name=name;o['id']='bsdf:instance:'+name;o.data.name='BSDF_'+name;o.data['id']='bsdf:mesh:'+name
        o.data.materials.clear();o.data.materials.append(m)
        if not o.data.uv_layers:
            o.data.uv_layers.new()
        return o
    for name,pos,mat in [('ClearSphere',(-1.25,-.95,.65),mats['ClearGlass']),('RoughSphere',(1.25,.9,.65),mats['RoughGlass'])]:
        bpy.ops.mesh.primitive_uv_sphere_add(segments=64,ring_count=32,radius=.65,location=pos)
        o=finish(bpy.context.object,name,mat)
        for p in o.data.polygons:p.use_smooth=True
    # A closed triangular prism: thickness grows from left to right.
    vertices=[(-.55,-.45,0),(.55,-.45,0),(.55,.45,0),(-.55,-.45,1.35),(.55,-.45,1.35),(.55,.45,1.35)]
    mesh=bpy.data.meshes.new('Wedge');mesh.from_pydata(vertices,[],[(0,2,1),(3,4,5),(0,1,4,3),(1,2,5,4),(2,0,3,5)]);mesh.update()
    o=bpy.data.objects.new('AbsorbingWedge',mesh);lab.collection.objects.link(o);o.location=(-1.25,.95,0);finish(o,'AbsorbingWedge',mats['AbsorbingGlass'])
    # Planar UVs are sufficient for the constant dielectric texture inputs.
    for poly in mesh.polygons:
        for li in poly.loop_indices:
            co=mesh.vertices[mesh.loops[li].vertex_index].co
            dominant=max(range(3),key=lambda axis:abs(poly.normal[axis]))
            axes=[axis for axis in range(3) if axis!=dominant]
            mesh.uv_layers.active.data[li].uv=(co[axes[0]],co[axes[1]])
    def box(name,pos,size,mat):
        bpy.ops.mesh.primitive_cube_add(size=1,location=pos);o=finish(bpy.context.object,name,mat);o.scale=size
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);return o
    backdrop=box('GridBackdrop',(0,2.38,1.72),(4.8,.02,3.35),mats['Grid'])
    camera(lab,'Lab',(0,-7.6,2.55),(0,.35,1.55),42)
    camera(lab,'LabClose',(0,-5.6,2.05),(0,.25,1.15),48)
    camera(lab,'InsideClear',(-.75,-.95,.65),(-1.25,2.3,.65),70)
    thickness=bpy.data.scenes.new('Thickness');thickness['id']='scene:BSDFThickness';bpy.context.window.scene=thickness
    thickness.collection.objects.link(backdrop)
    for name,x,d in [('ThinPlate',-.85,.1),('ThickPlate',.85,.5)]:box(name,(x,0,.9),(1.1,d,1.8),mats['AbsorbingGlass'])
    camera(thickness,'Thickness',(0,-6.6,1.8),(0,0,1),42)
    camera(thickness,'ThicknessOblique',(2.4,-5.6,2),(0,0,1),45)
    for o in list(bpy.data.objects):
        if o.type=='MESH':
            bpy.context.window.scene=next(s for s in bpy.data.scenes if o.name in s.objects)
            bpy.context.view_layer.objects.active=o
            modifier=o.modifiers.new('Triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=modifier.name)
    keep=['instance:'+n for n in ['Floor','BackWall','LeftWall','RightWall','Ceiling','CeilingLight']]
    config={'profile':'bsdf-scene-1','model':'../../Models/BSDFMaterials/BSDFMaterials.gltf','baseScene':'../PathTracing/PathTracing.json',
        'baseInstances':{'Lab':keep+['instance:ShaderBall'],'Thickness':keep},'coordinateSystem':'right-handed Y-up meters',
        'exposure':1,'environment':{'prefix':'../../Environments/Workshop/WorkshopSky','intensity':0},'cameras':{}}
    for o in bpy.data.objects:
        if o.type=='CAMERA':config['cameras'][o.name]={'position':rh(o.location),'target':list(o['targetRH']),'fovDegrees':70 if o.name=='InsideClear' else 48 if o.name=='LabClose' else 42,'near':.01,'far':100}
    lab['bsdf_config']=json.dumps(config)
    for s in bpy.data.scenes:
        s.world=bpy.data.worlds.new('BlackWorld_'+s.name);s.world.use_nodes=True;s.world.node_tree.nodes['Background'].inputs['Strength'].default_value=0
        render_settings(s,256,False);s.render.resolution_x=1920;s.render.resolution_y=1080
        s.camera=next(o for o in s.objects if o.type=='CAMERA' and o.name==s.name)
    bpy.context.window.scene=lab
    bpy.ops.wm.save_as_mainfile(filepath=str(source))
    print('Saved editable BSDFMaterials.blend (additional assets only)',flush=True)
if __name__=='__main__':main()
