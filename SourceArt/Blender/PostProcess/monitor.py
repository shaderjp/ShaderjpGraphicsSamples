"""Create once, or export a saved Monitor.blend without altering it. MIT License."""
from pathlib import Path
import bpy,json,sys,argparse
import numpy as np
HERE=Path(__file__).resolve().parent;ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE.parent/'ShaderBall'))
from shader_ball_tools import write_png,build_material,save_source_image
args=sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else []
p=argparse.ArgumentParser();p.add_argument('--create',action='store_true');p.add_argument('--output',type=Path,default=ROOT/'Assets');a=p.parse_args(args)
if a.create:
    if (HERE/'Monitor.blend').exists():raise RuntimeError('Refusing to overwrite saved Monitor.blend')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    images={}
    for name,value in [('Albedo',[.2,.2,.2,1]),('Normal',[.5,.5,1,1]),('ORM',[1,.5,0,1]),('Emissive',[0,0,0,1])]:
        q=HERE/'.generated'/f'{name}.png';write_png(q,np.tile(np.rint(np.array(value)*255).astype(np.uint8),(4,4,1)))
        images[name]=bpy.data.images.load(str(q),check_existing=False)
        images[name].colorspace_settings.name='sRGB' if name in ('Albedo','Emissive') else 'Non-Color'
        images[name].pack()
    mat=build_material(images)
    bpy.ops.mesh.primitive_cube_add(size=1);o=bpy.context.object;o.name='Monitor';o.dimensions=(1.4,.08,1.4)
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True);o.data.materials.append(mat)
    for polygon in o.data.polygons:
        axes=(0,2) if abs(polygon.normal.y)>.5 else (1,2) if abs(polygon.normal.x)>.5 else (0,1)
        for index in polygon.loop_indices:
            co=o.data.vertices[o.data.loops[index].vertex_index].co
            o.data.uv_layers.active.data[index].uv=tuple(co[axis]/o.dimensions[axis]+.5 for axis in axes)
    bpy.ops.wm.save_as_mainfile(filepath=str(HERE/'Monitor.blend'))
if Path(bpy.data.filepath).stem!='Monitor':raise RuntimeError('Open saved Monitor.blend')
o=bpy.data.objects['Monitor'];bpy.ops.object.select_all(action='DESELECT');o.select_set(True);bpy.context.view_layer.objects.active=o
out=a.output/'Models'/'PostProcess'/'Monitor';out.mkdir(parents=True,exist_ok=True)
for key in ('Albedo','Normal','ORM','Emissive'):save_source_image(o.data.materials[0].node_tree.nodes['Texture_'+key].image,out/'Textures'/f'{key}.png')
bpy.ops.export_scene.gltf(filepath=str(out/'Monitor.gltf'),export_format='GLTF_SEPARATE',use_selection=True,export_yup=True,
    export_texture_dir='Textures',export_normals=True,export_tangents=True,export_texcoords=True,export_vertex_color='NONE',export_animations=False,export_morph=False,export_skins=False,export_cameras=False,export_lights=False,export_extras=False)
q=out/'Monitor.gltf';j=json.loads(q.read_text());m=j['materials'][0];tex=j['textures']
for key,info in [('Albedo',m['pbrMetallicRoughness']['baseColorTexture']),('Normal',m['normalTexture']),('ORM',m['pbrMetallicRoughness']['metallicRoughnessTexture']),('Emissive',m['emissiveTexture'])]:
    j['images'][tex[info['index']]['source']]['uri']=f'Textures/{key}.png'
q.write_text(json.dumps(j,indent=2)+'\n',encoding='utf-8',newline='\n')
print('Monitor exported from saved source')
