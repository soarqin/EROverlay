"""Exercise dist.bat with real CMake/MSVC and 7-Zip in a temporary project."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile


def main():
    source = Path(__file__).resolve().parents[1] / "dist.bat"
    with tempfile.TemporaryDirectory(prefix="EROverlay dist check ") as directory:
        root = Path(directory)

        def write(relative, content):
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(content, encoding="utf-8")

        def run(arguments, *, success=True, env=None):
            result = subprocess.run(arguments, cwd=root.parent, env=env, capture_output=True, text=True, errors="replace")
            if (result.returncode == 0) != success:
                raise AssertionError(result.stdout + result.stderr)
            return result

        def dist(*, success=True, env=None):
            result = run(["cmd", "/d", "/c", "call", str(root / "dist.bat")], success=success, env=env)
            assert ("Created dist\\Boss.zip and dist\\Minimap.zip." in result.stdout) == success

        def seed_stale_packages():
            for name in ("Boss", "Minimap"):
                write(f"dist/{name}/stale.txt", "removed staging file")
                with zipfile.ZipFile(root / f"dist/{name}.zip", "w") as archive:
                    archive.writestr("stale.txt", "removed archive entry")

        def check_packages():
            for name in ("Boss", "Minimap"):
                with zipfile.ZipFile(root / f"dist/{name}.zip") as archive:
                    files = {entry.filename for entry in archive.infolist() if not entry.is_dir()}
                    expected = {"EROverlay.dll", f"overlays/{name}.dll", "README.md", "README_CN.md", "LICENSE"}
                    expected.update(f"{folder}/{config}.ini" for folder in ("configs", "configs_CN")
                                    for config in ("common", "input", "style", name))
                    expected.update(("docs/boss-revival.md", "docs/boss-revival.evidence.json", "data/boss.json")
                                    if name == "Boss" else ("LICENSE_bcdec.txt",))
                    assert {path.lower() for path in files} == {path.lower() for path in expected}, files
                    for relative in files:
                        data = archive.read(relative)
                        assert data == (root / "dist" / name / relative).read_bytes(), relative
                        if relative.endswith(".dll"):
                            assert data == (root / "build/bin" / relative).read_bytes(), relative
                            assert b"fixture-Release" in data and b"fixture-Debug" not in data, relative

        shutil.copyfile(source, root / "dist.bat")
        write("fixture.c", '__declspec(dllexport) const char *fixtureConfig(void) { return "fixture-" BUILD_CONFIG; }\n')
        write("zip_fail.c", "int main(void) { return 23; }\n")
        write("CMakeLists.txt", '''cmake_minimum_required(VERSION 3.24)
if(EXISTS "${CMAKE_SOURCE_DIR}/fail-configure")
    message(FATAL_ERROR "Intentional configure failure")
endif()
project(DistFixture C)
foreach(name EROverlay Boss Minimap)
    add_library(${name} SHARED fixture.c)
    target_compile_definitions(${name} PRIVATE BUILD_CONFIG="$<CONFIG>")
    set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/$<0:>")
    if(NOT name STREQUAL "EROverlay")
        set_target_properties(${name} PROPERTIES RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin/overlays/$<0:>")
    endif()
endforeach()
add_executable(failing_zip zip_fail.c)
set_target_properties(failing_zip PROPERTIES OUTPUT_NAME 7z RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/zip-fail/$<0:>")
if(EXISTS "${CMAKE_SOURCE_DIR}/fail-build")
    add_custom_target(fail ALL COMMAND "${CMAKE_COMMAND}" -E false)
endif()
''')
        for name in ("Boss", "Minimap"):
            for filename in ("README.md", "README_CN.md"):
                write(f"src/{name}/{filename}", f"{name} {filename}")
        for folder in ("configs", "configs_CN"):
            for config in ("common", "input", "style", "Boss", "Minimap"):
                write(f"{folder}/{config}.ini", f"{folder} {config}")
        for relative in ("LICENSE", "deps/bcdec/LICENSE", "docs/boss-revival.md",
                         "docs/boss-revival.evidence.json", "src/Boss/data/boss.json"):
            write(relative, relative)

        seed_stale_packages()
        for failure in ("fail-configure", "fail-build"):
            write(failure, "")
            dist(success=False)
            assert (root / "dist/Boss/stale.txt").is_file()
            with zipfile.ZipFile(root / "dist/Boss.zip") as archive:
                assert archive.namelist() == ["stale.txt"]
            (root / failure).unlink()
        print("PASS: configure/build failures stop before packaging", flush=True)

        dist()
        check_packages()
        run(["cmake", "--build", str(root / "build"), "--config", "Debug"])
        assert b"fixture-Debug" in (root / "build/bin/EROverlay.dll").read_bytes()
        seed_stale_packages()
        dist()
        check_packages()
        print("PASS: Debug DLLs replaced with Release; stale staging/ZIP entries removed; launch directory ignored", flush=True)

        (root / "configs/Boss.ini").unlink()
        dist(success=False)
        assert not (root / "dist/Boss.zip").exists()
        assert not (root / "dist/Minimap.zip").exists()
        write("configs/Boss.ini", "restored config")
        print("PASS: missing package input stops without creating archives", flush=True)

        env = os.environ.copy()
        env["PATH"] = str(root / "build/zip-fail") + os.pathsep + env["PATH"]
        dist(success=False, env=env)
        assert not (root / "dist/Boss.zip").exists()
        assert not (root / "dist/Minimap.zip").exists()
        print("PASS: compression failure propagates to the caller", flush=True)


if __name__ == "__main__":
    main()
