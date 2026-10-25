"""Export the saved BSDF asset source. Runtime reuses the immutable PathTracing Room. MIT."""
from pathlib import Path
import argparse,json,sys
import bpy
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE.parent/'Workshop'))
from workshop_tools import ROOT,arguments,json_write,save_source_image

def main():
    p=argparse.ArgumentParser();p.add_argument('--output-dir',type=Path,default=ROOT/'Assets');a=arguments(p)
    if Path(bpy.data.filepath).stem!='BSDFMaterials':raise RuntimeError('Open the saved BSDFMaterials.blend')
    directory=a.output_dir/'Models'/'BSDFMaterials';directory.mkdir(parents=True,exist_ok=True)
    path=directory/'BSDFMaterials.gltf';config=json.loads(bpy.data.scenes['Lab']['bsdf_config'])
    bpy.ops.export_scene.gltf(filepath=str(path),export_format='GLTF_SEPARATE',export_texture_dir='Textures',export_yup=True,
        export_normals=True,export_tangents=True,export_texcoords=True,export_materials='EXPORT',export_vertex_color='NONE',
        export_animations=False,export_morph=False,export_skins=False,export_cameras=True,export_lights=False,export_extras=True,export_apply=False)
    data=json.loads(path.read_text(encoding='utf-8'));data['images']=[];data['textures']=[]
    data['samplers']=[{'magFilter':9729,'minFilter':9987,'wrapS':10497,'wrapT':10497}]
    image_map={};used=set()
    def texture(image):
        if image.name not in image_map:
            image_map[image.name]=len(data['images']);save_source_image(image,directory/'Textures'/f'{image.name}.png')
            data['images'].append({'uri':'Textures/'+image.name+'.png'});data['textures'].append({'source':image_map[image.name],'sampler':0})
        return {'index':image_map[image.name]}
    for m in data['materials']:
        source=bpy.data.materials[m['name']];profile=json.loads(source['profile']);m.clear();m.update({'name':source.name,'extras':{'id':source['id']}})
        pbr={key:profile[key] for key in ['baseColorFactor','metallicFactor','roughnessFactor']}
        images={key:source.node_tree.nodes['Texture_'+key].image for key in ['Albedo','Normal','ORM','Emissive']}
        pbr['baseColorTexture']=texture(images['Albedo']);pbr['metallicRoughnessTexture']=texture(images['ORM'])
        m.update({'pbrMetallicRoughness':pbr,'normalTexture':texture(images['Normal']),'occlusionTexture':texture(images['ORM']),
            'emissiveTexture':texture(images['Emissive']),'emissiveFactor':[0,0,0],'alphaMode':'OPAQUE','doubleSided':False})
        if 'bsdf' in source:
            b=json.loads(source['bsdf']);volume={'thicknessFactor':b['thickness'],'attenuationColor':b['attenuationColor']}
            if b['attenuationDistance']:volume['attenuationDistance']=b['attenuationDistance']
            m['extensions']={'KHR_materials_transmission':{'transmissionFactor':b['transmission']},'KHR_materials_ior':{'ior':b['ior']},'KHR_materials_volume':volume}
            used.update(m['extensions'])
    for m in data['meshes']:
        source=bpy.data.meshes[m['name']];mid=source['id'];m['extras']={'id':mid}
        for i,p in enumerate(m['primitives']):p['extras']={'id':mid+':primitive:'+str(i)}
    for n in data['nodes']:
        extras=n.get('extras',{});n['extras']={k:v for k,v in extras.items() if k in ('id','targetRH')}
        if 'id' not in n['extras']:n['extras']['id']='bsdf:node:'+n['name']
    for s in data['scenes']:
        s['extras']={'id':'bsdf:scene:'+s['name']}
        for index in s['nodes']:
            node=data['nodes'][index]
            if node.get('extras',{}).get('id')=='bsdf:instance:GridBackdrop':node['extras']['id']+=':'+s['name']
    data['scene']=next(i for i,s in enumerate(data['scenes']) if s['name']=='Lab')
    data['extensionsUsed']=sorted(used);data.pop('extensionsRequired',None)
    json_write(path,data);json_write(a.output_dir/'Scenes'/'BSDFMaterials'/'BSDFMaterials.json',config)
    print('Exported BSDF overlay; original Room files are unchanged',flush=True)
if __name__=='__main__':main()
