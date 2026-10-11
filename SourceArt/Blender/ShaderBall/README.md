# Shader BallのBlender元データ

`ShaderBall.blend`が編集後の正本です。Blender 5.2.1 LTSで制作しました。配布用glTFを更新するときは保存済みデータから書き出し、初期生成スクリプトを再実行しないでください。

形状・画像・読込仕様・参考レンダーは[アセット解説](../../../Docs/Assets/ShaderBall.md)にまとめています。Blender自体はサンプル実行時には不要です。以下はリポジトリルートのPowerShellで実行します。

```powershell
$blenderExe = 'C:\Program Files\Blender Foundation\Blender 5.2\blender.exe'
```

## 保存済みデータから再書出し

```powershell
& $blenderExe --background SourceArt/Blender/ShaderBall/ShaderBall.blend --python-exit-code 1 --python SourceArt/Blender/ShaderBall/export_shader_ball.py -- --output-dir Assets/Models/ShaderBall
.\Tools\TestShaderBallAssets.ps1 -BlenderPath $blenderExe
```

`export_shader_ball.py`は初期生成を呼ばず、開いた`.blend`の形状・UV・保存済みPBR材質・梱包画像を使います。Albedo版の材質への一時差し替えも元に戻し、`.blend`を保存しません。PBR材質の編集はcore glTFで表せる範囲に留めてください。Clearcoatなど拡張を必要とする編集は書出し後の検査でエラーになります。

参考画像を更新する場合は`--render-dir`を追加します。Cycles CPU、48 samples、1200×1000、PBRはAgX／Medium High Contrast、AlbedoはEmission／Standardです。

```powershell
& $blenderExe --background SourceArt/Blender/ShaderBall/ShaderBall.blend --python-exit-code 1 --python SourceArt/Blender/ShaderBall/export_shader_ball.py -- --output-dir Assets/Models/ShaderBall --render-dir Docs/Assets/Images
```

## Blenderでの編集

1. `ShaderBall.blend`を開きます。`EXPORT`コレクションの`ShaderBall`だけが配布対象です。`PREVIEW_ONLY`の床・カメラ・ライトは撮影用です。
2. 形状を編集したら位置・回転・スケールとモディファイアを適用し、三角形化します。1オブジェクト、1マテリアル、UVMap一つ、20,000三角形以下を保ちます。接地面はBlenderのZ=0、幅約1.2 mにします。
3. 画像編集では`Albedo`など既存の画像名を保持します。sRGBはAlbedo／Emissive、Non-ColorはNormal／Metallic／Roughness／AO／ORM／BakeHeightです。単独画像はFake Userを設定して保存時の消失を防いでいます。
4. Texture Paintなどで画素を編集したら、画像を保存して梱包し直してから`.blend`を保存します。Image EditorのImageメニューのPackを使うか、既存の梱包をUnpackし、編集したPNGを保存したうえでPackします。`.blend`保存だけでは古い梱包PNGが残る場合があるため、必ず再書出しと参考レンダーで確認してください。
5. UV・法線・形状・BakeHeight・Metallic・Roughnessを変えたら、次の再ベイクを実行します。Normal／AO／ORMだけを更新し、Albedo・Metallic・Roughness・Emissive・形状を初期値へ戻しません。

Normalの元になる微細な高さ模様`BakeHeight`は制作専用の8枚目の梱包画像です。glTFにも配布テクスチャにも出力しません。実形状の浅い溝と、Normalに焼く微細な模様を分けています。

```powershell
# 編集結果を先にBlenderで保存します。指定したファイルへ再ベイク結果を保存します。
& $blenderExe --background SourceArt/Blender/ShaderBall/ShaderBall.blend --python-exit-code 1 --python SourceArt/Blender/ShaderBall/rebake_shader_ball.py -- --output-blend SourceArt/Blender/ShaderBall/ShaderBall.blend
```

元ファイルを保つ場合は`--output-blend .cache/shader-ball/Edited.blend`など別の保存先を指定できます。再ベイクは撮影用オブジェクトを一時的に除外し、Cycles CPU、64 samples、16 px marginでNormalとAOを作ります。Normalのパディング部分も正規化し、PNGへ線形値のまま保存します。ORMは保存したAOと既存のRoughness・Metallicから作ります。

名前付き材質ノード`Texture_Normal`と`Texture_ORM`は再ベイク時の接続先です。再ベイクを使う間はノード名を保持してください。画像をファイルデータへ変換してから梱包し、Blenderを閉じた後も画素を保持します。

## 初期モデルを新規生成する場合

通常の再書出しでは使いません。初期生成を別の空の保存先で試すコマンドです。

```powershell
& $blenderExe --background --factory-startup --python-exit-code 1 --python SourceArt/Blender/ShaderBall/generate_shader_ball.py -- --source-dir .cache/shader-ball/NewSource
```

球・溝・台座・UV・模様をPythonで生成し、三角形化とスケール適用後にベイクします。既存`ShaderBall.blend`がある保存先ではエラーにします。初期値へ完全に戻す場合だけ`--overwrite-source`を明示します。このオプションは編集したモデル・画像・材質も置き換えます。

Blender 5.2系を検査するほか、書出し時には未適用モディファイア、UV数、三角形化、個数、拡張の混入を検査します。PNGとglTFは書出し先へ配置され、元データの`.generated`フォルダはなくても梱包済み`.blend`から再現できます。

## 検証

`Tools/TestShaderBallAssets.ps1`が保存済み`.blend`から新しい一時フォルダへ再書出しし、JSON・BIN・画像と各属性の一致を確認します。`--python-exit-code 1`を`--python`より前に指定し、Pythonエラーを終了コードへ反映します。

検証ツールの自動取得先はKhronos公式GitHubのglTF-Validator 2.0.0-dev.3.10です。ネットワークなしの環境では事前に取得した同版を`-ValidatorPath`で指定できます。`-SkipReexport`は配布ファイルだけを検査する短縮オプションで、完全な再現性検証には使いません。

MIT License, Copyright (c) 2026 masafumi。外部Pythonパッケージは不要です。生成・ベイク・書出しはBlender付属bpy／NumPy、構造検証はPython標準ライブラリを使用します。
