"""Validate the tutorial's deliberately small glTF profile (stdlib only).

This complements, rather than replaces, Khronos glTF-Validator.
MIT License, Copyright (c) 2026 masafumi.
"""

import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
from urllib.parse import unquote, urlsplit
import zlib

TEXTURES = ("Albedo", "Normal", "Metallic", "Roughness", "AO", "ORM", "Emissive")
COMPONENTS = {5123: ("H", 2), 5125: ("I", 4), 5126: ("f", 4)}
WIDTHS = {"SCALAR": 1, "VEC2": 2, "VEC3": 3, "VEC4": 4}


def require(condition, message):
    if not condition:
        raise ValueError(message)


def external_file(root, uri):
    parts = urlsplit(uri)
    require(not parts.scheme and not parts.netloc and not parts.query and not parts.fragment,
            f"Only relative external URIs are supported: {uri}")
    require(not Path(unquote(parts.path)).is_absolute(), f"Absolute URI: {uri}")
    path = (root / unquote(parts.path)).resolve()
    require(path.is_relative_to(root.resolve()), f"URI escapes asset directory: {uri}")
    require(path.is_file(), f"Missing external file: {uri}")
    return path


def png_pixels(path):
    """Decode RGB/RGBA, 8-bit noninterlaced PNG, including all five filters."""
    data = path.read_bytes()
    require(data[:8] == b"\x89PNG\r\n\x1a\n", f"Not PNG: {path}")
    offset, compressed, header = 8, bytearray(), None
    while offset < len(data):
        size = struct.unpack_from(">I", data, offset)[0]
        kind, payload = data[offset + 4:offset + 8], data[offset + 8:offset + 8 + size]
        require(offset + size + 12 <= len(data), f"Truncated PNG chunk: {path}")
        crc = struct.unpack_from(">I", data, offset + 8 + size)[0]
        require(zlib.crc32(kind + payload) == crc, f"PNG CRC mismatch: {path}")
        if kind == b"IHDR":
            header = struct.unpack(">IIBBBBB", payload)
        elif kind == b"IDAT":
            compressed.extend(payload)
        elif kind == b"IEND":
            break
        offset += size + 12
    require(header is not None, f"Missing PNG header: {path}")
    width, height, depth, color, compression, filtering, interlace = header
    require(depth == 8 and color in (2, 6) and (compression, filtering, interlace) == (0, 0, 0),
            f"Expected 8-bit RGB/RGBA noninterlaced PNG: {path}")
    channels = 3 if color == 2 else 4
    stride = width * channels
    raw = zlib.decompress(compressed)
    require(len(raw) == height * (stride + 1), f"PNG scanline length: {path}")
    pixels, previous = bytearray(), bytearray(stride)
    for y in range(height):
        start = y * (stride + 1)
        mode, row = raw[start], bytearray(raw[start + 1:start + stride + 1])
        require(mode <= 4, f"Unknown PNG filter: {path}")
        for i in range(stride):
            left = row[i - channels] if i >= channels else 0
            above = previous[i]
            corner = previous[i - channels] if i >= channels else 0
            if mode == 1:
                predictor = left
            elif mode == 2:
                predictor = above
            elif mode == 3:
                predictor = (left + above) // 2
            elif mode == 4:
                p = left + above - corner
                distances = (abs(p - left), abs(p - above), abs(p - corner))
                predictor = (left, above, corner)[distances.index(min(distances))]
            else:
                predictor = 0
            row[i] = (row[i] + predictor) & 255
        pixels.extend(row)
        previous = row
    if channels == 3:
        rgba = bytearray(width * height * 4)
        for i in range(width * height):
            rgba[4 * i:4 * i + 3] = pixels[3 * i:3 * i + 3]
            rgba[4 * i + 3] = 255
        pixels = rgba
    return width, height, pixels


def accessor(data, buffers, index):
    entry = data["accessors"][index]
    require("sparse" not in entry and not entry.get("normalized", False), "No sparse/normalized accessors")
    code, size = COMPONENTS[entry["componentType"]]
    count, width = entry["count"], WIDTHS[entry["type"]]
    view = data["bufferViews"][entry["bufferView"]]
    stride = view.get("byteStride", size * width)
    begin = view.get("byteOffset", 0) + entry.get("byteOffset", 0)
    end = begin + (count - 1) * stride + size * width
    require(count > 0 and stride >= size * width and begin % size == 0,
            f"Invalid accessor layout: {index}")
    require(end <= view.get("byteOffset", 0) + view["byteLength"] <= len(buffers[view["buffer"]]),
            f"Accessor out of range: {index}")
    values = [struct.unpack_from("<" + code * width, buffers[view["buffer"]], begin + i * stride)
              for i in range(count)]
    require(all(math.isfinite(v) for row in values for v in row), f"Nonfinite accessor: {index}")
    for key, operation in (("min", min), ("max", max)):
        if key in entry:
            bounds = [operation(row[c] for row in values) for c in range(width)]
            require(all(abs(a - b) < 2e-6 for a, b in zip(bounds, entry[key])), f"Wrong {key}: {index}")
    return values


