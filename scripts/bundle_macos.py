#!/usr/bin/env python3
"""Copy the core and non-system dylib closure, relocate and ad-hoc sign it.

For local distribution only. Signing/notarization and a license audit are still
required before distributing a release. Does not modify Homebrew originals.
"""
import argparse
from pathlib import Path
import plistlib
import shutil
import subprocess
import tempfile


def run(*args):
    return subprocess.check_output(args, text=True)


def dependencies(path):
    return [line.strip().split(" (", 1)[0] for line in run("otool", "-L", str(path)).splitlines()[1:]]


def bundle(source, destination, copied):
    source = source.resolve()
    target = destination / source.name
    if source in copied:
        return target
    if target.name in {p.name for p in copied}:
        raise RuntimeError(f"Two dependencies have the same basename: {source.name}")
    copied.add(source)
    shutil.copy2(source, target)
    target.chmod(0o755)
    run("install_name_tool", "-id", f"@rpath/{target.name}", str(target))
    for dep in dependencies(source):
        if dep.startswith(("/usr/lib/", "/System/Library/")):
            continue
        if dep.startswith("@loader_path/"):
            original = source.parent / dep.removeprefix("@loader_path/")
        elif dep.startswith("/"):
            original = Path(dep)
        elif dep == f"@rpath/{source.name}":
            continue  # library install name itself
        else:
            raise RuntimeError(f"Unresolved dependency {dep} in {source}")
        if original.resolve() == source:
            continue
        nested = bundle(original, destination, copied)
        run("install_name_tool", "-change", dep, f"@loader_path/{nested.name}", str(target))
    run("codesign", "--force", "--sign", "-", str(target))
    return target


def prepare_application(application, library, display_name):
    frameworks = application / "Contents/Frameworks"
    if not frameworks.is_dir():
        raise ValueError("Expected an already-built macOS .app")
    bundle(library, frameworks, set())
    if display_name:
        info_path = application / "Contents/Info.plist"
        with info_path.open("rb") as file:
            info = plistlib.load(file)
        info["CFBundleDisplayName"] = display_name
        info["CFBundleName"] = display_name
        with info_path.open("wb") as file:
            plistlib.dump(info, file)
    # Sign after dependency relocation and plist changes. Preserve Flutter's
    # entitlements/flags, so a hardened runtime doesn't lose its requirements.
    run("codesign", "--force", "--deep", "--preserve-metadata=entitlements,flags",
        "--sign", "-", str(application))
    run("codesign", "--verify", "--deep", "--strict", str(application))


def publish_application(source, output, library, display_name):
    if output.suffix != ".app" or source.resolve() == output.resolve():
        raise ValueError("Output must be a separate .app path")
    if output.is_symlink() or (output.exists() and not output.is_dir()):
        raise ValueError("Refusing to replace a symlink or a non-directory output")
    output.parent.mkdir(parents=True, exist_ok=True)
    # Build in a temporary directory. A failure leaves the last good app intact.
    with tempfile.TemporaryDirectory(prefix=".photo-package-", dir=output.parent) as directory:
        staging = Path(directory) / output.name
        run("ditto", str(source), str(staging))
        prepare_application(staging, library, display_name)
        backup = Path(directory) / "previous.app"
        if output.exists():
            output.rename(backup)
        try:
            staging.rename(output)
        except OSError:
            if backup.exists():
                backup.rename(output)
            raise


def archive_application(application, archive):
    if archive.suffix != ".zip" or archive.is_symlink():
        raise ValueError("Archive must be a regular .zip path")
    archive.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".photo-archive-", dir=archive.parent) as directory:
        temporary = Path(directory) / archive.name
        run("ditto", "-c", "-k", "--sequesterRsrc", "--keepParent", str(application), str(temporary))
        temporary.replace(archive)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("application", type=Path)
    parser.add_argument("--library", type=Path, default=Path("build/native/libphoto_core.dylib"))
    parser.add_argument("--output", type=Path, help="Publish a standalone .app without changing the build product")
    parser.add_argument("--archive", type=Path, help="Also create a ZIP containing the standalone .app")
    parser.add_argument("--display-name", help="Display name shown by Finder and the application menu")
    args = parser.parse_args()
    if not (args.application / "Contents/Frameworks").is_dir():
        parser.error("Expected an already-built macOS .app")
    application = args.output or args.application
    if args.output:
        publish_application(args.application, args.output, args.library, args.display_name)
    else:
        prepare_application(application, args.library, args.display_name)
    if args.archive:
        archive_application(application, args.archive)
        print(f"Archive: {args.archive}")
    print(f"Standalone macOS application: {application}")


if __name__ == "__main__":
    main()
