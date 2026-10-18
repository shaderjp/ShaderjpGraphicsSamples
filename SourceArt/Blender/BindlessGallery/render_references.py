"""Cycles reference images from the saved source; never saves the blend."""
import argparse
from pathlib import Path
import sys
import time
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from gallery_tools import *

SHOTS=[('Hero','Standard','StandardHero'),('Materials','Standard','Materials'),
       ('Reflection','Standard','Reflection'),('Occlusion','Standard','Occlusion'),('Stress','Stress','StressHero')]
def main():
    p=argparse.ArgumentParser(); p.add_argument('--shot',choices=[s[0] for s in SHOTS]); p.add_argument('--samples',type=int,default=64)
    p.add_argument('--output-dir',type=Path,default=ROOT/'Docs'/'Assets'/'Images'/'BindlessGallery')
    args=arguments(p)
    source=Path(bpy.data.filepath)
    if source.stem!='BindlessGallery': raise RuntimeError('Open saved BindlessGallery.blend')
    original=sha(source); out=args.output_dir.resolve(); out.mkdir(parents=True,exist_ok=True)
    report={'sourceSha256':original,'blender':bpy.app.version_string,'buildHash':bpy.app.build_hash.decode(),'shots':{}}
    report_path=out/'References.json'
    if report_path.exists(): report['shots']=json.loads(report_path.read_text(encoding='utf-8'))['shots']
    for name,scene_name,cam in SHOTS:
        if args.shot and args.shot!=name: continue
        scene=bpy.data.scenes[scene_name]; bpy.context.window.scene=scene
        render_settings(scene,args.samples,True); scene.camera=bpy.data.objects[cam]
        scene.render.image_settings.file_format='PNG'; scene.render.image_settings.color_depth='8'
        scene.render.filepath=str(out/f'BindlessGallery_{name}.png')
        start=time.monotonic(); bpy.ops.render.render(write_still=True)
        report['shots'][name]={'scene':scene_name,'camera':cam,'width':1280,'height':720,'samples':args.samples,
            'device':scene.cycles.device,'seed':0,'denoise':True,'maxBounces':8,'display':'AgX Medium High Contrast',
            'seconds':time.monotonic()-start,'pngSha256':sha(scene.render.filepath)}
        json_write(report_path,report)
    if sha(source)!=original: raise RuntimeError('Rendering modified authoring source')

if __name__=='__main__': main()