def inspect_variant(root, name, pbr):
    path = root / name
    data = json.loads(path.read_text(encoding="utf-8"))
    require(data["asset"]["version"] == "2.0", f"Wrong glTF version: {name}")
    for forbidden in ("extensionsUsed", "extensionsRequired", "animations", "skins", "cameras"):
        require(not data.get(forbidden), f"Unexpected {forbidden}: {name}")
    for key in ("nodes", "meshes", "materials", "scenes", "buffers"):
        require(len(data[key]) == 1, f"Expected one {key}: {name}")
    require(data["scenes"][data.get("scene", 0)]["nodes"] == [0], "Scene must select node 0")
    node = data["nodes"][0]
    require(node["mesh"] == 0 and not node.get("children"), "Expected one mesh node")
    require(not any(key in node for key in ("matrix", "translation", "rotation", "scale")),
            "Apply transforms to the source mesh before exporting")
    mesh = data["meshes"][0]
    require(len(mesh["primitives"]) == 1, "Expected one primitive")
    primitive = mesh["primitives"][0]
    require(primitive.get("mode", 4) == 4 and primitive["material"] == 0, "Expected material-0 triangles")
    require(not primitive.get("targets") and not primitive.get("extensions"), "No morph/compression")
    expected = {"POSITION", "NORMAL", "TEXCOORD_0"} | ({"TANGENT"} if pbr else set())
    require(set(primitive["attributes"]) == expected, f"Unexpected vertex attributes: {name}")
    buffers = []
    for entry in data["buffers"]:
        buffer = external_file(root, entry["uri"]).read_bytes()
        require(len(buffer) == entry["byteLength"], "Wrong external buffer size")
        buffers.append(buffer)
    attributes = {key: accessor(data, buffers, value) for key, value in primitive["attributes"].items()}
    vertices = len(attributes["POSITION"])
    require(all(len(v) == vertices for v in attributes.values()), "Attribute count mismatch")
    require(all(data["accessors"][v]["componentType"] == 5126 for v in primitive["attributes"].values()),
            "Vertex attributes must be FLOAT")
    indices = [v[0] for v in accessor(data, buffers, primitive["indices"])]
    require(data["accessors"][primitive["indices"]]["componentType"] in (5123, 5125), "16/32-bit indices")
    require(len(indices) % 3 == 0 and len(indices) // 3 <= 20000 and max(indices) < vertices,
            "Triangle count or index range")
    bounds = [[operation(v[c] for v in attributes["POSITION"]) for c in range(3)]
              for operation in (min, max)]
    require(abs(bounds[0][1]) < 1e-6 and abs(bounds[1][0] - bounds[0][0] - 1.2) < 0.005,
            f"Expected Y-up ground at 0 and width 1.2 m: {bounds}")
    require(all(0 <= v <= 1 for uv in attributes["TEXCOORD_0"] for v in uv), "UV outside unit square")
    for n in attributes["NORMAL"]:
        require(abs(sum(v * v for v in n) - 1) < 1e-4, "Normal is not unit length")
    if pbr:
        for normal, tangent in zip(attributes["NORMAL"], attributes["TANGENT"]):
            require(abs(sum(v * v for v in tangent[:3]) - 1) < 1e-4, "Tangent not unit length")
            # Blender's exporter rounds tangent components to four decimals.
            require(abs(sum(a * b for a, b in zip(normal, tangent))) < 2e-4, "Tangent not perpendicular")
            require(tangent[3] in (-1, 1), "Tangent handedness must be +/-1")
    material = data["materials"][0]
    require(material.get("alphaMode", "OPAQUE") == "OPAQUE" and not material.get("doubleSided", False),
            "Expected opaque, single-sided material")
    require(not material.get("extensions"), "No material extensions")
    image_uris = {entry["uri"] for entry in data["images"]}
    expected_images = {"Textures/Albedo.png"}
    if pbr:
        expected_images |= {"Textures/Normal.png", "Textures/ORM.png", "Textures/Emissive.png"}
    require(image_uris == expected_images and len(data["images"]) == len(expected_images),
            f"Wrong image references: {image_uris}")
    for uri in image_uris:
        external_file(root, uri)
    def texture_uri(info):
        require(info.get("texCoord", 0) == 0, "Only TEXCOORD_0")
        texture = data["textures"][info["index"]]
        return data["images"][texture["source"]]["uri"]
    metallic_roughness = material["pbrMetallicRoughness"]
    require(texture_uri(metallic_roughness["baseColorTexture"]) == "Textures/Albedo.png", "BaseColor binding")
    if pbr:
        require(texture_uri(metallic_roughness["metallicRoughnessTexture"]) == "Textures/ORM.png", "ORM binding")
        require(texture_uri(material["occlusionTexture"]) == "Textures/ORM.png", "AO must share ORM")
        require(texture_uri(material["normalTexture"]) == "Textures/Normal.png", "Normal binding")
        require(texture_uri(material["emissiveTexture"]) == "Textures/Emissive.png", "Emissive binding")
    else:
        require(not any(k in material for k in ("normalTexture", "occlusionTexture", "emissiveTexture"))
                and "metallicRoughnessTexture" not in metallic_roughness, "Albedo-only material")
    # Compare expanded indexed triangles: tangent-driven vertex splitting is legal.
    geometry = [tuple(attributes[key][i] for key in ("POSITION", "NORMAL", "TEXCOORD_0")) for i in indices]
    summary = {"vertices": vertices, "triangles": len(indices) // 3, "bounds": bounds,
               "attributes": sorted(expected), "images": sorted(image_uris), "indexComponentType":
               data["accessors"][primitive["indices"]]["componentType"]}
    return summary, geometry, data


