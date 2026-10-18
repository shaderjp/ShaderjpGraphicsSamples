"""Independent invalid-data checks for the saved Gallery export. MIT License."""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import tempfile

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('gallery_validation',ROOT/'SourceArt/Blender/BindlessGallery/validate.py')
validation=importlib.util.module_from_spec(spec); spec.loader.exec_module(validation)

def main():
    with tempfile.TemporaryDirectory(prefix='gallery-negative-',dir=ROOT/'.cache') as temporary:
        assets=Path(temporary)/'Assets'
        for group in ('Models','Scenes'):
            shutil.copytree(ROOT/'Assets'/group/'BindlessGallery',assets/group/'BindlessGallery')
        path=assets/'Models/BindlessGallery/BindlessGallery.gltf'
        original=json.loads(path.read_text(encoding='utf-8'))
        def expect(label,edit,fragment):
            data=copy.deepcopy(original); edit(data); path.write_text(json.dumps(data),encoding='utf-8')
            try: validation.validate(assets)
            except ValueError as error:
                if fragment not in str(error): raise RuntimeError(label+': '+str(error))
            else: raise RuntimeError('Accepted '+label)
            print('Rejected '+label)
        expect('duplicate ID',lambda d:d['nodes'][1]['extras'].update(id=d['nodes'][0]['extras']['id']),'Duplicate stable ID')
        expect('wrong preset',lambda d:d['scenes'][0]['nodes'].pop(0),'Preset object/material count')
        expect('mirrored node',lambda d:d['nodes'][0].update(scale=[-1,1,1]),'Positive uniform scale')
        expect('geometry alias',lambda d:d['meshes'][0]['extras'].update(geometry=63),'Material variants duplicated geometry payload')
        expect('MASK cutoff',lambda d:next(m for m in d['materials'] if m['alphaMode']=='MASK').update(alphaCutoff=.2),'MASK policy')
        expect('unsupported alpha',lambda d:d['materials'][0].update(alphaMode='BLEND'),'Alpha modes')
        def collapse(d):
            for n in d['nodes']:
                if 'mesh' in n:n['translation']=[0,0,0]
        expect('unevaluated transforms',collapse,'Scene placements collapsed')
    print('Seven BindlessGallery invalid-data fixtures PASS')

if __name__=='__main__': main()
