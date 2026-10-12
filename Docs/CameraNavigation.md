# 第13回以降のカメラ操作

Direct3D12／Vulkanの第13〜20回とAdvancedのレイトレーシングサンプルで、同じ操作を使います。起動時は従来の固定カメラです。描画領域をクリックしてから操作してください。

| 操作 | 動作 |
|---|---|
| W／S | 視線方向へ前進／後退 |
| A／D | 左／右へ移動 |
| Q／E | ワールドの下／上へ移動 |
| Shift＋移動キー | 速度を4倍にする |
| 右マウスボタンを押してドラッグ | その場で視線を回転 |
| R | 選択中の固定カメラへ戻す |
| UIのReset | カメラとサンプルの設定を初期値へ戻す |
| Esc | 終了 |

通常の移動速度は2 m/s、Shift時は8 m/sです。斜め移動でも速度は一定です。上下の回転は極を避けて制限します。形状との衝突判定は行わないので、室内への移動や遮蔽物の裏側の確認に使えます。

ImGuiがマウスやキーボード入力を取得している間は、対応するカメラ入力を抑えます。フォーカスを失うと押下状態と右ドラッグを解除します。ドラッグ中はWin32のマウスキャプチャを使い、ウィンドウ外でボタンを離しても回転が続かないようにします。

WorkshopのScene／固定Cameraを変更すると、その視点から操作をやり直せます。Camera pathを有効にすると操作による変更を解除し、経路を表示します。経路の再生中に移動・回転すると、その時点の位置・向き・FOV・Near／Farを引き継いで再生を止めます。Rは選択中の固定カメラへ戻します。性能測定の`--benchmark`では操作を無効にして、既定の経路を維持します。

第14〜16回のRender-to-Texture用第二カメラは固定です。メインカメラを操作しても、モニターの映像取得用カメラは動きません。FOV／Near／Farはそれぞれのシーン設定を維持します。

## 実装

[CameraNavigation.h](../Samples/Shared/CameraNavigation.h)はCPUの入力・視点だけを保持します。各サンプルの`OnMessage`でImGuiとWin32入力を渡し、`OnUpdate`で移動を評価します。GPUリソース、描画、同期は各APIのサンプルに残します。

カメラを操作するまで、既存の定数生成関数は元の位置・注視点をそのまま使います。操作後は位置・注視点を上書きし、同じ投影条件でViewProjectionとその逆行列を更新します。DeferredのG-buffer、深度による位置復元、透明物の並べ替え、Skybox、Tile／ClusterのView行列、レイトレーシングのレイ生成で同じカメラを参照します。位置用G-bufferは追加しません。Shadow Mapのライト視点も変更しません。

## 検証

```powershell
./Tools/ValidateProjects.ps1
./Tools/TestCameraNavigationCpu.ps1
./Tools/TestCameraNavigation.ps1 -Configuration Debug
./Tools/TestCameraNavigation.ps1 -Configuration Release
```

CPU試験は左手系の前進、斜め移動の速度、Shift、UI入力取得、フォーカス・キャプチャ喪失、上下回転の制限、Resetを確認します。GPU試験は各実行ファイルへWin32のキーとマウスメッセージを送り、1280×720の移動前・移動後・回転後・Reset後の画像を比較します。移動と回転で各1,000画素以上が変わり、Reset後のRGBは初期画像と完全一致することを要求します。

`--test-camera`はこの自動試験専用です。バックグラウンドでの入力と1/60秒の移動刻みを許可します。通常実行の操作はフォアグラウンドのウィンドウに限定します。画像は`.cache/camera-navigation/<構成>`へ保存し、既存の記事画像を上書きしません。

Visual Studioの全APIサンプルは、[ソリューション](../ShaderjpGraphicsSamples.sln)のDirect3D12／Vulkanフォルダへ配置します。既に開いているVisual Studioにはソリューションを再読み込みしてください。`ValidateProjects`はプロジェクトのXML／filtersに加え、ソリューションとプロジェクトのGUID一致、APIフォルダへの配置も検証します。
