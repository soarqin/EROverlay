from pathlib import Path
root=Path(__file__).resolve().parents[2]
source=(root/'crates/renderer/examples/gpu_perf.rs').read_text(encoding='utf-8')
source=source.replace('    let warp = args.iter().any(|s| s == "--warp");','    let retain = args.iter().any(|s| s == "--retain");\n    let warp = args.iter().any(|s| s == "--warp");')
source=source.replace('layer: LAYER_BELOW_UI,','layer: 0,')
source=source.replace('LAYER_BELOW_UI, Mesh','Mesh')
source=source.replace('RGB10A2 native UI transmittance','RGB10A2 AboveUI full-viewport')
source=source.replace('"hidden",\n            "stable_64_quads",\n            "large_rotated_transparent_1024_quads",\n            "resize_region",\n            "upload_burst",','"stable_64_quads",')
source=source.replace('clip_rect: [40.0, 40.0, 40.0 + size, 40.0 + size],','clip_rect: [0.0, 0.0, 1920.0, 1080.0],')
source=source.replace('circle: 1,','circle: 0,')
source=source.replace('renderer.render(&frame, visible, options)','renderer.render_with_group(&frame, visible, options, usize::from(retain))')
(root/'target/boss-presentation-final/copy_perf.rs').write_text(source,encoding='utf-8')
