"""Summarize five validated Release runs per API/condition from actual GPU CSVs."""
from pathlib import Path
import csv
import hashlib
import json
import math
import shutil
import statistics
import sys

repo = Path(__file__).resolve().parents[1]
work = Path(sys.argv[1]) if len(sys.argv) > 1 else repo / ".cache/inline-tests/Release"
environment = json.loads((work.parent / "Environment.json").read_text(encoding="utf-8-sig"))
cases = {
    "26_InlineRaytracingShadows": "基本26：RayQueryの太陽の影",
    "ShadowMap": "基本26：Shadow Map",
    "27_InlineRaytracingReflections": "基本27：太陽の影＋反射",
    "Advanced16_BindlessInlineHybrid": "発展16：Inline Hybrid",
    "PipelineHybrid": "発展15：RT Pipeline比較",
    "Advanced17_MeshShaderTiledInlineShadows": "発展17：Tiled・影付き8灯",
    "ManyAll8": "発展17：全ライト評価・影付き8灯",
    "ManyTile32": "発展17：Tiled・影付き32灯",
}
results = []
measurements = repo / "Docs/Measurements/InlineRaytracing"
measurements.mkdir(parents=True, exist_ok=True)
shutil.copy2(work.parent / "Environment.json", measurements / "Environment.json")
for api in ("Direct3D12", "Vulkan"):
    for case, title in cases.items():
        values, run_means, startup, hashes = [], [], [], []
        memory = None
        for repeat in range(1, 6):
            path = work / f"{api}-{case}-benchmark-{repeat}.json"
            report = json.loads(path.read_text(encoding="utf-8-sig"))
            assert report["measuredFrames"] == 1200 and report["warmupFrames"] == 60, path
            assert report["configuration"] == "Release" and report["width"] == 1280 and report["height"] == 720, path
            if case in ("Advanced16_BindlessInlineHybrid", "PipelineHybrid"):
                assert report["objects"] == 512 and report["residentMaterials"] == 64, path
                assert report["binding"] == "textures" and not report["gpuDriven"], path
                assert report["rtShadows"] and report["rtReflections"], path
            with path.with_suffix(".csv").open(newline="", encoding="utf-8-sig") as stream:
                rows = list(csv.DictReader(stream))
            assert len(rows) == 1200, path
            column = "gpu-total" if "gpu-total" in rows[0] else "total"
            run = [float(row[column]) for row in rows]
            assert all(math.isfinite(v) and v > 0 for v in run), path
            run_means.append(statistics.fmean(run))
            values.extend(run)
            inline = report.get("inlineRaytracing")
            if inline:
                if "17_" in case or case.startswith("Many"):
                    assert inline["shadowPointLights"] == (32 if case == "ManyTile32" else 8), path
                startup.append(inline["asStartupMs"])
                current_memory = {
                    "accelerationStructuresCommittedBytes": inline["accelerationStructuresCommittedBytes"],
                    "additionalInlineSceneBufferBytes": inline["inlineSceneBufferBytes"],
                    "scope": "AS allocations and additional CPU-uploaded ray scene buffers; excludes raster geometry, attachments and transient scratch",
                }
            else:
                startup.append(report["startupMs"]["asBuild"])
                current_memory = report["memory"]
            if memory is None:
                memory = current_memory
            hashes.append({"file": path.name, "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
                           "csvSha256": hashlib.sha256(path.with_suffix(".csv").read_bytes()).hexdigest()})
            shutil.copy2(path, measurements / path.name)
            shutil.copy2(path.with_suffix(".csv"), measurements / path.with_suffix(".csv").name)
        ordered = sorted(values)
        results.append({
            "api": api, "case": case, "title": title, "measuredFrames": len(values), "repeats": 5,
            "averageMs": statistics.fmean(values), "medianMs": statistics.median(values),
            "p95Ms": ordered[min(len(ordered) - 1, int(len(ordered) * .95))],
            "minimumRunAverageMs": min(run_means), "maximumRunAverageMs": max(run_means),
            "asStartupAverageMs": statistics.fmean(startup), "memory": memory, "sourceReports": hashes,
        })
