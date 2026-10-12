"""Render the saved sky and reuse LightingLab's fixed CPU IBL integrator. MIT License."""
import argparse
from pathlib import Path
import sys
import bpy
import numpy as np
HERE=Path(__file__).resolve().parent
sys.path.insert(0,str(HERE)); sys.path.insert(0,str(HERE.parent/'LightingLab'))
from workshop_tools import ROOT, arguments
from ibl import bake_environment, bake_lut, write_dds

def main():
    p=argparse.ArgumentParser(); p.add_argument('--source-dir',type=Path,default=HERE)
    p.add_argument('--output-dir',type=Path,default=ROOT/'Assets')
    p.add_argument('--reuse-exr',action='store_true'); p.add_argument('--force-lut',action='store_true')
    args=arguments(p); source=args.source_dir.resolve(); exr=source/'WorkshopSky.exr'
    if not args.reuse_exr:
        bpy.ops.wm.open_mainfile(filepath=str(source/'WorkshopSky.blend'))
        scene=bpy.context.scene; scene.render.image_settings.file_format='OPEN_EXR'
        scene.render.image_settings.color_depth='32'; scene.render.filepath=str(exr)
        bpy.ops.render.render(write_still=True)
    image=bpy.data.images.load(str(exr),check_existing=False)
    pixels=np.empty(image.size[0]*image.size[1]*4,dtype=np.float32); image.pixels.foreach_get(pixels)
    pixels=pixels.reshape(image.size[1],image.size[0],4)[::-1].copy()
    if pixels.shape!=(1024,2048,4) or not np.isfinite(pixels).all() or pixels[...,:3].max()<=1:
        raise RuntimeError('Expected finite 2048x1024 scene-linear HDR')
    out=args.output_dir.resolve()/'Environments'/'Workshop'
    bake_environment(pixels,out,'WorkshopSky',samples=1024)
    if args.force_lut or not (out/'BRDF.dds').exists(): write_dds(out/'BRDF.dds',[[bake_lut()]],False)

if __name__=='__main__': main()
