fn main() {
    println!("cargo:rerun-if-env-changed=EROVERLAY_CPP_REFERENCE");
    #[cfg(feature = "runtime-read-fixture")]
    reference_fixture();
}
#[cfg(feature = "runtime-read-fixture")]
fn reference_fixture() {
    use std::path::PathBuf;
    assert_eq!(
        std::env::var("CARGO_CFG_TARGET_OS").as_deref(),
        Ok("windows"),
        "runtime-read-fixture requires a Windows target"
    );
    let root=PathBuf::from(std::env::var_os("EROVERLAY_CPP_REFERENCE").expect("runtime-read-fixture needs an explicitly exported C++ reference: run tools/export_cpp_reference.py --commit afab7e1, then set EROVERLAY_CPP_REFERENCE to the exported directory containing src/"));
    let required = [
        "src/util/mapstate.cpp",
        "src/util/mapstate.hpp",
        "src/util/gameflags.cpp",
        "src/util/gameflags.hpp",
        "src/util/paramreader.hpp",
        "src/nativeapi.h",
        "src/api.h",
        "src/util/gamescan.hpp",
        "src/util/gamebindings.hpp",
        "src/util/peimage.hpp",
        "src/util/signature.hpp",
        "src/util/memory.hpp",
        "src/util/eventflag.hpp",
        "src/util/minimap_scan_rules.inc",
        "src/boss/data.cpp",
        "src/boss/data.hpp",
        "deps/nlohmann_json/include/nlohmann/json.hpp",
        "deps/pattern16/Pattern16.h",
    ];
    for relative in required {
        let path = root.join(relative);
        assert!(
            path.is_file(),
            "missing C++ reference {}; export afab7e1 before enabling runtime-read-fixture",
            path.display()
        );
        println!("cargo:rerun-if-changed={}", path.display());
    }
    println!("cargo:rerun-if-changed=native/read_checks.cpp");
    cc::Build::new()
        .cpp(true)
        .file("native/read_checks.cpp")
        .file(root.join("src/util/mapstate.cpp"))
        .file(root.join("src/util/gameflags.cpp"))
        .file(root.join("src/boss/data.cpp"))
        .include(root.join("src"))
        .include(root.join("deps/pattern16"))
        .include(root.join("deps/nlohmann_json/include"))
        .flag("/std:c++latest")
        .flag("/utf-8")
        .flag("/EHsc")
        .define("NOMINMAX", None)
        .compile("eroverlay_cpp_read_fixture");
}
