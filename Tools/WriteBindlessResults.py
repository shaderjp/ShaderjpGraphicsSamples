"""Fill Japanese comparison tables from the completed measured-run summary."""
from pathlib import Path
import json
ROOT=Path(__file__).resolve().parents[1]
def main():
    data=json.loads((ROOT/'Docs/BindlessPerformance.json').read_text())
    if len(data)!=46 or any(v['runs']!=5 for v in data.values()):raise ValueError('Expected 46 conditions x 5 runs')
    reference=any(v.get('measurementStatus')=='reference-only' for v in data.values())
    def get(api,n,b,driven=False,o=512,m=64):return data[f"{api}-A{n}-{o}-{m}-{b}-{'driven' if driven else 'direct'}"]
    out=[('**以下は並行GPU試験・ビルドと重なった初回参考値です。速度差の結論には使用しません。**\n\n' if reference else '')+'測定は46条件×5反復＝230回、計276,000測定フレームです。数値は各反復の中央値をさらに中央値で集計しています。p95も各反復のp95の中央値です。反復間の最小・最大はJSONに残しています。',
         '\n### 参照方式の比較（512配置・64材質・208画像）\n',
         '| API | 発展 | 方式 | CPU記録 ms | GPU合計 ms | GPU p95 ms | Descriptor bind | Scene Buffer |',
         '|---|---:|---|---:|---:|---:|---:|---:|']
    for api in ['Direct3D12','Vulkan']:
        for n in range(12,16):
            for binding in ['bound','textures']+(['resources'] if n!=12 else []):
                d=get(api,n,binding)
                # Buffer resource count follows the actual constructed binding path, not descriptor views.
                buffers=d['sceneBufferResources']
                out.append(f"| {api} | {n} | {binding} | {d['cpuRecordMedianMs']:.4f} | {d['gpuMedianMs']:.4f} | {d['gpuP95MedianMs']:.4f} | {d['descriptorBinds']} | {buffers} |")
    out+=['\n### 描画集約の比較（同じバインド方式内）\n',
          '| API | 発展 | 方式 | 直接→集約 Dispatch | CPU記録 ms（直接→集約） | GPU合計 ms（直接→集約） |',
          '|---|---:|---|---|---:|---:|']
    for api in ['Direct3D12','Vulkan']:
        for n in [13,15]:
            for binding in ['textures','resources']:
                a=get(api,n,binding);b=get(api,n,binding,True)
                out.append(f"| {api} | {n} | {binding} | {a['dispatches']} → {b['dispatches']} | {a['cpuRecordMedianMs']:.4f} → {b['cpuRecordMedianMs']:.4f} | {a['gpuMedianMs']:.4f} → {b['gpuMedianMs']:.4f} |")
    out+=['\n![配置数を変えた実測](Assets/Images/BindlessPerformance/objects.png)',
          '\n![材質数を変えた実測](Assets/Images/BindlessPerformance/materials.png)',
          '\n![参照方式と描画集約の実測](Assets/Images/BindlessPerformance/techniques.png)',
          '\n### 結果を読む際の注意\n']
    a=get('Direct3D12',13,'resources');b=get('Direct3D12',13,'resources',True)
    if reference:
        out.append(f"Direct3D12の13／resourcesでは、直接描画のDispatch {a['dispatches']}回がGPU Drivenで{b['dispatches']}回になります。GPU DrivenにはComputeカリング・可視リストへの圧縮・間接引数生成も含まれます。CPU／GPUの時間差は、外部負荷を除いた再測定後に評価します。")
    else:
        out.append(f"Direct3D12の13／resourcesでは、Dispatchは{a['dispatches']}→{b['dispatches']}回、CPU記録は{a['cpuRecordMedianMs']:.4f}→{b['cpuRecordMedianMs']:.4f} ms、GPU合計は{a['gpuMedianMs']:.4f}→{b['gpuMedianMs']:.4f} msでした。GPU DrivenにはComputeカリング・可視リストへの圧縮・間接引数生成も含まれます。Dispatch数の減少だけをGPU高速化と解釈しません。")
        a=get('Vulkan',13,'resources');b=get('Vulkan',13,'resources',True)
        out.append(f"\nVulkanの13／resourcesでは、CPU記録は{a['cpuRecordMedianMs']:.4f}→{b['cpuRecordMedianMs']:.4f} ms、GPU合計は{a['gpuMedianMs']:.4f}→{b['gpuMedianMs']:.4f} msでした。Geometryパスは{a['gpu']['geometry']['medianMs']:.4f}→{b['gpu']['geometry']['medianMs']:.4f} ms、集約側のCullは{b['gpu']['cull']['medianMs']:.4f} msです。GPU Drivenでは可視リストの圧縮・追加参照とMeshletの実行順序・分配も変わります。各要因の寄与は分離していませんが、増加はGeometryパスに大きく現れています。")
        overlap=[]
        for api in ['Direct3D12','Vulkan']:
            a=get(api,12,'bound');b=get(api,12,'textures')
            out.append(f"\n{api}の12／512配置・64材質では、Descriptor bindは{a['descriptorBinds']}→{b['descriptorBinds']}回、CPU記録は{a['cpuRecordMedianMs']:.4f}→{b['cpuRecordMedianMs']:.4f} ms、GPU合計は{a['gpuMedianMs']:.4f}→{b['gpuMedianMs']:.4f} msでした。")
            overlap.extend(max(a[k][0],b[k][0])<=min(a[k][1],b[k][1]) for k in ['cpuRecordRunRangeMs','gpuRunRangeMs'])
        if all(overlap):out.append('\n発展12のこの条件では、両APIともCPU記録・GPU合計の反復間の最小〜最大が方式間で重なります。バインド回数の削減は確認できますが、この結果だけで安定した処理時間の短縮を断定しません。')
    out.append('\n従来方式も材質順に並べ、Descriptorをキャッシュしています。12の64材質では配置数を増やしても材質バインド回数は一定です。配置数を増やす実験と材質数を増やす実験を分けることで、Draw件数とリソース切替の違いを観察できます。')
    out.append('\n14のCPU記録には、CPUからヒットごとに材質をバインドする処理がありません。固定Arrayと個別画像、結合Bufferと個別Bufferの配置・参照の差です。15は一次面と二次レイの両経路を含みます。API間の絶対値はコンパイラ、Descriptorの組み方、Presentの経路も異なるため、同じAPI・同じ描画経路内の差を読みます。')
    out.append('\nShaderとDescriptorは事前作成しています。起動時のWIC／mip処理・転送・AS構築は定常時間と分離してJSONへ保存しました。資源を細かく分けると確保時のアライメント余白も増えるため、payload容量だけでなくcommitted容量とDescriptor個数も確認してください。小差は反復間の幅と合わせて判断します。')
    p=ROOT/'Docs/BindlessVerification.md';s=p.read_text(encoding='utf-8');marker='<!-- GENERATED_PERFORMANCE -->';s=s.split(marker)[0]+marker+'\n\n'+'\n'.join(out)+'\n';p.write_text(s,encoding='utf-8')
    print('Wrote measured comparison tables')
if __name__=='__main__':main()
