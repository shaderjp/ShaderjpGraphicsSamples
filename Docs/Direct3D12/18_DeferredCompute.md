# Direct3D 12 第18 Compute ShaderでG-bufferをシェーディングする

Workshopの工房を使い、PBR、IBL、Shadow Map、HDR、BloomをDeferred Renderingへ統合します。第17回と同じG-bufferから、8×8のThread GroupでHDR画像へ書き込みます。位置はDepthから再構築し、位置用のRender Targetは作りません。

![完成表示](Images/18_DeferredCompute-cs-base.png)

1280×720、Standard、Heroカメラ、OpaqueMask、露出1、環境強度0.8。比較画像はRTX 4070で実際に描画した結果です。[共通章](../DeferredRendering.md)と[Workshop制作仕様](../Assets/Workshop.md)も参照してください。

## ビルドと実行

VS 2026／v145、Windows SDK 10.0.26100.0、C++20、x64の規約を維持します。Agility SDK 1.619.6、NuGet DXC 1.9.2609.5、FL12_0／SM6.6を使います。

```powershell
msbuild ShaderjpGraphicsSamples.sln -t:Restore -p:RestorePackagesConfig=true
msbuild Samples\Direct3D12\18_DeferredCompute\Direct3D12_18_DeferredCompute.vcxproj -p:Configuration=Debug -p:Platform=x64
& .\bin\x64\Debug\Direct3D12_18_DeferredCompute\Direct3D12_18_DeferredCompute.exe
```

アプリは同梱glTF・PNG・DDS・JSONを使います。Blenderと再ベイクは不要です。第17・18回は別の実行ファイルを持ち、共通HLSLと同じG-buffer設定で比較します。

## 処理の流れ

```text
Shadow Map → G-buffer → Compute Shader / Dispatch → RGBA16F HDR
→ Skybox → Forward透明合成 → Bloom → 露出・Reinhard・sRGB → UI
```

