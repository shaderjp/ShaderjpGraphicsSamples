"""Export Albedo/PBR variants from the SAVED .blend; never regenerate geometry.

Run: blender --background ShaderBall.blend --python export_shader_ball.py --
     --output-dir <path> [--render-dir <path>]
MIT License, Copyright (c) 2026 masafumi.
"""

import argparse
import hashlib
import json
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from shader_ball_tools import (MESH_NAME, TEXTURE_NAMES, build_material, render_references,
                               save_source_image, source_images)


def export_variant(obj, filepath, tangents):
    bpy.ops.object.select_all(action="DESELECT")
    obj.select_set(True)
    bpy.context.view_layer.objects.active = obj
    bpy.ops.export_scene.gltf(
        filepath=str(filepath), export_format="GLTF_SEPARATE", export_image_format="AUTO",
        export_texture_dir="Textures", use_selection=True, export_yup=True,
        export_texcoords=True, export_normals=True, export_tangents=tangents,
        export_materials="EXPORT", export_vertex_color="NONE", export_attributes=False,
        export_animations=False, export_morph=False, export_skins=False,
        export_cameras=False, export_lights=False, export_extras=False,
        export_apply=False, export_draco_mesh_compression_enable=False,
        export_image_add_webp=False, export_image_webp_fallback=False,
        export_copyright="Copyright (c) 2026 masafumi. MIT License.",
    )
    data = json.loads(filepath.read_text(encoding="utf-8"))
    if data.get("extensionsUsed") or data.get("extensionsRequired"):
        raise RuntimeError(f"Core-only asset unexpectedly exports extensions: {data.get('extensionsUsed')}")
    if len(data["nodes"]) != 1 or len(data["meshes"]) != 1 or len(data["materials"]) != 1:
        raise RuntimeError("Export must have one node, mesh, and material")
    if len(data["meshes"][0]["primitives"]) != 1:
        raise RuntimeError("Export must have one primitive")
    # Stable readable JSON, preserving all actual Blender-exported declarations.
    filepath.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser()
    root = Path(__file__).resolve().parents[3]
    parser.add_argument("--output-dir", type=Path, default=root / "Assets/Models/ShaderBall")
    parser.add_argument("--render-dir", type=Path)
    args = parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])
    if bpy.app.version[:2] != (5, 2):
        raise RuntimeError("Use Blender 5.2 LTS")
    if not bpy.data.filepath:
        raise RuntimeError("Open the saved ShaderBall.blend before export")
    obj = bpy.data.objects.get(MESH_NAME)
    if obj is None or obj.type != "MESH" or len(obj.data.materials) != 1:
        raise RuntimeError("ShaderBall must be one mesh with one material slot")
    if any(len(p.vertices) != 3 for p in obj.data.polygons):
        raise RuntimeError("Triangulate the edited source before export and normal baking")
    if len(obj.data.polygons) > 20000 or len(obj.data.uv_layers) != 1 or obj.modifiers:
        raise RuntimeError("Use <=20000 triangles, one UV layer, and applied modifiers")
    if any(abs(v - 1) > 1e-6 for v in obj.scale) or obj.parent is not None:
        raise RuntimeError("Apply scale and remove parent transforms before export")
    if any(abs(v) > 1e-6 for v in obj.location) or any(abs(v) > 1e-6 for v in obj.rotation_euler):
        raise RuntimeError("Apply location and rotation: the first loader uses an identity node")
    images = source_images()
    output = args.output_dir.resolve()
    texture_dir = output / "Textures"
    texture_dir.mkdir(parents=True, exist_ok=True)
    for name, image in images.items():
        path = texture_dir / (name + ".png")
        save_source_image(image, path)
        # Relative saved-source paths must not affect where the exporter finds PNGs.
        image.filepath = str(path)
    original = obj.data.materials[0]
    albedo = build_material(images, "albedo")
    try:
        obj.data.materials[0] = albedo
        export_variant(obj, output / "ShaderBall_Albedo.gltf", False)
    finally:
        obj.data.materials[0] = original
        bpy.data.materials.remove(albedo)
    export_variant(obj, output / "ShaderBall_PBR.gltf", True)
    if args.render_dir:
        render_references(args.render_dir.resolve(), images)
    files = sorted(p for p in output.rglob("*") if p.is_file() and p.suffix in (".gltf", ".bin", ".png"))
    manifest = {
        "asset": "ShaderBall", "license": "MIT", "copyright": "Copyright (c) 2026 masafumi",
        "blender": bpy.app.version_string, "generator": "Shader.jp Shader Ball 1.0",
        "source": "SourceArt/Blender/ShaderBall/ShaderBall.blend",
        "triangles": len(obj.data.polygons), "uvSets": 1,
        "files": {p.relative_to(output).as_posix(): {
            "bytes": p.stat().st_size, "sha256": hashlib.sha256(p.read_bytes()).hexdigest()
        } for p in files},
    }
    (output / "Manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8", newline="\n")
    print(f"Exported saved source to {output}", flush=True)


if __name__ == "__main__":
    main()
