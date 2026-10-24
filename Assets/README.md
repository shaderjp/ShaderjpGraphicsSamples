# 初回アセット

第29・30回は`Models/SilverRobotMotionSet`の地上移動・通常ジャンプ13クリップと、静的な箱・段差の`Models/TraversalCourse`を使います。[モーションマッチング](../Docs/MotionMatching.md)と[衝突付き移動](../Docs/TraversalController.md)を参照してください。

`Models/Cube.obj`と`Textures/UV.png`はこのリポジトリ用の自作データです。リポジトリのMITライセンスで利用できます。

PNGは左上が赤、右上が緑、左下が青、右下が黄です。ラベルと上向き矢印で色・上下方向・UVを確認できます。画像はsRGBとして読み込み、両APIのsRGB描画先へ出力します。

OBJは面ごとのUVと法線を持つ一辺2のBOXです。四角形をローダーで三角形化し、位置・UV・法線が異なる組ごとに頂点を分割します。OBJのV座標は読み込み時に`1-v`へ変換してPNGの上から下へ進む行順に合わせます。

Blender製アセットへの差し替えは[モデル読込の説明](../Docs/Direct3D12/05_Model.md)を参照してください。

## 後続glTF／PBR用アセット

[`Models/ShaderBall`](Models/ShaderBall/README.md)に、Shader Ball＋台座とAlbedo／PBR用テクスチャを用意しています。1モデル・1メッシュ・1マテリアルの自作MITデータです。[制作・読込仕様](../Docs/Assets/ShaderBall.md)と[Blender元データ](../SourceArt/Blender/ShaderBall/README.md)を参照してください。既存の05_ModelはOBJ専用で、[サンプル06のglTFローダー](../Docs/GltfLoading.md)がこのデータを読みます。

第10〜13回では[Lighting Lab](../Docs/Assets/LightingLab.md)を使います。`Models/LightingLab`は材質比較球・床・壁・遮蔽物、`Environments/LightingLab`は自作HDR環境のCubemap・Irradiance・粗さ別GGX環境・BRDF LUT、`Scenes/LightingLab`は配置とカメラ・ライト・配布Manifestです。自作アセットはMIT Licenseで、実行時の再生成は不要です。

第14〜16回は[モニター](../Docs/Assets/Monitor.md)を追加します。`Models/PostProcess/Monitor`のglTFと、`Scenes/PostProcess`の第二カメラ・配置・ハッシュを同梱します。Blender正本は`SourceArt/Blender/PostProcess`です。

GPUスキニング向けの[Silver Robot原本](../Docs/Assets/SilverRobot.md)は、`Models/SilverRobot`のAポーズglTF／GLBと`SourceArt/Blender/SilverRobot/SilverRobot.blend`へ保持しています。53ボーン、ウェイト、人型対応表、自作確認ActionはMIT Licenseです。

基本22／23と発展10では[Silver Robot Animated](../Docs/Assets/SilverRobotAnimated.md)を使います。`SilverRobotAnimated.blend`と`Models/SilverRobotAnimated`にUniversal Animation Library Standardの6クリップをリターゲットし、A/T確認ポーズも同梱しました。ロボットはMIT、QuaterniusモーションはCC0-1.0で、`Animations.json`に素材版・作者・固定コミット・SHA-256を保存しています。

発展18以降のパストレーシングには[専用Room](../Docs/Assets/PathTracing.md)と既存Workshopを使います。`Models/PathTracing`と`Scenes/PathTracing`に共通配布データ、`SourceArt/Blender/PathTracing`に保存正本を収録しています。
