"""End-to-end depth experiments. Expected values use independent NumPy formulae."""
from pathlib import Path
import argparse,json,os,subprocess
import numpy as np
import struct,zlib

def read_png(path):
    data=path.read_bytes();offset=8;compressed=bytearray()
    assert data[:8]==b'\x89PNG\r\n\x1a\n'
    while offset<len(data):
        count=struct.unpack_from('>I',data,offset)[0];kind=data[offset+4:offset+8];body=data[offset+8:offset+8+count]
        if kind==b'IHDR':width,height,bits,color,_,_,interlace=struct.unpack('>IIBBBBB',body);assert bits==8 and color in (2,6) and interlace==0
        if kind==b'IDAT':compressed.extend(body)
        offset+=count+12
    channels=4 if color==6 else 3;stride=width*channels;filtered=zlib.decompress(compressed);output=bytearray();previous=bytearray(stride)
    for y in range(height):
        start=y*(stride+1);mode=filtered[start];row=bytearray(filtered[start+1:start+1+stride])
        for x in range(stride):
            left=row[x-channels] if x>=channels else 0;up=previous[x];diagonal=previous[x-channels] if x>=channels else 0
            if mode==0:predictor=0
            elif mode==1:predictor=left
            elif mode==2:predictor=up
            elif mode==3:predictor=(left+up)//2
            else:
                p=left+up-diagonal;dl,du,dd=abs(p-left),abs(p-up),abs(p-diagonal);predictor=left if dl<=du and dl<=dd else up if du<=dd else diagonal
            row[x]=(row[x]+predictor)&255
        output.extend(row);previous=row
    return np.frombuffer(output,np.uint8).reshape(height,width,channels)[:,:,:3]


ROOT=Path(__file__).resolve().parents[1]
parser=argparse.ArgumentParser()
parser.add_argument('--configuration',choices=['Debug','Release'],default='Debug')
parser.add_argument('--api',choices=['Direct3D12','Vulkan'])
parser.add_argument('--capture-docs',action='store_true')
parser.add_argument('--quick',action='store_true')
args=parser.parse_args()
OUT=ROOT/'.cache/depth-gpu'/args.configuration
OUT.mkdir(parents=True,exist_ok=True)
results=[]
def run(api,topic,label,options=(),ui=False,fail=False,docs=False):
    name=api+'_'+topic;prefix=OUT/(name+'-'+label)
    image=ROOT/f'Docs/{api}/Images/{topic}-{label}.png' if docs else prefix.with_suffix('.png')
    log=prefix.with_suffix('.log')
    exe=ROOT/f'bin/x64/{args.configuration}/{name}/{name}.exe'
    command=[str(exe),'--frames','46' if ui else '3','--log',str(log),'--screenshot',str(image)]
    if not docs and not ui:command+=['--width','960','--height','540']
    if not docs:command+=['--capture-gpu']
    if ui:command+=['--test-ui']
    command+=list(options)
    info=subprocess.STARTUPINFO();info.dwFlags|=subprocess.STARTF_USESHOWWINDOW;info.wShowWindow=0
    env=os.environ.copy()
    if args.configuration=='Debug':env['VK_LAYER_VALIDATE_SYNC']='1'
    proc=subprocess.run(command,cwd=OUT,env=env,startupinfo=info,capture_output=True,timeout=45)
    text=log.read_text(encoding='utf-8',errors='replace')
    if fail:
        assert proc.returncode!=0 and 'ERROR:' in text,(label,text)
        assert 'Validation errors: 0' in text,(label,text)
        results.append(dict(api=api,case=label,rejected=True));return
    assert proc.returncode==0,(label,text,proc.stderr)
    assert 'Validation errors: 0' in text,(label,text)
    assert 'warning:' not in text.lower(),(label,text)
    if args.configuration=='Debug':assert 'Layer enabled' in text
    if docs:
        assert struct.unpack('>II',image.read_bytes()[16:24])==(1920,1080)
        results.append(dict(api=api,case=label,documentation=True))
        return
    directory=image.parent/(image.stem+'-gpu')
    metadata=json.loads((directory/'metadata.json').read_text())
    data=[np.fromfile(directory/f,dtype='<f4').reshape(metadata['height'],metadata['width'],4) for f in ['standard.bin','reversed.bin']]
    for d in data:
        assert np.isfinite(d).all(),label
        assert (d[...,3]>0).sum()>100,label
        assert ((d[...,0]>=0)&(d[...,0]<=1)).all(),label
    result=dict(api=api,case=label,coverage=int((data[0][...,3]>0).sum()))
    results.append(result)
    if ui:
        assert 'Depth UI edited' in text and 'Depth UI reset' in text and 'Depth probe selected' in text,text
        pictures=[read_png(image.parent/(image.stem+'-'+suffix+'.png')) for suffix in ['baseline','edited','reset']]
        # Compare scene + raw/linear depth rectangles, excluding text, graphs and UI hover.
        base,edited,reset=[p[112:775,370:1900,:3] for p in pictures]
        assert np.count_nonzero(base!=edited)>100,'UI edit did not affect GPU scene/depth pixels'
        assert np.array_equal(base,reset),'Reset did not reproduce GPU baseline'
        result['ui']=True
    return metadata,data,result,directory

