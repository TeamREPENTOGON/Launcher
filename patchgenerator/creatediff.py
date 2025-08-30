#!/usr/bin/env python

import argparse
import os
import hashlib
import json
import bsdiff4
from pathlib import Path
import shutil
import sys

def parse_args():
    description = """\
This script is used to generate a binary patchset allowing to convert a set of
files to a different one, either by patching existing files, creating new ones,
or deleting extraneous files.

The script works by taking all files in a source folder and binary diffing them
with the files with the same name in a target folder. The corresponding bsdiff
style patches are generated in an output folder.

Given an output folder, the script creates a file called manifest.json that
describes the operations to perform on the source files: patch them, delete
some, or create new ones. Patches are stored in a subfolder called patches.
The processing of manifest.json is to be done through a different process.
"""
    parser = argparse.ArgumentParser(prog="REPENTOGON binary diff generator",
                                     description=description)
    parser.add_argument("-n", "--dry-run", action="store_true",
                        help="Perform a dry run: only simulate execution")
    parser.add_argument("-y", "--yes", action="store_true",
                        help="If the output folder already exists, do not prompt "
                             "before deletion")
    parser.add_argument("-s", "--source", metavar="DIR",
                        help="Folder in which to find the files to downgrade",
                        default="source", type=Path)
    parser.add_argument("-t", "--target", metavar="DIR",
                        help="Folder in which to find the downgraded versions of the files",
                        default="target", type=Path)
    parser.add_argument("-o", "--output", metavar="DIR",
                        help="Folder in which to output the resulting patches",
                        default="output", type=Path)

    return parser.parse_args()


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

def handle_output_folder(output, force, dry):
    if output.exists():
        if not force:
            remove = input(f"Output folder {output} already exists, do you want "
                           "to remove it (Y/n) ? (Answering no will abort) ")
            while (remove != "y" and remove != "Y" and remove != "n" and
                   remove != "N" and remove != ""):
                remove = input(f"Please answer with y, Y, n, N or nothing (Y/n) ")

            if remove == "N" or remove == "n":
                sys.exit(0)
        print (f"Removing output folder {output}")
        if not dry:
            shutil.rmtree(output)

    print(f"Creating output folder {output}")
    if not dry:
        output.mkdir(parents=True)

def generate_common_files_patches(files, source, target, patch_dir, dry):
    to_patch = []

    for file in files:
        file_a = source / file
        file_b = target / file
        if file_hash(file_a) != file_hash(file_b):
            print (f"Generating bsdiff patch for {file}")
            patch_path = patch_dir / (file.replace("/", "__") + ".patch")

            if not dry:
                patch_path.parent.mkdir(parents=True, exist_ok=True)

            with open(file_a, 'rb') as fa, open(file_b, 'rb') as fb:
                a_data = fa.read()
                b_data = fb.read()
                patch = bsdiff4.diff(a_data, b_data)

                if not dry:
                    patch_path.write_bytes(patch)

            to_patch.append(file)

    return to_patch

def generate_created_files_patches(files, target, output, dry):
    result = {}

    for file in files:
        print (f"Generating creation patch for {file}")
        src = target / file
        dst = output / "create" / file

        if not dry:
            dst.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(src, dst)

        result[file] = str((Path("create") / file).as_posix())

    return result

def main(source, target, output, dry, force):
    if dry:
        print ("Performing a dry run")

    source = Path(source)
    target = Path(target)
    output = Path(output)

    handle_output_folder(output, force, dry)

    source_files = set(relative_files(source))
    target_files = set(relative_files(target))

    deleted = sorted(list(source_files - target_files))
    created = sorted(list(target_files - source_files))
    common = sorted(list(source_files & target_files))

    patch_dir = output / "patches"
    print (f"Creating patches folder {patch_dir}")
    if not dry:
        patch_dir.mkdir()

    to_patch = generate_common_files_patches(common, source, target, patch_dir, dry)
    created_patch = generate_created_files_patches(created, target, output, dry)

    patch_manifest = {
        "delete": deleted,
        "create": created_patch,
        "patch": to_patch
    }

    print ("Generating manifest file")
    if not dry:
        with open(output / "manifest.json", "w") as f:
            json.dump(patch_manifest, f, indent=2)

    print(f"Patch created at {output}")

if __name__ == "__main__":
    args = parse_args()
    main(args.source, args.target, args.output, args.dry_run, args.yes)
