"""Cycles references from SAVED geometry; never saves authoring sources. MIT License."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
import time
import bpy
import numpy as np
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE))
from workshop_tools import ROOT, arguments, render_settings, json_write, write_png

SHOTS=[('Hero_Direct','Standard','Hero','direct','OpaqueMask',1,False),
    ('Hero_Environment','Standard','Hero','environment','OpaqueMask',1,False),
    ('Hero_Combined','Standard','Hero','combined','OpaqueMask',1,False),
    ('Materials','Standard','Materials','combined','OpaqueMask',1,False),
    ('Occlusion','Standard','Occlusion','combined','OpaqueMask',1,False),
    ('Transparency','Standard','Transparency','combined','Full',1,False),
    ('Stress','Stress','StressHero','combined','OpaqueMask',1,False),
    ('Beauty','Standard','Hero','combined','Full',8,True)]

def sha(path): return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def main():
    p=argparse.ArgumentParser(); p.add_argument('--output-dir',type=Path,default=ROOT/'Docs'/'Assets'/'Images'/'Workshop')
    p.add_argument('--preview',action='store_true'); p.add_argument('--shot',choices=[s[0] for s in SHOTS])
    p.add_argument('--force',action='store_true'); p.add_argument('--source-dir',type=Path)
    args=arguments(p)
    if args.source_dir: bpy.ops.wm.open_mainfile(filepath=str(args.source_dir/'Workshop.blend'))
    if Path(bpy.data.filepath).stem!='Workshop': raise RuntimeError('Open saved Workshop.blend')
    source=Path(bpy.data.filepath).parent; original=sha(source/'Workshop.blend')
    config=json.loads(bpy.data.scenes['Standard']['workshop_config'])
    out=args.output_dir.resolve(); out.mkdir(parents=True,exist_ok=True)
    report_path=out/'RenderSettings.json'
    report=json.loads(report_path.read_text(encoding='utf-8')) if report_path.exists() else {'shots':{}}
    report['blender']={'version':bpy.app.version_string,'hash':bpy.app.build_hash.decode(),'exporter':'5.2.40'}
    report['sourceSha256']=original
    env=source/'WorkshopSky.exr'
    if env.exists():
        image=bpy.data.images.load(str(env),check_existing=False)
        for world in bpy.data.worlds:
            if world.name.startswith('WorkshopSky'):
                nodes,links=world.node_tree.nodes,world.node_tree.links
                tex=nodes.new('ShaderNodeTexEnvironment'); tex.image=image
                links.new(tex.outputs[0],nodes['Environment'].inputs['Color'])
        report['environmentSha256']=sha(env)
    selected=[s for s in SHOTS if not args.shot or s[0]==args.shot]
    energies={obj.data:obj.data.energy for obj in bpy.data.objects if obj.type=='LIGHT'}
    strengths={m:m.node_tree.nodes['PBR'].inputs['Emission Strength'].default_value
        for m in bpy.data.materials if m.use_nodes and m.node_tree.nodes.get('PBR')}
    for name,scene_name,cam,lighting,preset,bounces,denoise in selected:
        scene=bpy.data.scenes[scene_name]; bpy.context.window.scene=scene
        render_settings(scene,32 if args.preview else 512,denoise)
        scene.render.resolution_percentage=50 if args.preview else 100
        scene.cycles.max_bounces=bounces; scene.camera=bpy.data.objects[cam]
        scene.world.node_tree.nodes['Environment'].inputs['Strength'].default_value=0 if lighting=='direct' else config['environment']['intensity']
        for obj in scene.objects:
            if obj.type=='LIGHT': obj.data.energy=0 if lighting=='environment' else energies[obj.data]
            if obj.type=='MESH':
                modes={json.loads(m['profile'])['alphaMode'] for m in obj.data.materials}
                obj.hide_render=not modes.issubset(set(config['presets'][preset]))
        # max_bounces=1 prevents material-to-material GI; beauty includes emission GI.
        for m,strength in strengths.items(): m.node_tree.nodes['PBR'].inputs['Emission Strength'].default_value=strength
        settings={'scene':scene_name,'camera':config['cameras'][cam],'lighting':lighting,'preset':preset,
            'samples':scene.cycles.samples,'seed':0,'device':'CPU','adaptiveSampling':False,'denoise':denoise,
            'maxBounces':bounces,'transparentMaxBounces':16,'width':640 if args.preview else 1280,
            'height':360 if args.preview else 720,'exposure':config['exposure'],
            'display':'AgX Medium High Contrast' if denoise else 'Reinhard + exact sRGB',
            'environmentIntensity':0 if lighting=='direct' else config['environment']['intensity']}
        fingerprint=hashlib.sha256((original+sha(__file__)+(sha(env) if env.exists() else 'analytic')+json.dumps(settings,sort_keys=True)).encode()).hexdigest()
        base=out/('Workshop_'+name); exr=base.with_suffix('.exr'); png=base.with_suffix('.png')
        old=report['shots'].get(name,{})
        if not args.force and old.get('fingerprint')==fingerprint and exr.exists() and png.exists() and old.get('exrSha256')==sha(exr) and old.get('pngSha256')==sha(png):
            print('Unchanged reference '+name,flush=True); continue
        start=time.monotonic(); scene.render.filepath=str(exr)
        scene.render.image_settings.file_format='OPEN_EXR'; scene.render.image_settings.color_depth='32'
        print('Rendering '+name+' '+json.dumps(settings),flush=True)
        bpy.ops.render.render(write_still=True)
        if denoise:
            scene.render.image_settings.file_format='PNG'; scene.render.image_settings.color_depth='8'
            bpy.data.images['Render Result'].save_render(str(png),scene=scene)
        else:
            image=bpy.data.images.load(str(exr),check_existing=False)
            pixels=np.empty(image.size[0]*image.size[1]*4,dtype=np.float32); image.pixels.foreach_get(pixels)
            pixels=pixels.reshape(image.size[1],image.size[0],4)[::-1]
            if not np.isfinite(pixels).all() or pixels[...,:3].min() < -1e-5: raise RuntimeError('Non-finite/negative reference radiance')
            rgb=np.maximum(pixels[...,:3],0)*config['exposure']; rgb=rgb/(1+rgb)
            rgb=np.where(rgb<=.0031308,12.92*rgb,1.055*np.power(rgb,1/2.4)-.055)
            write_png(png,np.dstack((np.rint(np.clip(rgb,0,1)*255).astype(np.uint8),np.full(rgb.shape[:2],255,np.uint8))))
            bpy.data.images.remove(image)
        report['shots'][name]={**settings,'fingerprint':fingerprint,'seconds':round(time.monotonic()-start,2),
            'exr':exr.name,'png':png.name,'exrSha256':sha(exr),'pngSha256':sha(png)}
        json_write(report_path,report)
        print('Finished '+name,flush=True)
    if sha(source/'Workshop.blend')!=original: raise RuntimeError('Authoring source changed')

if __name__=='__main__': main()
