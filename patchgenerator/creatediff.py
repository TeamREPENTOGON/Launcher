import os
import hashlib
import json
import bsdiff4
from pathlib import Path
import shutil

def file_hash(path):
    with open(path, 'rb') as f:
        return hashlib.sha256(f.read()).hexdigest()

def relative_files(folder):
    files = []
    for root, _, filenames in os.walk(folder):
        for f in filenames:
            full = os.path.join(root, f)
            rel = os.path.relpath(full, folder)
            files.append(rel)
    return files

def main(folder_a, folder_b, folder_c):
    folder_a = Path(folder_a)
    folder_b = Path(folder_b)
    folder_c = Path(folder_c)

    if folder_c.exists():
        shutil.rmtree(folder_c)
    folder_c.mkdir(parents=True)

    files_a = set(relative_files(folder_a))
    files_b = set(relative_files(folder_b))

    deleted = sorted(list(files_a - files_b))
    created = sorted(list(files_b - files_a))
    common = sorted(list(files_a & files_b))

    to_patch = []

    patch_dir = folder_c / "patches"
    patch_dir.mkdir()

    for file in common:
        file_a = folder_a / file
        file_b = folder_b / file
        if file_hash(file_a) != file_hash(file_b):
            patch_path = patch_dir / (file.replace("/", "__") + ".patch")
            patch_path.parent.mkdir(parents=True, exist_ok=True)
            with open(file_a, 'rb') as fa, open(file_b, 'rb') as fb:
                a_data = fa.read()
                b_data = fb.read()
                patch = bsdiff4.diff(a_data, b_data)
                patch_path.write_bytes(patch)
            to_patch.append(file)

    patch_manifest = {
        "delete": deleted,
        "create": {},
        "patch": to_patch
    }

    for file in created:
        src = folder_b / file
        dst = folder_c / "create" / file
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
        patch_manifest["create"][file] = str((Path("create") / file).as_posix())

    with open(folder_c / "manifest.json", "w") as f:
        json.dump(patch_manifest, f, indent=2)

    print("Patch created at:", folder_c)

if __name__ == "__main__":
    main("source", "target", "output")