def inspect_textures(root):
    pixels, summary = {}, {}
    for name in TEXTURES:
        path = root / "Textures" / (name + ".png")
        width, height, rgba = png_pixels(path)
        require((width, height) == (1024, 1024), f"Wrong image size: {name}")
        require(set(rgba[3::4]) == {255}, f"Nonopaque pixels: {name}")
        pixels[name] = rgba
        summary[name] = {"width": width, "height": height, "sha256": hashlib.sha256(path.read_bytes()).hexdigest()}
    for name in ("AO", "Roughness", "Metallic"):
        require(pixels[name][0::4] == pixels[name][1::4] == pixels[name][2::4], f"Not grayscale: {name}")
    for channel, name in enumerate(("AO", "Roughness", "Metallic")):
        require(pixels["ORM"][channel::4] == pixels[name][0::4], f"ORM channel {channel} != {name}")
    require(set(pixels["Metallic"][0::4]) == {0, 255}, "Metallic must contain both painted and metal areas")
    require(len(set(pixels["Roughness"][0::4])) >= 4, "Need multiple roughness areas")
    require(min(pixels["AO"][0::4]) < 220 and max(pixels["AO"][0::4]) == 255, "Need nontrivial AO")
    normal = pixels["Normal"]
    require(max(normal[0::4]) - min(normal[0::4]) > 15 and min(normal[2::4]) >= 128,
            "Normal map must include tangent detail with positive Z")
    for i in range(0, len(normal), 4):
        length_squared = sum((normal[i + c] / 127.5 - 1) ** 2 for c in range(3))
        require(abs(length_squared - 1) < 0.025, f"Invalid decoded normal at pixel {i // 4}")
    emissive = pixels["Emissive"]
    lit = sum(any(emissive[i + c] for c in range(3)) for i in range(0, len(emissive), 4))
    require(0 < lit < 1024 * 1024 * 0.02, "Emissive should cover a small area")
    return summary


def inspect(root):
    albedo, geometry_a, json_a = inspect_variant(root, "ShaderBall_Albedo.gltf", False)
    pbr, geometry_b, json_b = inspect_variant(root, "ShaderBall_PBR.gltf", True)
    require(geometry_a == geometry_b, "Albedo/PBR position, normal, UV or triangle order mismatch")
    textures = inspect_textures(root)
    manifest = json.loads((root / "Manifest.json").read_text(encoding="utf-8"))
    actual_files = {p.relative_to(root).as_posix() for p in root.rglob("*")
                    if p.is_file() and p.suffix in (".gltf", ".bin", ".png")}
    require(set(manifest["files"]) == actual_files, "Manifest file list mismatch")
    for relative, info in manifest["files"].items():
        raw = (root / relative).read_bytes()
        require(len(raw) == info["bytes"] and hashlib.sha256(raw).hexdigest() == info["sha256"],
                f"Manifest hash mismatch: {relative}")
    return {"albedo": albedo, "pbr": pbr, "textures": textures}, (geometry_a, json_a, json_b)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("asset_dir", type=Path)
    parser.add_argument("--compare", type=Path, help="Re-export of the saved source")
    parser.add_argument("--report", type=Path)
    args = parser.parse_args()
    root = args.asset_dir.resolve()
    report, original = inspect(root)
    if args.compare:
        repeated, exported = inspect(args.compare.resolve())
        require(original == exported, "Saved-source re-export changed geometry or glTF declarations")
        require(report == repeated, "Saved-source re-export changed textures or counts")
        for relative in json.loads((root / "Manifest.json").read_text())["files"]:
            require((root / relative).read_bytes() == (args.compare / relative).read_bytes(),
                    f"Nonreproducible exported file: {relative}")
        report["savedSourceReexport"] = "identical geometry, material, images and binary bytes"
    report["result"] = "PASS"
    if args.report:
        args.report.parent.mkdir(parents=True, exist_ok=True)
        args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(json.dumps(report, indent=2))


if __name__ == "__main__":
    main()
