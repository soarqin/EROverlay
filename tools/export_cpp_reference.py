"""Export a pinned C++ reference from a separate Git history into ignored target/.

The Rust build needs no C++ checkout. Optional differential tests use this export
via EROVERLAY_CPP_REFERENCE. An independent clone may pass --repository pointing
to a C++ checkout containing the requested commit; no fetch or checkout occurs.
"""

import argparse
import io
import json
from pathlib import Path
import subprocess
import zipfile

CPP_COMMIT = "afab7e14db2b62ae0bede980d5d0c96634321350"


def export_reference(repository: Path, revision: str, output: Path) -> str:
    commit = subprocess.check_output(
        ["git", "-C", str(repository), "rev-parse", "--verify", "--end-of-options", revision + "^{commit}"],
        text=True,
    ).strip()
    root = Path(__file__).resolve().parents[1]
    output = output.resolve()
    if not output.is_relative_to(root / "target") or output == root / "target":
        raise ValueError("Reference exports must be below this project's ignored target/ directory")
    if output.exists():
        raise FileExistsError(f"Reference directory already exists; choose another --output: {output}")
    data = subprocess.check_output(["git", "-C", str(repository), "-c", "core.autocrlf=false", "archive", "--format=zip", commit])
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        for entry in archive.infolist():
            if not (output / entry.filename).resolve().is_relative_to(output):
                raise ValueError(f"Archive entry escapes reference directory: {entry.filename}")
            if (entry.external_attr >> 16) & 0o170000 == 0o120000:
                raise ValueError(f"Symlink reference entries are unsupported: {entry.filename}")
        output.mkdir(parents=True)
        archive.extractall(output)
    (output / "reference.json").write_text(
        json.dumps({"commit": commit, "repository": str(repository.resolve())}, indent=2) + "\n",
        encoding="utf-8",
    )
    return commit


def main() -> None:
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repository", type=Path, default=root)
    parser.add_argument("--commit", default=CPP_COMMIT)
    parser.add_argument("--output", type=Path, default=root / "target/cpp-reference")
    args = parser.parse_args()
    try:
        commit = export_reference(args.repository, args.commit, args.output)
    except (subprocess.CalledProcessError, OSError, ValueError) as error:
        parser.exit(1, f"Reference export failed: {error}\nUse --repository with a C++ repository containing the requested commit.\n")
    print(f"C++ reference {commit}: {args.output.resolve()}")


if __name__ == "__main__":
    main()