G-bufferはBase Color＋AO、Octahedral法線RG、Metallic＋Roughness、HDR Emissiveの4枚とDepthです。合計18 B／画素です。法線はNormal Map適用後のワールド法線を16bit×2へ符号化し、位置は画素中心とDepthを逆ViewProjectionで変換して復元します。詳しい格納形式と数式は[共通章](../DeferredRendering.md#位置を保存しないg-buffer)で説明しています。

## リソースと接続

固有Primitiveと画像を共有し、配置ごとに定数を設定してDrawします。Standardの点光源8個、Stressの72個はStructured Bufferで渡します。両版とも全ライトを同じ順に評価します。ディレクショナルライトのShadowは2048×2048で、MASKもAlpha判定を行います。

G-bufferの各チャンネルは整数画素の`Load`で読み、画素境界で材質や法線を混ぜません。Clear Depth 1の画素はライティング結果を0にして、後のSkyboxへ渡します。Fullでは透明カバーを別のForwardパスで合成します。

主なコードは[Sample.cpp](../../Samples/Direct3D12/18_DeferredCompute/Sample.cpp)、[Workshop CPUローダー](../../Common/Workshop.cpp)、[共通の照明](../../Shaders/Deferred/Lighting.hlsli)です。APIの初期化・リソース作成・転送・描画・同期・終了処理は各サンプルにあります。

## シェーダ

```hlsl
[numthreads(8,8,1)]
void CSMain(uint3 id : SV_DispatchThreadID) {
    if (any(id.xy >= uint2(screen.xy))) return;
    hdrOutput[id.xy] = float4(ShadePixel(id.xy), 1);
}
```

[LightingCompute.hlsl](../../Shaders/Deferred/LightingCompute.hlsl)は画素ごとに`ShadePixel`を呼びます。画像外のスレッドを除外するため、641×479など8の倍数でない画像にも対応します。VulkanのHDR出力は`rgba16f`を明示したStorage Imageです。タイル内のライトリスト、groupshared、Async Computeは後続テーマに残します。

`ShadePixel`はG-bufferを復号し、ワールド位置を再構築して、共通のGGX・Schlick・Smith・LambertとIBLを評価します。PS／CSの違いはDrawかDispatchか、およびHDRへの書込み方法です。Forward診断と透明合成も同じBRDFを使います。

## 同期

G-bufferを`RENDER_TARGET`から`ALL_SHADER_RESOURCE`へ、Depthを`DEPTH_READ | ALL_SHADER_RESOURCE`へ移します。Shader Resource ViewのDepth形式は`R32_FLOAT`、リソースは`R32_TYPELESS`、Depth Stencil Viewは`D32_FLOAT`です。

CSのHDR書込みは`UNORDERED_ACCESS`です。完了後に`RENDER_TARGET`へ遷移してSkyboxと透明を描き、`ALL_SHADER_RESOURCE`へ遷移してBloomへ渡します。遷移がUAV書込みの可視性も確保します。

定数・UI・ライトのUpload BufferはGPU完了を待ってから更新します。TimestampのQuery HeapをResolveし、同じ完了待ちの後でGPU時間を読みます。[Resource Barrier](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12)

## 操作と比較画像

- SceneとPresetでStandard／Stress、Opaque／OpaqueMask／Fullを切り替えます。
- Camera、20秒のCamera Path、Play、Frameで同じ構図を再現できます。
- ViewでBase Color、AO、符号化／復号法線、Metallic、Roughness、Emissive、Depth、Shadow Map、照明の寄与、Bloom途中画像を表示します。
- Shadow、3×3 PCF、Bias、Occluder X、IBL、Bloom、Threshold、Strength、Exposureを変更し、Resetで基準へ戻します。
- Forward diagnosticではG-buffer構築を省略し、直接HDRへ描画します。

```powershell
& .\bin\x64\Debug\Direct3D12_18_DeferredCompute\Direct3D12_18_DeferredCompute.exe --no-ui --shading normal
& .\bin\x64\Debug\Direct3D12_18_DeferredCompute\Direct3D12_18_DeferredCompute.exe --workshop-scene Stress --camera StressHero
& .\bin\x64\Debug\Direct3D12_18_DeferredCompute\Direct3D12_18_DeferredCompute.exe --preset Full --camera Transparency
```

比較には同じAPIの第17回を使います。Standard／Stress／Full／奇数サイズでG-bufferとDepthのバイト一致を試験し、HDR差は絶対`1e-3`＋相対`2e-3`以内にします。別API間ではラスタライズや浮動小数点の差も含まれるため、全画素のビット一致を要求しません。

## GPU時間と検証

UIはパス別のGPU時間を表示します。ReleaseのベンチマークはUIとVSyncを無効化し、60 framesの準備後に同じ経路を1200 frames評価します。

```powershell
& .\bin\x64\Release\Direct3D12_18_DeferredCompute\Direct3D12_18_DeferredCompute.exe --benchmark --workshop-scene Standard --timings D:\Temp\Direct3D12-cs-Standard.json
& Tools\TestWorkshopLoader.ps1
& Tools\TestDeferred.ps1 -Configuration Debug
& Tools\TestDeferred.ps1 -Configuration Release -Benchmark
```

計測はGPU時間の平均・中央値・p95をJSONへ、フレームごとの結果をCSVへ保存します。Benchmark中はOpaqueMaskと照明条件を固定します。PS／CSの処理時間はGPU・シーン・構成に依存します。[検証記録](../DeferredVerification.md)には実測値、SDK／コンパイラ／GPU、画像条件をまとめています。

本サンプルではディレクショナルライトのShadowを統合しています。点光源Shadow、IBLの局所遮蔽、Cyclesの間接光とは差があります。次のTiled／Clustered Renderingでは、この全ライトのループを画素ごとの候補へ絞り込んで比較します。

## カメラ操作

WASDで移動、Q/Eで上下移動、右ドラッグで視線を回転、Shiftで加速します。Rで選択中の固定カメラへ戻り、UIのResetはカメラと設定を初期化します。ImGuiの操作中はカメラ入力を抑えます。[共通の操作と検証手順](../CameraNavigation.md)も参照してください。
