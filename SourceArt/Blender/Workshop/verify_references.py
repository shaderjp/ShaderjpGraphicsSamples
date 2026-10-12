"""Inspect all distributed EXR/PNG references through Blender's image reader. MIT License."""
from pathlib import Path
import hashlib
import json
import sys
import bpy
import numpy as np
HERE=Path(__file__).resolve().parent
ROOT=HERE.parents[2]
sys.path.insert(0,str(HERE))
from render_references import SHOTS
from validate import png, check

def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()

def main():
    directory=ROOT/'Docs'/'Assets'/'Images'/'Workshop'
    report=json.loads((directory/'RenderSettings.json').read_text(encoding='utf-8'))
    check(report['sourceSha256']==sha(HERE/'Workshop.blend'),'Reference source hash')
    check(report['environmentSha256']==sha(HERE/'WorkshopSky.exr'),'Reference environment hash')
    check(set(report['shots'])=={s[0] for s in SHOTS},'Eight reference shots required')
    result={}
    for name,scene,camera,lighting,preset,bounces,denoise in SHOTS:
        settings=report['shots'][name]; exr=directory/settings['exr']; image_path=directory/settings['png']
        check(settings['samples']==512 and settings['seed']==0 and settings['device']=='CPU' and not settings['adaptiveSampling'],'Reference sampling')
        check(settings['scene']==scene and settings['lighting']==lighting and settings['preset']==preset and settings['maxBounces']==bounces and settings['denoise']==denoise,'Reference condition')
        check(settings['width']==1280 and settings['height']==720,'Reference recorded size')
        check(settings['exrSha256']==sha(exr) and settings['pngSha256']==sha(image_path),'Reference hash')
        image=bpy.data.images.load(str(exr),check_existing=False)
        check(tuple(image.size)==(1280,720),'EXR size')
        pixels=np.empty(1280*720*4,np.float32); image.pixels.foreach_get(pixels)
        pixels=pixels.reshape(720,1280,4)[::-1]
        check(np.isfinite(pixels).all() and pixels[...,:3].min()>=-1e-5,'EXR finite nonnegative RGB')
        display=png(image_path); check(display.shape==(720,1280,4) and np.all(display[...,3]==255),'PNG size/alpha')
        if not denoise:
            # Validate the saved linear->display conversion independently using
            # a log-space form of the power transfer, including its linear toe.
            radiance=np.maximum(pixels[...,:3].astype(np.float64),0)*settings['exposure']
            mapped=1-1/(1+radiance)
            encoded=np.empty_like(mapped); toe=mapped<=.0031308
            encoded[toe]=mapped[toe]*12.92
            encoded[~toe]=1.055*np.exp(np.log(mapped[~toe])/2.4)-.055
            expected=np.rint(np.clip(encoded,0,1)*255)
            error=int(np.max(abs(expected-display[...,:3].astype(float))))
            check(error<=1,'Reinhard/sRGB serialization')
        else: error=None
        result[name]={'linearMin':float(pixels[...,:3].min()),'linearMax':float(pixels[...,:3].max()),
            'displayMaxByteError':error,'finite':True,'dimensions':[1280,720]}
        bpy.data.images.remove(image)
    check(result['Hero_Combined']['linearMax']>1,'Scene HDR retained')
    # Light toggles must produce a real change under an identical camera.
    direct=png(directory/report['shots']['Hero_Direct']['png'])
    env=png(directory/report['shots']['Hero_Environment']['png'])
    check(np.count_nonzero(np.max(abs(direct[...,:3].astype(int)-env[...,:3].astype(int)),axis=-1)>2)>10000,'Lighting comparison has no visible difference')
    print(json.dumps(result,indent=2))
    print('Workshop reference EXR/PNG dimensions, conditions, HDR, finite values and display conversion PASS')

if __name__=='__main__': main()
