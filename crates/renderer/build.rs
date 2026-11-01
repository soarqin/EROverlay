fn main() {
    if std::env::var("CARGO_CFG_TARGET_OS").as_deref() != Ok("windows") {
        return;
    }
    let minhook = std::path::Path::new("../../third_party/minhook");
    cc::Build::new()
        .include(minhook)
        .files(["buffer.c", "hook.c", "trampoline.c", "hde64.c"].map(|p| minhook.join(p)))
        .warnings(false)
        .compile("er_rust_minhook");
    cc::Build::new()
        .cpp(true)
        .define("NOMINMAX", None)
        .std("c++20")
        .flag("/utf-8")
        .flag("/EHsc")
        .flag("/Gy")
        .include(minhook)
        .file("native/scene.cpp")
        .warnings(false)
        .compile("er_rust_scene");
    for p in [
        "native/scene.cpp",
        "native/scene.hpp",
        "native/checks.inc",
        "../../third_party/minhook",
    ] {
        println!("cargo:rerun-if-changed={p}");
    }
}
