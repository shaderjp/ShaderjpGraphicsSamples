"""Negative asset-profile fixtures; does not implement the future runtime loader."""
from pathlib import Path
import copy
import json
import shutil
import sys
import uuid
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'SourceArt'/'Blender'/'Workshop'))
from validate import model_report

def main():
    original=ROOT/'Assets'
    directory=ROOT/'.cache'/('workshop-negative-'+uuid.uuid4().hex)
    directory.mkdir(parents=True)
    # Keep fixtures for diagnosis. Every write is confined to this fresh cache root.
    if not directory.resolve().is_relative_to((ROOT/'.cache').resolve()): raise RuntimeError('Fixture path')
    shutil.copytree(original/'Models'/'Workshop',directory/'Models'/'Workshop')
    shutil.copytree(original/'Scenes'/'Workshop',directory/'Scenes'/'Workshop')
    path=directory/'Models'/'Workshop'/'Workshop.gltf'
    document=json.loads(path.read_text(encoding='utf-8'))
    binary=directory/'Models'/'Workshop'/'Workshop.bin'; saved_binary=binary.read_bytes()
    def expect(label,edit,fragment):
        data=copy.deepcopy(document); payload=bytearray(saved_binary); edit(data,payload)
        path.write_text(json.dumps(data),encoding='utf-8'); binary.write_bytes(payload)
        try: model_report(directory)
        except ValueError as error:
            if fragment not in str(error): raise RuntimeError(label+': wrong rejection '+str(error))
        else: raise RuntimeError(label+': accepted invalid fixture')
        print('Rejected '+label)
    expect('duplicate stable ID',lambda d,b:d['nodes'][1]['extras'].update(id=d['nodes'][0]['extras']['id']),'Duplicate stable ID')
    expect('scene selection',lambda d,b:d['scenes'][0].update(name='Unknown'),'Two ordered scenes')
    def alpha(d,b):
        next(m for m in d['materials'] if m['alphaMode']=='MASK')['alphaCutoff']=.7
    expect('MASK cutoff drift',alpha,'MASK policy')
    def degenerate(d,b):
        p=d['meshes'][0]['primitives'][0]; a=d['accessors'][p['indices']]; v=d['bufferViews'][a['bufferView']]
        dtype={5123:'<u2',5125:'<u4'}[a['componentType']]
        indices=np.frombuffer(b,dtype=dtype,count=a['count'],offset=v.get('byteOffset',0)+a.get('byteOffset',0))
        indices[1]=indices[0]
    expect('degenerate triangle',degenerate,'Degenerate triangles')
    def negative_scale(d,b):
        next(n for n in d['nodes'] if 'mesh' in n)['scale']=[-1,1,1]
    expect('mirrored placement',negative_scale,'Positive uniform scale')
    def cycle(d,b):
        root=d['scenes'][0]['nodes'][0]; d['nodes'][root].setdefault('children',[]).append(root)
    expect('node hierarchy cycle',cycle,'Cycle/multiple parent')
    print('Workshop six invalid asset fixtures PASS')

if __name__=='__main__': main()