output = {
    "schemaVersion": 1, "environment": environment,
    "measurementDirectory": "Measurements/InlineRaytracing",
    "conditions": {"configuration": "Release", "width": 1280, "height": 720,
                   "warmupFrames": 60, "measuredFramesPerRun": 1200, "repeats": 5,
                   "fixedDeltaSeconds": 1 / 60, "ui": False, "vsync": False,
                   "scope": "GPU timestamp total, including display; startup and transient AS scratch are separate"},
    "results": results,
}
destination = repo / "Docs/InlineRaytracingPerformance.json"
destination.write_text(json.dumps(output, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
lines = [
    "## Releaseの実測値",
    "",
    "1280×720、60フレーム準備後の固定経路1,200フレームを5反復。表は各条件6,000フレームを統合したGPU totalのmsです。UI・VSyncを無効にし、他サンプルと重なった反復は再測定します。",
    "",
    "| API | 条件 | 平均ms | 中央値ms | p95 ms | 反復平均の最小〜最大ms | AS初期構築ms |",
    "|---|---|---:|---:|---:|---:|---:|",
]
for row in results:
    lines.append(f'| {row["api"]} | {row["title"]} | {row["averageMs"]:.3f} | {row["medianMs"]:.3f} | {row["p95Ms"]:.3f} | {row["minimumRunAverageMs"]:.3f}〜{row["maximumRunAverageMs"]:.3f} | {row["asStartupAverageMs"]:.2f} |')
lines += [
    "",
    "AS初期構築はCPUでのデータ準備・GPU構築完了待ちを含む起動時の壁時計時間です。基本26の両方式はASを保持します。発展16／15の既存メモリ報告と基本26／27・発展17の追加Ray Scene Buffer報告は範囲が異なるため、APIや系列をまたいで合計メモリとして比較しません。",
    "",
    "影付き8灯と32灯は画の条件が変わる負荷比較です。全ライト／Tiledでは同じ8灯を影対象とします。発展15／16はStandard・512配置・64材質・textures・GPU Driven OFFを固定します。",
    "",
    "反復平均の幅は特にVulkanで大きく、発展15／16の平均差は反復間の変動より小さいため、この結果だけで実行方式の優劣を確定しません。発展17の全ライト8灯とTiled8灯では照明の探索・評価量が変わり、Tiled32灯では影付き灯数が変わります。",
    "",
    "| API | 系列 | AS記録値 MiB | ASの範囲 | 追加Ray Scene Buffer MiB |",
    "|---|---|---:|---|---:|",
]
for api in ("Direct3D12", "Vulkan"):
    basic = next(r for r in results if r["api"] == api and r["case"] == "26_InlineRaytracingShadows")["memory"]
    gallery = next(r for r in results if r["api"] == api and r["case"] == "Advanced16_BindlessInlineHybrid")["memory"]
    lines.append(f'| {api} | 基本26／27・発展17 | {basic["accelerationStructuresCommittedBytes"] / 1048576:.3f} | AS backing allocation | {basic["additionalInlineSceneBufferBytes"] / 1048576:.3f} |')
    lines.append(f'| {api} | 発展16／15 | {gallery["accelerationStructures"] / 1048576:.3f} | 既存レポートのAS結果サイズ | 個別のscene buffers記録を参照 |')
lines += [
    "",
    "[全測定値・環境・入力レポートSHA-256](InlineRaytracingPerformance.json)。GPUやシーンによる差を含み、RayQueryが常に高速になることを保証しません。",
    "[元のJSON・1,200行のCSV・環境](Measurements/InlineRaytracing)も保存しています。",
]
verification = repo / "Docs/InlineRaytracingVerification.md"
text = verification.read_text(encoding="utf-8")
begin, end = "<!-- INLINE_PERFORMANCE_BEGIN -->", "<!-- INLINE_PERFORMANCE_END -->"
section = begin + "\n" + "\n".join(lines) + "\n" + end
if begin in text:
    first = text.index(begin)
    last = text.index(end, first) + len(end)
    text = text[:first] + section + text[last:]
else:
    text += "\n" + section + "\n"
verification.write_text(text, encoding="utf-8")
print(f"Summarized {len(results) * 5} runs, {sum(r['measuredFrames'] for r in results)} measured frames")
