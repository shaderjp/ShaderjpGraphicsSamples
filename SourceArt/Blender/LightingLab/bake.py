"""Render saved environment sources to EXR and bake offline DDS IBL. MIT License."""
from pathlib import Path
import argparse
import hashlib
import json
import sys
import bpy
import numpy as np
HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0,str(HERE))
from ibl import bake_environment, bake_lut, write_dds, rgba, make_orientation

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--environment',choices=['Studio','Room'])
    parser.add_argument('--reuse-exr',action='store_true')
    parser.add_argument('--force-lut',action='store_true')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    out = ROOT/'Assets'/'Environments'/'LightingLab'
    out.mkdir(parents=True,exist_ok=True)
    make_orientation(out/'Orientation.dds')
    for name in ([args.environment] if args.environment else ['Studio','Room']):
        exr = HERE/f'{name}.exr'
        if not args.reuse_exr:
            bpy.ops.wm.open_mainfile(filepath=str(HERE/f'{name}.blend'))
            bpy.context.scene.render.image_settings.file_format = 'OPEN_EXR'
            bpy.context.scene.render.image_settings.color_depth = '32'
            bpy.context.scene.render.filepath = str(exr)
            bpy.ops.render.render(write_still=True)
        image = bpy.data.images.load(str(exr),check_existing=False)
        pixels = np.empty(image.size[0]*image.size[1]*4,dtype=np.float32)
        image.pixels.foreach_get(pixels)
        pixels = pixels.reshape(image.size[1],image.size[0],4)[::-1].copy()
        bake_environment(pixels,out,name)
    if args.force_lut or not (out/'BRDF.dds').exists():
        print('Integrating height-correlated Smith GGX BRDF LUT',flush=True)
        write_dds(out/'BRDF.dds',[[bake_lut()]],False)

if __name__ == '__main__':
    main()
