# Shader Ball配布データ

自作のShader Ball＋台座です。MIT License、Copyright (c) 2026 masafumi。リポジトリの[LICENSE](../../../LICENSE)に従って利用できます。

- `ShaderBall_Albedo.gltf`＋`ShaderBall_Albedo.bin`：POSITION、NORMAL、TEXCOORD_0、indices。Albedo 1枚だけを参照します。
- `ShaderBall_PBR.gltf`＋`ShaderBall_PBR.bin`：同じ形状とUVにTANGENTを追加。Albedo、Normal、ORM、Emissiveを参照します。
- `Textures`：同じUVに対応する1024×1024のPNG 7枚。Metallic／Roughness／AOは個別の教材用画像としても同梱しています。
- `Manifest.json`：配布ファイルのサイズ、SHA-256、制作ツールのバージョン。

1 Node・1 Mesh・1 Primitive・1 Material、8,640三角形、幅1.2 m、高さ1.16 m。glTF 2.0の右手系・Y-up、接地面Y=0、UVは画像左上基準です。ORMはR=AO／G=Roughness／B=Metallic、NormalはglTFの接線空間です。

編集は[梱包画像を持つBlender正本](../../../SourceArt/Blender/ShaderBall/ShaderBall.blend)で行い、保存済みデータから書き出します。[制作・読込仕様](../../../Docs/Assets/ShaderBall.md)と[検証記録](../../../Docs/Assets/Verification.md)を参照してください。

このデータ向けのC++ glTFローダーは[サンプル06](../../../Docs/GltfLoading.md)で実装しています。06はAlbedo、07はLambert／Blinn-Phong、08はPBR照明とNormal／ORM／Emissiveを表示します。[07→08の解説](../../../Docs/LightingAndPBR.md)を参照してください。既存の05_ModelはOBJ専用です。
