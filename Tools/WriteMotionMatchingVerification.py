"""Write documentation only after the retained samples pass current tests."""
from pathlib import Path
import hashlib
import json
import zipfile

ROOT = Path(__file__).resolve().parents[1]
WORK = ROOT / '.cache/motion-matching'

def read(name):
    return json.loads((ROOT / 'Docs' / name).read_text(encoding='utf-8-sig'))

environment = read('MotionMatchingEnvironment.json')
cpu = read('MotionMatchingCpu.json')
assets = read('MotionAssetsVerification.json')
assert cpu['result'] == assets['result'] == 'PASS'
assert cpu['clips'] == 13 and cpu['searchableFrames'] == 281
assert cpu['normalJumpLanded'] and cpu['fallRecovery'] and cpu['rotatedWallBlocked']
assert .19 < cpu['stepHeightMetres'] < .22 and cpu['maxCapsulePenetrationMetres'] < .01
for source, digest in assets['sourceHashes'].items():
    assert hashlib.sha256((ROOT / source).read_bytes()).hexdigest().upper() == digest.upper()
for result in assets['khronos'].values():
    assert result['numErrors'] == result['numWarnings'] == 0

for configuration in ['Debug', 'Release']:
    results = read(f'MotionMatchingGpu{configuration}.json')
    assert len(results) == 4
    for result in results:
        assert result['validationErrors'] == 0 and result['resetRgb'] == 'identical'
        assert result['maxFootError'] < .05 and result['maxPenetration'] < .01
        binary = ROOT / f"bin/x64/{configuration}/{result['name']}/{result['name']}.exe"
        assert hashlib.sha256(binary.read_bytes()).hexdigest() == result['executableSha256']
    lifecycle = (WORK / f'lifecycle-{configuration.lower()}.log').read_text(encoding='utf-8-sig')
    assert lifecycle.count('passed (1280 x 720') == 6
    cpu_log = (WORK / f'cpu-{configuration.lower()}.log').read_text(encoding='utf-8-sig')
    assert 'PASS: swept capsule' in cpu_log

ui = read('MotionMatchingUiCaptures.json')
assert len(ui) == 4
for capture in ui:
    api, topic = capture['sample'].split('_', 1)
    image = ROOT / f'Docs/{api}/Images/{topic}-ui.png'
    assert hashlib.sha256(image.read_bytes()).hexdigest() == capture['sha256']

destination = ROOT / 'Docs/Measurements/MotionMatching'
destination.mkdir(parents=True, exist_ok=True)
with zipfile.ZipFile(destination / 'ValidationLogs.zip', 'w', compression=zipfile.ZIP_DEFLATED) as archive:
    for name in ['cpu-debug.log', 'cpu-release.log', 'projects.log', 'loaders.log', 'assets-test.log',
                 'gpu-debug.log', 'gpu-release.log', 'lifecycle-debug.log', 'lifecycle-release.log', 'ui.log']:
        path = WORK / name
        assert path.exists(), name
        archive.write(path, 'summary/' + name)
    for name in ['restore.log', 'build-Debug.log', 'build-Release.log']:
        archive.write(ROOT / '.cache/skinning' / name, 'build/' + name)
    for configuration in ['Debug', 'Release']:
        for path in (WORK / f'gpu/{configuration}').glob('*.log'):
            archive.write(path, configuration + '/' + path.name)
        for topic in ['28_CharacterController', '29_MotionMatching', '30_TraversalController']:
            for path in (ROOT / f'.cache/validation/{configuration}').glob(f'*_{topic}.log'):
                archive.write(path, f'lifecycle-{configuration}/' + path.name)