for api in ([args.api] if args.api else ['Direct3D12','Vulkan']):
    for preset in (['mapping','precision','orthographic','infinite'] if not args.quick else ['mapping','precision']):
        formats=['d16','d24','d32'] if preset=='precision' and not args.quick else ['d32']
        for fmt in formats:
            label=preset+'-'+fmt
            m,data,r,_=run(api,'35_DepthPrecision',label,['--depth-preset',preset,'--depth-format',fmt])
            n,f=m['near'],m['far']
            for panel,d in enumerate(data):
                valid=d[...,3]>0;z=d[...,1][valid].astype(np.float64)
                if m['orthographic']:expected=(f-z)/(f-n) if panel else (z-n)/(f-n)
                elif m['infinite']:expected=n/z if panel else 1-n/z
                else:expected=n*(f-z)/((f-n)*z) if panel else f*(z-n)/((f-n)*z)
                error=np.max(np.abs(d[...,0][valid]-expected))
                limit={'d16':2/65535,'d24':4/16777215,'d32':4e-7}[fmt]
                assert error<limit,(api,label,panel,error)
                r[f'max_depth_error_{panel}']=float(error)
            if preset=='precision':
                centre=tuple(x//2 for x in data[0].shape[:2])
                ids=[int(d[centre][2]) for d in data]
                inside=data[1][...,2]==11
                if fmt=='d32':
                    assert ids[1]==11,(api,label,ids)
                    wrong=np.mean(data[0][...,2][inside]!=11)
                    assert wrong>.01,(api,label,wrong)
                    r['standard_wrong_near_fraction']=float(wrong)
                r['centre_ids']=ids
    for light in (['directional','spot'] if not args.quick else ['spot']):
        m,data,r,directory=run(api,'36_ShadowDepth',light,['--depth-preset',light])
        sm=np.fromfile(directory/'shadow.bin',dtype='<f4').reshape(m['resolution'],m['resolution'],4)
        valid=sm[...,3]>0
        assert valid.sum()>1000
        n,f=m['near'],m['far'];linear=sm[...,1][valid].astype(np.float64)
        z=n+linear*(f-n)
        expected=f*(z-n)/((f-n)*z) if m['spot'] else linear
        error=np.max(np.abs(sm[...,0][valid]-expected))
        assert error<5e-7,(api,light,error)
        r['shadow_depth_error']=float(error)
        if m['spot']:assert np.mean(abs(sm[...,0][valid]-linear))>.1
        # The color-independent camera depth and visible geometry remain identical.
        assert np.array_equal(data[0],data[1]),'Shadow encoding changed geometry visibility'
        # Render the same encoding in both panels; comparisons are performed per tap.
        images=[]
        for encoding in ['projected','linear']:
            _,_,_,di=run(api,'36_ShadowDepth',light+'-'+encoding,
                ['--depth-preset',light,'--depth-encoding',encoding,'--depth-pcf','off'])
            pic=read_png(di.parent/(di.name[:-4]+'.png'))[56:231,185:562,:3]
            images.append(pic)
        difference=np.count_nonzero(np.max(abs(images[0].astype(float)-images[1]),axis=2)>2)
        assert difference/images[0].shape[0]/images[0].shape[1]<.002,(api,light,difference)
        r['shadow_encoding_different_pixels']=int(difference)
        filtered=read_png(directory.parent/(directory.name[:-4]+'.png'))[56:231,185:562,:3]
        changed=np.count_nonzero(np.max(abs(filtered.astype(float)-images[0]),axis=2)>2)
        assert changed>20,(api,light,'PCF did not change the GPU shadow edge',changed)
        r['pcf_changed_pixels']=int(changed)
    if not args.quick:
        m,_,r,di=run(api,'36_ShadowDepth','low-resolution',['--depth-preset','resolution'])
        assert m['resolution']==256
        assert (di/'shadow.bin').stat().st_size==256*256*16
        for topic in ['35_DepthPrecision','36_ShadowDepth']:
            run(api,topic,'ui',ui=True)
        for label,options in [('near-zero',['--depth-near','0']),('nan',['--depth-near','nan']),
            ('equal-range',['--depth-near','1','--depth-far','1']),
            ('ortho-infinite',['--depth-projection','orthographic','--depth-infinite','on']),
            ('unknown',['--depth-format','d12'])]:
            run(api,'35_DepthPrecision',label,options,fail=True)
        for label,options in [('fractional-resolution',['--depth-resolution','256.5']),('bad-light',['--depth-light','point'])]:
            run(api,'36_ShadowDepth',label,options,fail=True)
    if args.capture_docs:
        for preset in ['mapping','near','precision','infinite','orthographic']:
            run(api,'35_DepthPrecision',preset,['--depth-preset',preset],docs=True)
        for graph in ['spacing','toy']:
            run(api,'35_DepthPrecision',graph,['--depth-preset','precision','--depth-graph',graph],docs=True)
        for preset in ['directional','spot','bias','resolution']:
            run(api,'36_ShadowDepth',preset,['--depth-preset',preset],docs=True)
    print(api+' depth GPU tests passed',flush=True)
(OUT/('results'+('-'+args.api if args.api else '')+'.json')).write_text(json.dumps(results,indent=2))
