import argparse
import json
import math
from pathlib import Path
import statistics


def main():
    p=argparse.ArgumentParser();p.add_argument('directory',type=Path);p.add_argument('--warmup',type=int,default=120)
    p.add_argument('--frames',type=int,default=600);p.add_argument('--repeats',type=int,default=5);a=p.parse_args()
    groups={}
    workloads={}
    for file in sorted(a.directory.glob('[0-9][0-9]-*.json')):
        data=json.loads(file.read_text());key=file.stem.rsplit('-',1)[0]
        if int(file.stem.rsplit('-',1)[1])>a.repeats:continue
        assert len(data['frames']) == a.warmup+a.frames
        assert abs(data['workload']['fixedDelta']-1/60)<1e-8
        assert abs(data['workload']['animationStart']-.7)<1e-6
        assert data['workload']['scene']==0 and data['workload']['activePercent']==1
        signature={k:v for k,v in data['workload'].items() if k!='partition'}
        signature['animationTimes']=[f['time'] for f in data['frames']]
        assert signature == workloads.setdefault(data['technique'],signature),'Comparison workloads differ'
        frames=data['frames'][a.warmup:a.warmup+a.frames]
        assert len(frames)==a.frames
        groups.setdefault(key,[]).append((file.name,data,frames))
    result={'warmup':a.warmup,'framesPerRun':a.frames,'repeats':a.repeats,'resolution':[1920,1080],
            'fixedDelta':1/60,'startTime':.7,'cases':{}}
    rows=['| Sample / AS | Vertex / instance ms | CLAS ms | BLAS ms | TLAS ms | Trace ms | AS MiB | Scratch MiB | Input MiB |',
          '|---|---:|---:|---:|---:|---:|---:|---:|---:|']
    for key,runs in sorted(groups.items()):
        assert len(runs)==a.repeats,(key,len(runs))
        stages={}
        for stage in ['vertices_ms','clas_ms','blas_ms','top_ms','trace_ms','display_ms']:
            medians=[statistics.median(f[stage] for f in frames) for _,_,frames in runs]
            values=sorted(f[stage] for _,_,frames in runs for f in frames)
            assert all(math.isfinite(v) and v>=0 for v in values)
            stages[stage]={'median':statistics.median(medians),'p95':values[min(len(values)-1,int(len(values)*.95))], 'runMedians':medians}
        memory=runs[0][1]['memory'];assert all(d['width']==1920 and d['height']==1080 for _,d,_ in runs)
        assert all(d['memory']==memory for _,d,_ in runs)
        result['cases'][key]={'stages':stages,'memory':memory,'sources':[n for n,_,_ in runs]}
        sizes=[(memory['clas']+memory['blas']+memory['templates']+memory['top'])/1048576,
               memory['scratch']/1048576,memory['source']/1048576]
        rows.append('| '+key+' | '+' | '.join(f'{stages[s]["median"]:.3f}' for s in ['vertices_ms','clas_ms','blas_ms','top_ms','trace_ms'])+' | '+' | '.join(f'{v:.2f}' for v in sizes)+' |')
    (a.directory/'Summary.json').write_text(json.dumps(result,indent=2)+'\n')
    (a.directory/'Summary.md').write_text(f'# Geometry Lab Release measurements\n\n1920×1080、固定時間刻み1/60秒、{a.warmup}フレームのウォームアップ後{a.frames}フレーム×{a.repeats}回。値は各試行の中央値の中央値です。p95と各試行値はSummary.jsonに保存します。\n\n'+'\n'.join(rows)+'\n',encoding='utf-8')
    print('Summarized',len(groups),'matched-workload cases')


if __name__=='__main__':main()
