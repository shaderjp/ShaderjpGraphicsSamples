"""Collect measured data and actual GPU comparison results; never synthesize missing runs."""
from pathlib import Path
from datetime import datetime
import argparse
import gzip
import hashlib
import json
import math
import statistics
import struct

ROOT=Path(__file__).resolve().parents[1]

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--measurements',type=Path,default=ROOT/'.cache/culling-performance')
    args=parser.parse_args()
    rows=[];raw=[]
    modes=[('31_FrustumCulling','off'),('31_FrustumCulling','frustum'),('32_OcclusionQueries','depth-only'),
           ('32_OcclusionQueries','query'),('33_SoftwareOcclusion','software'),('34_HiZOcclusion','depth-only'),('34_HiZOcclusion','hiz')]
    for api in ['Direct3D12','Vulkan']:
        for camera in ['Hero','Plaza']:
            for topic,mode in modes:
                runs=[]
                for repeat in range(1,6):
                    path=args.measurements/f'{api}_{topic}-{mode}-{camera}-{repeat}.json'
                    data=json.loads(path.read_text(encoding='utf-8'))
                    assert len(data['cullingSamples'])==1200 and data['width']==1920 and data['height']==1080
                    assert data['mode']==mode and data['configuration']=='Release' and data['api']==api
                    assert data['scene']=='Standard' and data['warmupFrames']==60 and data['measuredFrames']==1200
                    columns=data['cullingSampleColumns']
                    assert len(columns)==9 and len(set(columns))==9
                    for frame in data['cullingSamples']:
                        assert len(frame)==len(columns) and all(math.isfinite(v) and v>=0 for v in frame)
                    for column,metric in enumerate(columns):
                        values=sorted(frame[column] for frame in data['cullingSamples'])
                        assert data['cullingMs'][metric]['median']==values[600]
                        assert data['cullingMs'][metric]['p95']==values[1140]
                    raw.append(dict(api=api,camera=camera,topic=topic,mode=mode,repeat=repeat,
                        columns=data['cullingSampleColumns'],frames=data['cullingSamples']))
                    runs.append(dict(repeat=repeat,metrics=data['cullingMs'],sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
                aggregate={}
                for metric in runs[0]['metrics']:
                    medians=[r['metrics'][metric]['median'] for r in runs]
                    aggregate[metric]=dict(median=statistics.median(medians),minimum=min(medians),maximum=max(medians),
                        medianP95=statistics.median(r['metrics'][metric]['p95'] for r in runs))
                rows.append(dict(api=api,camera=camera,topic=topic,mode=mode,runs=runs,metrics=aggregate))
    destination=ROOT/'Docs/Measurements/Culling';destination.mkdir(parents=True,exist_ok=True)
    (destination/'Summary.json').write_text(json.dumps(dict(date=datetime.now().isoformat(),resolution=[1920,1080],
        scene='Standard',warmupFrames=60,measuredFrames=1200,repetitions=5,rows=rows),indent=2)+'\n',encoding='utf-8')
    with gzip.open(destination/'Frames.json.gz','wt',encoding='utf-8') as stream:json.dump(raw,stream,separators=(',',':'))
    lines=['# 伝統的カリングのRelease計測','',
        '各値は5反復の「1,200フレーム中央値」の中央値、単位msです。各反復の中央値・p95・範囲は[JSON](Summary.json)、全フレームは[圧縮JSON](Frames.json.gz)を参照してください。',
        '', 'ラン内のp50・p95は既存計測器と同じく、昇順に並べた1,200値のindex 600・1140（0始まり）を採ります。表の中央値は、このp50を5反復分並べた中央の値です。',
        '', '1920×1080、Standard、各反復60フレームのウォームアップ。Heroは保存された入室経路、Plazaは上空の円弧経路です。', '']
    for api in ['Direct3D12','Vulkan']:
        for camera in ['Hero','Plaza']:
            selected=[r for r in rows if r['api']==api and r['camera']==camera]
            lines += [f'## {api} / {camera}','',
                '| サンプル / モード | CPU判定 | CPU記録 | OnRender全体 | GPU全体 | GPU反復範囲 |',
                '|---|---:|---:|---:|---:|---:|']
            for r in selected:
                values=[r['metrics'][name]['median'] for name in ['cpuCull','cpuRecord','renderWall','gpuTotal']]
                gpu=r['metrics']['gpuTotal']
                lines.append(f'| {r["topic"][:2]} / {r["mode"]} | '+' | '.join(f'{v:.3f}' for v in values)+
                             f' | {gpu["minimum"]:.3f}〜{gpu["maximum"]:.3f} |')
            lines += ['', '| サンプル / モード | Depth | Query / Resolve | Hi-Z構築 | Hi-Z判定 | 色描画 |',
                      '|---|---:|---:|---:|---:|---:|']
            for r in selected:
                values=[r['metrics'][name]['median'] for name in ['depth','queryResolve','hizBuild','hizTest','color']]
                lines.append(f'| {r["topic"][:2]} / {r["mode"]} | '+' | '.join(f'{v:.3f}' for v in values)+' |')
            lines += ['']
    lines += ['GPU各パスの区間はTimestampで測定します。未実行パスの値にはTimestamp自身の費用が残ります。CPU判定はWorld AABB・投影・必要なCPU遮蔽処理、CPU記録はCommand List／Command Bufferの記録区間です。',
        '', 'OnRender全体は定数更新・GPU完了待ち・Present・統計の読戻しを含み、ImGuiのOnUpdateや起動時ロードは含みません。フレーム毎にGPU完了を待つ教材の値であり、フレームを重ねたエンジンのスループットとは区別してください。',
        '', 'OFFでも比較用のWorld AABBと投影データを更新します。CPU判定の共通費用が残るため、OFFのCPU時間を「境界を一切作らない描画系」の値として使うことはできません。',
        '', 'Hi-Zのピラミッドは両APIともGPU専用メモリに置きます。診断用のPredicateはVulkanでHOST_VISIBLE、D3D12でREADBACKへのコピーを使います。API間の数値差をカリング原理だけの差として解釈しないでください。','']
    lines += ['電源プランはBalanced、CPU／GPUクロックは既定の動的制御です。クロックを固定した測定ではないため、反復の最小〜最大も確認してください。全168,000フレームの値が有限・非負であることと、各ランの中央値・p95が元データから再計算した値と一致することを収集時に検査しています。','']
    (destination/'Summary.md').write_text('\n'.join(lines),encoding='utf-8')
    validation={}
    for configuration in ['Debug','Release']:
        directory=ROOT/'.cache/culling-tests'/configuration
        entries=[]
        for name in ['results-Direct3D12.json','results-Vulkan.json','ui-results.json','motion-results.json','resize-results.json',
                     'results-Vulkan.hiz.json','results-Vulkan.hero-opening-near-occlusion.hiz.json']:
            path=directory/name
            if path.exists():entries += json.loads(path.read_text(encoding='utf-8'))
        validation[configuration]=entries
    (ROOT/'Docs/CullingResults.json').write_text(json.dumps(validation,indent=2)+'\n',encoding='utf-8')
    captures=[]
    for api in ['Direct3D12','Vulkan']:
        for path in sorted((ROOT/'Docs'/api/'Images').glob('3[1-4]_*.png')):
            payload=path.read_bytes();width,height=struct.unpack('>II',payload[16:24])
            assert (width,height)==(1920,1080)
            captures.append(dict(file=str(path.relative_to(ROOT)).replace('\\','/'),width=width,height=height,
                sha256=hashlib.sha256(payload).hexdigest(),source='Real GPU / Release / Tools/CaptureCulling.ps1'))
    assert len(captures)==20,'Expected eight finished views and twelve diagnostic/overview views'
    (ROOT/'Docs/CullingCaptures.json').write_text(json.dumps(captures,indent=2)+'\n',encoding='utf-8')
    # A completion report is emitted only from a complete matrix, never from an interrupted run.
    for configuration in ['Debug','Release']:
        directory=ROOT/'.cache/culling-tests'/configuration
        for api in ['Direct3D12','Vulkan']:
            comparisons=json.loads((directory/f'results-{api}.json').read_text(encoding='utf-8'))
            assert len(comparisons)==78 and all(r['missingObjects']==r['idMismatchPixels']==0 for r in comparisons)
            assert all(r['maxDepthError']<=1e-6 for r in comparisons)
        ui=json.loads((directory/'ui-results.json').read_text(encoding='utf-8'))
        motion=json.loads((directory/'motion-results.json').read_text(encoding='utf-8'))
        resize=json.loads((directory/'resize-results.json').read_text(encoding='utf-8'))
        assert len(ui)==8 and all(r['resetDifference']==0 and r['changedHdrComponents']>0 for r in ui)
        assert len(motion)==32 and all(r['missingObjects']==r['idMismatchPixels']==0 for r in motion)
        assert len(resize)==8 and all(r['idMismatchPixels']==0 and r['restored']==[1920,1080] for r in resize)
        old=(ROOT/f'.cache/culling-existing-lifecycle-{configuration.lower()}.log').read_text(encoding='utf-8-sig')
        assert old.count(' passed (')==102
        new=ROOT/('.cache/culling-new-lifecycle-debug.log' if configuration=='Debug' else '.cache/culling-captures.log')
        assert new.read_text(encoding='utf-8-sig').count(' passed (')==8
    report=['# 伝統的カリング：実機検証記録','',
        '2026-11-09〜10（日本時間）。基本31〜34のDirect3D12／Vulkan、計8アプリを実装し、以下を実機で確認しました。',
        '', '## 環境とビルド','',
        'Windows 10.0.26300.0、Core i9-13900F、GeForce RTX 4070／NVIDIA 621.01（32.0.16.2101）。VS 2026 Community 18.10.12201.205、v145／MSVC 19.51.36257.0、Windows SDK 10.0.26100.0、C++20。',
        '', 'Agility SDK 1.620.1、D3D12 DXC 1.9.2609.5（01b62ad47）、Vulkan SDK 1.4.341.1／DXC 1.9.0.5180（e35541826）、SPIRV-Tools v2026.1（fbe4f3ad）、Vulkanターゲット1.3、SM6.6／HLSL 2021。[環境JSON](CullingEnvironment.json)',
        '', 'NuGet Restore、全113プロジェクト（110 APIアプリ＋3 Support）のx64 Debug／Releaseビルド、DXIL／SPIR-Vコンパイル・spirv-val、project／filtersのXML検証を通過しました。Preview SDKを使う既存の発展20〜23の設定は維持し、新4方式は通常版SDKでビルドしています。',
        '', '## 正確性','',
        '| 検証 | 結果 |','|---|---|',
        '| GPU画像比較 | 78条件 × 2 API × 2構成＝312件。可視対象の誤除外0、ID不一致0 |',
        '| Depth | 全GPU比較でOFFとの差が1e-6以下 |',
        '| 完成HDR | Hero視点で4方式とOFFのFP16出力が完全一致 |',
        '| Hi-Z | 全段の最大値縮約とFarの余白をCPU参照と完全一致で確認 |',
        '| UI | 両構成の8アプリで扉・OFF・Reset、編集状態のCLI再現、HDRの復帰を確認 |',
        '| カメラ | 両構成で移動・急な振り返り・Resetの32ステージをOFFと比較 |',
        '| 奇数リサイズ | 両構成の8アプリで641×479→最小化→1920×1080復帰のID／Depthが一致 |',
        '| ライフサイクル | 既存102＋新8アプリの全110本をDebug／Releaseで通過（計220件） |',
        '| CPU／Loader | 6平面、投影、Nearクリップ、裏面、セル被覆、OBJ／glTF／WIC／ファイル、配置数を通過 |',
        '| 既存ImGui | 09の両API・両構成でSlider／Resetと画像の往復を通過 |',
        '| 検証レイヤー | 正常Debug実行のValidationエラー0。Vulkanは同期検証も有効 |',
        '', '[数値結果](CullingResults.json)。18の場面はHero、開いた扉のOpening、Near、Occlusion、Plaza、1279×719、Mini、Stress、経路600／1200、初回フレーム、対象移動、および完全遮蔽・部分可視・細い隙間・全可視・視錐台外・カメラ内包の独立fixtureです。Hero・奇数解像度・隙間ではCPU Depthの1/2・1/4・1/8を比較しました。',
        '', 'Near面の試験で、CPU Depthが片面材質の裏面まで描いていた問題を検出しました。GPUと同じ面の向き／両面設定を適用して修正し、全比較を再実行しました。完全遮蔽fixtureは三角形の共有辺から離して配置し、CPU参照実装が意図的に残す未被覆セルと誤除外を区別しています。',
        '', 'Vulkan 34の大きなピラミッドはDEVICE_LOCALに配置し、撮影時だけ読戻します。この変更後にReleaseの18条件とDebugの4条件を追加比較しました。後続のDebug本試験・移動・奇数リサイズ・ライフサイクルも変更後の実装で確認しています。',
        '', '## 制作データと画像','',
        'Blender 5.2.1 LTS（9e2066aef7ef）の保存済み正本から隔離して再書出しし、glTF／BIN／PNG・固定カメラ・経路をバイト比較で一致確認しました。Mini／Standard／Stressの対象数は64／512／4,096、建築物込みで86／594／4,418です。Workshopの正本は変更していません。',
        '', '実GPUの1920×1080画像20枚を保存・目視確認しました。完成表示8枚と、両APIのBounds／CPU Depth／Hi-Z／俯瞰です。[画像ハッシュと出典](CullingCaptures.json)。画像内のUI時間は撮影時の瞬間値であり、以下の単独計測の代用にはしません。',
        '', '## 性能','',
        '他の検証プロセスを終了した後、Release／1920×1080／Standardで60フレームのウォームアップ＋1,200フレームを5反復しました。2 API × 2経路 × 7モード × 5反復＝140ラン、168,000計測フレームです。',
        '', '[CPU／GPU各区間の計測表](Measurements/Culling/Summary.md)、[反復ごとの中央値・p95と範囲](Measurements/Culling/Summary.json)、[全フレームの圧縮JSON](Measurements/Culling/Frames.json.gz)。', '']
    for api in ['Direct3D12','Vulkan']:
        for camera in ['Hero','Plaza']:
            selected=[r for r in rows if r['api']==api and r['camera']==camera]
            gpu=min(selected,key=lambda r:r['metrics']['gpuTotal']['median'])
            wall=min(selected,key=lambda r:r['metrics']['renderWall']['median'])
            report.append(f'- {api}／{camera}: GPU全体の最小中央値は{gpu["topic"][:2]} {gpu["mode"]}の{gpu["metrics"]["gpuTotal"]["median"]:.3f} ms。OnRender全体では{wall["topic"][:2]} {wall["mode"]}の{wall["metrics"]["renderWall"]["median"]:.3f} msでした。')
    report += ['', '遮蔽が少ない場合とCPU費用の例：','']
    for api in ['Direct3D12','Vulkan']:
        def metric(camera,topic,mode,name):
            return next(r for r in rows if r['api']==api and r['camera']==camera and
                        r['topic'].startswith(topic) and r['mode']==mode)['metrics'][name]['median']
        plaza_depth=metric('Plaza','34','depth-only','gpuTotal')
        plaza_hiz=metric('Plaza','34','hiz','gpuTotal')
        cpu=metric('Hero','33','software','cpuCull')
        software=metric('Hero','33','software','renderWall')
        frustum=metric('Hero','31','frustum','renderWall')
        report.append(f'- {api}: Plazaの34はDepthのみが{plaza_depth:.3f} ms、Hi-Zが{plaza_hiz:.3f} ms（GPU全体）。'
                      f'HeroのSoftwareはCPU判定だけで{cpu:.3f} ms、OnRender全体は{software:.3f} msでした。'
                      f'同じHeroのFrustumはOnRender {frustum:.3f} msです。')
    report += ['', 'GPU時間とCPUを含むOnRender時間は分けて読みます。SoftwareはDrawを減らせても単一スレッドのラスタライズ費用を払います。Query／Hi-ZはCPUの色Draw記録数を削減せず、追加パスも必要です。除外数だけを性能向上の根拠にしないため、DepthのみとOFFを併記しています。',
        '', '電源プランはBalanced、CPU／GPUクロックは既定の動的制御です。上記の最小中央値は今回の測定内での値です。反復範囲が重なる場合は速度の優劣を確定せず、計測表の最小〜最大とJSONのp95も確認してください。',
        '', '## 再実行と範囲','', '```powershell',
        'Tools/TestCullingCpu.ps1','Tools/TestCulling.ps1 -Configuration Debug',
        'Tools/TestCulling.ps1 -Configuration Debug -UiOnly',
        'Tools/TestCulling.ps1 -Configuration Debug -MotionOnly',
        'Tools/TestCulling.ps1 -Configuration Debug -ResizeOnly',
        'Tools/MeasureCulling.ps1',
        "& 'C:/Program Files/Blender Foundation/Blender 5.2/5.2/python/bin/python.exe' Tools/CollectCullingResults.py",'```','',
        'Releaseでも同じ専用試験を実行します。撮影は`Tools/CaptureCulling.ps1`、正本の隔離再書出しは`SourceArt/Blender/CullingLab/validate.py --reexport`です。',
        '', '実機確認は上記RTX 4070環境です。他GPUやConditional Rendering非対応機での起動失敗は未実機検証です。静的形状と剛体移動を対象とし、スキニング・透明合成・Shadowの可視性判定、前フレームDepth再利用、SIMD／並列CPU最適化はこの初版に含めません。','']
    (ROOT/'Docs/CullingVerification.md').write_text('\n'.join(report),encoding='utf-8')
    print('Collected',len(rows)*5,'measured runs and GPU verification results')

if __name__=='__main__':main()