deformation = max(clip['maxErrorMetres'] for clip in assets['deformation']['clips'].values())
text = f'''# 第29・30回の検証記録

検証日：{environment['date']}（日本時間）。基本編30テーマと発展編17テーマ、両APIで94本の構成です。今回のGPU操作試験は第29・30回の4アプリ、ウィンドウ操作試験は第28〜30回の6アプリを対象にしました。

## 環境とビルド

Windows 11 Pro {environment['os']['Version']}、Visual Studio 2026 {environment['visualStudio']}、v145／MSVC {environment['msvcTools']}、cl {environment['compiler']}、C++20、Windows SDK {environment['windowsSdk']}。GPUは{environment['gpu']['Name']}、ドライバ{environment['gpu']['DriverVersion']}です。Blenderは{environment['blender']}です。

D3D12はAgility SDK {environment['agilitySdk']}／DXC 1.9.2609.5、VulkanはSDK 1.4.341.1と同梱DXCを使用しました。SM6.6、HLSL 2021、Vulkan 1.3です。[詳細環境](MotionMatchingEnvironment.json)

ソリューションをRestoreし、x64 Debug／Releaseをビルドしました。SPIR-V検証、全vcxproj／filtersのXML・項目対応・GUID検査、ローダー試験も成功しました。

## 素材とCPU

- 保存済みBlenderからの再出力は配布データとバイト一致し、検証による正本の書換えはありません。
- モデルglTF／GLBとコースglTFはKhronos Validatorのエラー・警告とも0件です。
- 13クリップのBlenderと独立glTFスキニング比較は最大誤差{deformation:.8g} mでした。
- 地上検索は281フレーム・30要素。全候補に対する検索、30／60／144 fps、Rootの向き、足IK、慣性化、古い検索データの拒否を確認しました。
- 足IK残差は最大{cpu['maxFootResidualMetres']:.8g} m。回転した壁のスイープ、通常ジャンプと着地、20 cm段差、壁での停止、落下復帰とResetをDebug／Releaseで確認しました。
- 段差を歩いて上った最大高さは{cpu['stepHeightMetres']:.6f} m、ジャンプ試験のカプセル侵入は最大{cpu['maxCapsulePenetrationMetres']:.8g} mです。

[素材の結果](MotionAssetsVerification.json)／[CPUの結果](MotionMatchingCpu.json)

## GPUとウィンドウ

第29回を300フレーム、第30回を900フレームで両API・両構成実行し、8ケース成功しました。DebugではD3D12 Debug Layer／Vulkan Validation Layerと同期検証を有効にし、Validationエラーは0件です。両APIのCPUトレースは一致し、歩行画像の変化とReset画像のRGB一致を確認しました。

第28〜30回の両API・両構成、計12ケースで1280×720描画、640×480リサイズ、最小化・復帰、1280×720への復帰、PNG取得と終了を確認しました。

画像は更新した実行ファイルから得た1280×720の実GPUキャプチャです。自動入力による試験であり、第29・30回の物理パッドによる手動操作試験は今回の結果に含めません。

[Debugの結果](MotionMatchingGpuDebug.json)／[Releaseの結果](MotionMatchingGpuRelease.json)／[UI画像](MotionMatchingUiCaptures.json)／[ログ](Measurements/MotionMatching/ValidationLogs.zip)

## 再現コマンド

```powershell
./Tools/ValidateProjects.ps1
./Tools/BuildSkinning.ps1 -Configuration Debug -Restore
./Tools/BuildSkinning.ps1 -Configuration Release
./Tools/TestLoaders.ps1
./Tools/TestMotionAssets.ps1
./Tools/TestMotionMatchingCpu.ps1 -Configuration Debug
./Tools/TestMotionMatchingCpu.ps1 -Configuration Release
./Tools/TestMotionMatching.ps1 -Configuration Debug
./Tools/TestMotionMatching.ps1 -Configuration Release -CaptureDocs
./Tools/TestSamples.ps1 -Configuration Debug -Topics 28_CharacterController,29_MotionMatching,30_TraversalController
./Tools/TestSamples.ps1 -Configuration Release -Topics 28_CharacterController,29_MotionMatching,30_TraversalController
./Tools/CaptureMotionMatchingUi.ps1
```
'''
(ROOT / 'Docs/MotionMatchingVerification.md').write_text(text, encoding='utf-8', newline='\n')
verification = ROOT / 'Docs/Verification.md'
old = verification.read_text(encoding='utf-8-sig')
old = old.replace('最新の追加結果は末尾の「第28回 キャラクター操作」を参照してください。',
                  '最新の追加結果は末尾の「第29・30回 モーションマッチングと衝突付き移動」を参照してください。')
marker = '\n## 第29・30回 モーションマッチングと衝突付き移動\n'
old = old.split(marker)[0].rstrip() + '\n' + marker + '\n2026-10-22（日本時間）。基本編を第30回までに整理しました。全ソリューションの両構成ビルド、素材とCPU、追加4アプリの両構成GPU試験、第28〜30回のウィンドウ操作を確認しました。[検証記録](MotionMatchingVerification.md)を参照してください。\n'
verification.write_text(old, encoding='utf-8', newline='\n')
print('Current binaries, source hashes, CPU/GPU/lifecycle/UI reports verified; documentation written.')
