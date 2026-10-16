"""Record reproducible EXE coverage with the production UI_SCENE resolver."""
import argparse
from datetime import date
import hashlib
import json
from pathlib import Path
import subprocess
import sys


def digest(path, *, source=False):
    if source:
        # Git may check text out with LF or CRLF. Hash the same canonical text
        # on both so evidence remains comparable after a fresh checkout.
        return hashlib.sha256(path.read_bytes().replace(b"\r\n", b"\n")).hexdigest()
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--output", type=Path, default=Path("build/ui-scene/evidence.json"))
    parser.add_argument("--ui-compositor", type=Path, nargs=2, metavar=("VERTEX", "PIXEL"), help="Optional SM6 test vertex shader and locally extracted GXSimple_BlendUI_SDR2SDR_F.ppo")
    args = parser.parse_args()
    root = Path(__file__).resolve().parent.parent
    scanner = root / "build/ui-scene/ui_scene_scan_verify.exe"
    renderer = root / "build/ui-scene/ui_scene_render_verify.exe"
    records = []
    for exe in sorted(args.archive.glob("*/Game/eldenring.exe"), key=lambda p: tuple(int(v) for v in p.parents[1].name.split("."))):
        result = subprocess.run([str(scanner), str(exe)], capture_output=True, text=True, check=True, cwd=root)
        columns = result.stdout.strip().split("\t")
        if len(columns) != 3 or columns[1] not in {"PASS", "UNSUPPORTED"}:
            raise RuntimeError(result.stdout)
        records.append({"version": columns[0], "sha256": digest(exe), "status": columns[1], "detail": columns[2]})
        print(columns[0], columns[1], flush=True)
    if not records:
        raise RuntimeError("No */Game/eldenring.exe in archive")
    reference = args.archive / "1.16/Game/eldenring.exe"
    negative = subprocess.run([str(scanner), str(reference), "--negative"], capture_output=True, text=True, check=True, cwd=root)
    checks = {}
    runs = [("native_rgb10a2", renderer, [])]
    if args.ui_compositor:
        runs.append(("native_sdr_game_compositor", renderer, ["--ui-compositor", *(str(p.resolve()) for p in args.ui_compositor)]))
    runs.extend((name, renderer.parent / (name + "_verify.exe"), []) for name in ["plugin_thread", "minimap_visibility", "minimap_map"])
    runs.append(("offscreen", renderer.parent / "minimap_texture_verify.exe", ["--offscreen"]))
    for name, executable, extra in runs:
        run = subprocess.run([str(executable), *extra], capture_output=True, text=True, cwd=root)
        if run.returncode:
            print(run.stdout, end="", file=sys.stderr)
            print(run.stderr, end="", file=sys.stderr)
            run.check_returncode()
        checks[name] = {"exit_code": run.returncode, "output": run.stdout.splitlines(), "diagnostics": run.stderr.splitlines()}
    evidence = {
        "date": date.today().isoformat(),
        "liber_reference_commit": "d8ae5c92719adca4344d8d902b63286b807f562b",
        "reference_game_version": "1.16",
        "scope": "UI_SCENE is the only renderer. Static PE contracts, two-hook MinHook teardown, rejection without fallback, controlled D3D12 layer pixels/lifetimes, native SDR compositor opacity and CPU minimap visibility. User confirmed in-game tests passed before removing the DXGI renderer.",
        "renderer": "ui_scene",
        "renderer_selection_config": False,
        "dxgi_hooks": False,
        "startup_validation_failure": "no rendering; no fallback",
        "review_fixes": {
            "unsupported_image": "retry only while the window is missing; native validation failure returns once",
            "plugin_destroy_calls": {"baseline": 3, "fixed": 1, "scenario": "destroy before creation, then create and destroy twice"},
            "pending_gpu_submissions": {"swapchain_buffers": 3, "queued_game_frames": 3, "baseline": 3, "fixed": 6},
            "upper_layer_reservation": "when only one ring slot is free, skip the lower stage and submit the upper stage once",
            "submission_ring_capacity": "2 * swapchain BufferCount, one fence shared by allocator and ImGui buffers",
            "offscreen_textures_per_context": 1,
            "callback_only_draw_lists": "preserve callbacks in each layer, including frames without geometry",
            "removed_state": ["layer filter/All mode", "unused plugin API version storage", "snapshot onGUI", "second ImGui fence array", "offscreen backbuffer ring", "underlay context wrapper", "Hooking forwarding methods"],
        },
        "user_reported_validation": {"date": "2026-10-16", "result": "All tests passed; user requested removal of the DXGI path and configuration."},
        "native_main_rtv_format": "R10G10B10A2_UNORM",
        "native_below_ui_rtv_formats": ["R10G10B10A2_UNORM", "R8G8B8A8_UNORM"],
        "native_hooks": ["draw queue submit", "native UI compositor boundary recording"],
        "plugin_layers": {"default_and_legacy": "above_ui", "minimap_optional_export": "below_ui", "plugin_exports_abi_changed": False, "renders_per_plugin_per_frame": 1},
        "minimap_game_auto_hide": ["loading_or_logo", "full_screen_world_map"],
        "minimap_display_state_validation": {
            "constructed_rtti": ["CS::CSMenuManImp", "CS::LoadingScreenData"],
            "visible_context_requires": ["published menu owner", "published owner view", "validated root RTTI", "unchanged object identities after read"],
            "unvalidated_state": "hide resource text, border and map immediately",
            "startup_regressions": ["allocated player with zero-filled menu", "LoadingScreenData not constructed", "menu owner not published", "owner view not published",
                                    "root menu constructor in progress", "loading data invalidated after gameplay"],
            "baseline_vertices_per_failed_case": 48,
            "fixed_vertices_per_hidden_case": 0,
            "historical_layouts_tested": ["1.02", "1.03-1.10.1", "1.12+"],
        },
        "offscreen_rtv_format": "R8G8B8A8_UNORM",
        "native_ui_alpha_contract": {
            "native_sdr_output": "pow(scene.rgb * ui.a + ui.rgb, outputGammaForSDR)",
            "ui_alpha": "scene transmittance",
            "lower_stage": "One reusable RGBA8 layer, then ui.rgb += lower.rgb * ui.a; ui.a *= 1 - lower.a",
            "controlled_sdr_gamma": 1,
            "alpha_cases": [100, 90, 50, 20, 0],
            "native_ui_transmittance_cases": [255, 128, 0],
            "game_shader_sha256": digest(args.ui_compositor[1]) if args.ui_compositor else None,
            "sm6_test_vertex_sha256": digest(args.ui_compositor[0]) if args.ui_compositor else None,
        },
        "hdr_test_scope": "User tested HDR with format 24 for both main RTV and swap chain; display colors and brightness are not measured by the log.",
        "user_reported_hdr_log": {
            "date": "2026-10-16",
            "width": 3440, "height": 1440,
            "main_rtv_format": 24, "swapchain_format": 24, "offscreen_format": 28,
            "first_draw_vertices": 350, "first_draw_indices": 1473,
        },
        "source_sha256_encoding": "raw UTF-8 bytes with CRLF normalized to LF",
        "source_sha256": {p: digest(root / p, source=True) for p in ["src/util/uiscene_patterns.inc", "src/util/uiscenescan.hpp", "src/uiscene.cpp", "src/d3drenderer.cpp",
                                                       "deps/imgui/imgui_impl_dx12.cpp", "deps/imgui/imgui_impl_dx12.h", "src/uiscene.hpp", "src/d3drenderer.hpp",
                                                       "src/drawlayers.hpp", "src/plugin.cpp", "src/plugin.hpp", "src/api.h", "src/api.cpp", "src/nativeapi.h", "src/textureupload.cpp",
                                                       "src/dllmain.cpp", "src/hooking.cpp", "src/hooking.hpp",
                                                       "src/util/mapstate.cpp", "src/util/mapstate.hpp", "src/gamefiles.cpp", "src/gamefiles.hpp",
                                                       "src/minimap/data.cpp", "src/minimap/render.cpp", "src/minimap/dllmain.cpp",
                                                       "tools/ui_scene_scan_verify.cpp", "tools/ui_scene_render_verify.cpp", "tools/ui_scene_compositor_verify.hlsl", "tools/minimap_visibility_verify.cpp",
                                                       "tools/plugin_thread_verify.cpp", "tools/minimap_map_verify.cpp", "tools/minimap_texture_verify.cpp", "tools/build_ui_scene_checks.bat", "tools/ui_scene_evidence.py"]},
        "archive_directories": len(records),
        "independent_sha256": len({r["sha256"] for r in records}),
        "resolved_directories": sum(r["status"] == "PASS" for r in records),
        "unsupported_directories": sum(r["status"] == "UNSUPPORTED" for r in records),
        "images": records,
        "negative_and_aslr_checks": negative.stdout.splitlines(),
        "controlled_integration_checks": checks,
    }
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(evidence, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"evidence: {args.output}", flush=True)


if __name__ == "__main__":
    main()
