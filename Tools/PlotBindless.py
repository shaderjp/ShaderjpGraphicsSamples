"""Static publication figures. Requires matplotlib 3.10.7 (development tool only)."""
from pathlib import Path
import json
import sys
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
D=json.loads((ROOT/'Docs/BindlessPerformance.json').read_text());OUT=ROOT/'Docs/Assets/Images/BindlessPerformance';OUT.mkdir(parents=True,exist_ok=True)
REFERENCE=any(v.get('measurementStatus')=='reference-only' for v in D.values())
def save(fig,name):
    if REFERENCE:
        fig.text(.5,.5,'REFERENCE ONLY\nConcurrent GPU / build activity',ha='center',va='center',fontsize=28,color='#a51c30',alpha=.32,rotation=15)
    fig.savefig(OUT/name,dpi=150);plt.close(fig)
plt.rcParams.update({'font.size':10,'axes.grid':True,'grid.alpha':.22,'figure.facecolor':'white'})
COLORS=['#666f80','#197bbd','#cf7024','#4c9e6b','#9959ad']
def get(api,n,o,m,b,driven=False):
    return D[f"{api}-A{n}-{o}-{m}-{b}-{'driven' if driven else 'direct'}"]
for axis in ['objects','materials']:
    fig,axs=plt.subplots(2,3,figsize=(14,7),layout='constrained')
    values=[128,512,4096] if axis=='objects' else [16,64,256]
    for row,api in enumerate(['Direct3D12','Vulkan']):
        for col,(metric,label) in enumerate([('cpuRecordMedianMs','CPU command recording (ms)'),('gpuMedianMs','GPU total (ms)'),('descriptorBinds','Descriptor binds / frame')]):
            ax=axs[row,col]
            for mode,c in zip(['bound','textures'],COLORS):
                rows=[get(api,12,v if axis=='objects' else 512,64 if axis=='objects' else v,mode) for v in values]
                ys=np.array([d[metric] for d in rows])
                if metric=='descriptorBinds':
                    ax.plot(values,ys,marker='o',color=c,label=mode)
                else:
                    bounds=np.array([d['cpuRecordRunRangeMs' if metric=='cpuRecordMedianMs' else 'gpuRunRangeMs'] for d in rows])
                    ax.errorbar(values,ys,yerr=np.array([ys-bounds[:,0],bounds[:,1]-ys]),marker='o',capsize=3,color=c,label=mode)
            ax.set(xscale='log',xticks=values,title=f'{api}: {label}',xlabel=axis,ylim=(0,None));ax.set_xticklabels(values);ax.legend()
    fig.suptitle(f'Advanced12 / {axis} sweep / median of 5 run medians\nRTX 4070 / 1280 x 720 / 1200 frames after 60 warmup / whiskers: min-max of run medians')
    save(fig,f'{axis}.png')
fig,axs=plt.subplots(2,2,figsize=(13,8),layout='constrained')
for row,api in enumerate(['Direct3D12','Vulkan']):
    for col,(metric,label) in enumerate([('cpuRecordMedianMs','CPU command recording (ms)'),('gpuMedianMs','GPU total (ms)')]):
        ax=axs[row,col];x=np.arange(4);variants=[('bound',False),('textures',False),('resources',False),('textures',True),('resources',True)];width=.16
        for i,(mode,driven) in enumerate(variants):
            rows=[get(api,n,512,64,mode,driven) if not(driven and n in [12,14]) and not(n==12 and mode=='resources') else None for n in [12,13,14,15]]
            heights=np.array([d[metric] if d else np.nan for d in rows])
            bounds=np.array([d['cpuRecordRunRangeMs' if metric=='cpuRecordMedianMs' else 'gpuRunRangeMs'] if d else [np.nan,np.nan] for d in rows])
            ax.bar(x+(i-2)*width,heights,width,yerr=np.array([heights-bounds[:,0],bounds[:,1]-heights]),capsize=2,error_kw={'elinewidth':.8},color=COLORS[i],label=mode+(' + GPU driven' if driven else ''))
        ax.set(xticks=x,xticklabels=['A12','A13','A14','A15'],title=f'{api}: {label}',ylim=(0,None))
handles,labels=axs[0,0].get_legend_handles_labels()
fig.legend(handles,labels,loc='outside lower center',ncol=5,fontsize=9)
fig.suptitle('Reference access and submission architecture are separate comparisons\n512 objects / 64 materials / 208 images / median of 5 run medians / whiskers: min-max')
save(fig,'techniques.png')
print('Wrote 3 real-measurement figures:',OUT)
