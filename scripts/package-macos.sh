#!/bin/bash
# Build and validate a self-contained local macOS archive. No Apple account needed.
set -euo pipefail

if [[ "$(uname -s)" != Darwin ]]; then
    echo "This packaging script requires macOS." >&2
    exit 1
fi
# Qt invokes install_name_tool from PATH. Prefer Apple's tools over conda/cctools
# shims, which can fail to rewrite framework IDs or inject incompatible signatures.
apple_tool_bin="$(dirname "$(/usr/bin/xcrun --find install_name_tool)")"
export PATH="$apple_tool_bin:/usr/bin:/bin:/usr/sbin:/sbin:$PATH"

repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
package_build="${DND_PACKAGE_BUILD_DIR:-$repo_root/build-release}"
package_dist="${DND_PACKAGE_DIST_DIR:-$repo_root/dist}"
package_arch="${DND_PACKAGE_ARCHITECTURES:-$(uname -m)}"
qmake_bin="${DND_QMAKE:-$(command -v qmake)}"
qt_version="$("$qmake_bin" -query QT_VERSION)"
qt_bins="$("$qmake_bin" -query QT_INSTALL_BINS)"
qt_prefix="$("$qmake_bin" -query QT_INSTALL_PREFIX)"
qt_plugins="$("$qmake_bin" -query QT_INSTALL_PLUGINS)"
deploy_tool="${DND_MACDEPLOYQT:-$qt_bins/macdeployqt}"
python_bin="${DND_PACKAGE_PYTHON:-$(command -v python3)}"
for required in cmake codesign otool install_name_tool ditto curl; do
    command -v "$required" >/dev/null || { echo "Missing required command: $required" >&2; exit 1; }
done
[[ -x "$deploy_tool" ]] || { echo "macdeployqt is unavailable: $deploy_tool" >&2; exit 1; }
mkdir -p "$package_build" "$package_dist"
package_build="$(cd "$package_build" && pwd)"
package_dist="$(cd "$package_dist" && pwd)"
package_stage="$(mktemp -d "$package_dist/.package-stage.XXXXXX")"
trap 'rm -rf "$package_stage"' EXIT
app_name="Dungeoning a Dragon.app"
staged_app="$package_stage/$app_name"

cmake -S "$repo_root" -B "$package_build" \
    -DCMAKE_BUILD_TYPE=Release -DDND_BUILD_GUI=ON -DDND_BUILD_TESTS=OFF \
    "-DCMAKE_PREFIX_PATH=$qt_prefix" "-DCMAKE_OSX_ARCHITECTURES=$package_arch" \
    "-DCMAKE_OSX_DEPLOYMENT_TARGET=${DND_PACKAGE_DEPLOYMENT_TARGET:-14.0}"
cmake --build "$package_build" --config Release --target dnd_desktop --parallel "${DND_PACKAGE_JOBS:-4}"
ditto --norsrc "$package_build/$app_name" "$staged_app"
"$deploy_tool" "$staged_app" -always-overwrite -no-codesign -verbose=1
# macdeployqt deploys Cocoa by default; retain the matching headless platform for
# this package's command-line export and relocated acceptance workflow as well.
mkdir -p "$staged_app/Contents/PlugIns/platforms"
ditto --norsrc "$qt_plugins/platforms/libqoffscreen.dylib" "$staged_app/Contents/PlugIns/platforms/libqoffscreen.dylib"

# The official license texts are cached by the exact Qt version used to build.
license_cache="$package_build/license-cache/qt-$qt_version"
mkdir -p "$license_cache"
for license in LGPL-3.0-only GPL-3.0-only Qt-GPL-exception-1.0; do
    if [[ ! -s "$license_cache/$license.txt" ]]; then
        curl --fail --location --retry 2 --silent --show-error \
            "https://raw.githubusercontent.com/qt/qtbase/v$qt_version/LICENSES/$license.txt" \
            --output "$license_cache/$license.txt.part"
        mv "$license_cache/$license.txt.part" "$license_cache/$license.txt"
    fi
done

"$python_bin" - "$staged_app" "$repo_root" "$qmake_bin" "$qt_prefix" "$qt_version" "$license_cache" "$package_build" <<'PY'
import datetime, hashlib, json, pathlib, plistlib, re, shutil, subprocess, sys

app, repo, qmake, prefix, version, cached, build = sys.argv[1:]
app, repo, prefix, cached, build = map(pathlib.Path, (app, repo, prefix, cached, build))
contents = app / "Contents"
notices = contents / "Resources" / "Notices"
notices.mkdir(parents=True, exist_ok=True)
shutil.copy2(repo / "LICENSE", notices / "APPLICATION-BSD-3-Clause.txt")
icon_attribution = repo / "assets" / "icons" / "ATTRIBUTION.md"
if icon_attribution.exists():
    shutil.copy2(icon_attribution, notices / "APP-ICON-ATTRIBUTION.md")
qt_notices = notices / "Qt"
qt_notices.mkdir(exist_ok=True)
for text in cached.glob("*.txt"):
    shutil.copy2(text, qt_notices / text.name)
(qt_notices / "README.txt").write_text(
    "Qt " + version + "\nCopyright (C) The Qt Company Ltd. and other contributors.\n"
    "The application dynamically links Qt Core, Gui, Widgets, PrintSupport, and deployed dependencies.\n"
    "Qt essential libraries are used under the GNU LGPL version 3 option. Full LGPL and GPL texts accompany this notice.\n"
    "Qt source: https://download.qt.io/archive/qt/" + ".".join(version.split(".")[:2]) + "/" + version + "/submodules/\n"
    "Source repository: https://code.qt.io/qt/qtbase.git/?h=v" + version + "\n"
    "The installed Qt SPDX inventory and available third-party notices are included below.\n"
    "This local validation artifact is ad hoc signed; it is not a notarized public release.\n", encoding="utf-8")

# Preserve installed license texts, inventories, and recipes for Qt's actual Homebrew dependency closure.
# Additional source/build dependencies may appear in this provenance inventory; the Mach-O inventory below
# identifies precisely which runtime binaries were deployed.
qt_install = pathlib.Path(qmake).resolve().parent.parent
queue = [qt_install]
for candidate in [prefix / "opt" / "nlohmann-json", build / "_deps" / "json-src"]:
    if candidate.exists():
        queue.append(candidate.resolve())
seen = set()
provenance = []
while queue:
    installation = queue.pop(0).resolve()
    if installation in seen:
        continue
    seen.add(installation)
    receipt_path = installation / "INSTALL_RECEIPT.json"
    receipt = json.loads(receipt_path.read_text()) if receipt_path.exists() else {}
    name = installation.parent.name if receipt else installation.name
    destination = notices / "Dependencies" / name
    copied = []
    for path in installation.rglob("*"):
        if not path.is_file() or path.is_symlink():
            continue
        basename = path.name.lower()
        is_notice = basename.startswith(("license", "copying", "copyright", "notice", "authors", "credits"))
        is_metadata = basename == "install_receipt.json" or basename.endswith((".spdx", ".spdx.json")) or ".brew" in path.parts
        if is_notice or is_metadata:
            relative = path.relative_to(installation)
            target = destination / relative
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, target)
            copied.append(str(relative))
    provenance.append({"package": name, "installedVersion": installation.name, "copiedNotices": copied})
    for dependency in receipt.get("runtime_dependencies", []):
        dep = prefix / "opt" / dependency["full_name"].split("/")[-1]
        if dep.exists():
            queue.append(dep.resolve())

magic = {b"\xfe\xed\xfa\xce", b"\xce\xfa\xed\xfe", b"\xfe\xed\xfa\xcf", b"\xcf\xfa\xed\xfe", b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf"}
binaries = []
for path in contents.rglob("*"):
    if path.is_file() and not path.is_symlink():
        with path.open("rb") as stream:
            if stream.read(4) in magic:
                binaries.append(path)
if not binaries:
    raise SystemExit("No Mach-O executable found in package")

def output(*args):
    return subprocess.check_output(args, text=True)
def rewrite(*args):
    result = subprocess.run(args, text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if result.returncode:
        raise RuntimeError(result.stderr)
def rpaths(path):
    return re.findall(r"cmd LC_RPATH\s+cmdsize \d+\s+path (.*?) \(offset", output("otool", "-l", str(path)))
executable = contents / "MacOS" / "Dungeoning a Dragon"
frameworks = contents / "Frameworks"
def bundled_name(path):
    if path.is_relative_to(frameworks):
        return "@rpath/" + str(path.relative_to(frameworks))
    return "@executable_path/../" + str(path.relative_to(contents))
for path in binaries:
    # Qt's Homebrew framework IDs can retain their original Cellar/opt paths even
    # when macdeployqt has rewritten incoming references. Normalize those IDs too.
    identifiers = output("otool", "-D", str(path)).splitlines()
    if len(identifiers) > 1 and identifiers[1].strip():
        rewrite("install_name_tool", "-id", bundled_name(path), str(path))
    for line in output("otool", "-L", str(path)).splitlines():
        if not line.startswith("\t"):
            continue
        dependency = line.strip().split(" (compatibility version", 1)[0]
        if dependency.startswith("/") and not dependency.startswith(("/System/Library/", "/usr/lib/")):
            matches = [candidate for candidate in binaries if candidate.name == pathlib.Path(dependency).name]
            if len(matches) != 1:
                raise SystemExit("Cannot identify unique deployed copy for " + dependency)
            rewrite("install_name_tool", "-change", dependency, bundled_name(matches[0]), str(path))
for path in binaries:
    # macdeployqt can leave redundant build-machine search directories even after rewriting dependencies.
    for rpath in rpaths(path):
        if rpath.startswith("/") and not rpath.startswith(("/System/Library/", "/usr/lib/")):
            rewrite("install_name_tool", "-delete_rpath", rpath, str(path))
if "@executable_path/../Frameworks" not in rpaths(executable):
    rewrite("install_name_tool", "-add_rpath", "@executable_path/../Frameworks", str(executable))

def expanded(value, loader):
    return pathlib.Path(value.replace("@executable_path", str(executable.parent)).replace("@loader_path", str(loader.parent)))
minimums, inventory = [], []
executable_architectures = set(output("lipo", "-archs", str(executable)).split())
for path in sorted(binaries):
    load_commands = output("otool", "-l", str(path))
    linked = []
    for line in output("otool", "-L", str(path)).splitlines():
        if not line.startswith("\t"):
            continue
        dep = line.strip().split(" (compatibility version", 1)[0]
        if dep.startswith(("/System/Library/", "/usr/lib/")):
            linked.append(dep)
            continue
        if dep.startswith("/"):
            raise SystemExit("Unbundled absolute dependency: " + str(path) + " -> " + dep)
        candidates = []
        if dep.startswith("@rpath/"):
            suffix = dep[len("@rpath/"):]
            candidates.extend(expanded(p, path) / suffix for p in rpaths(path))
            candidates.extend(expanded(p, executable) / suffix for p in rpaths(executable))
        else:
            candidates.append(expanded(dep, path))
        if not any(p.exists() and p.resolve().is_relative_to(app.resolve()) for p in candidates):
            raise SystemExit("Unresolved bundled dependency: " + str(path) + " -> " + dep)
        linked.append(dep)
    mins = re.findall(r"\bminos ([\d.]+)", load_commands) + re.findall(r"cmd LC_VERSION_MIN_MACOSX\s+cmdsize \d+\s+version ([\d.]+)", load_commands)
    minimums.extend(mins)
    architectures = output("lipo", "-archs", str(path)).strip()
    if not executable_architectures.issubset(set(architectures.split())):
        raise SystemExit("Runtime architecture mismatch in " + str(path) + ": " + architectures)
    inventory.append({"path": str(path.relative_to(app)), "architectures": architectures, "minimumMacOS": mins, "dependencies": linked, "rpaths": rpaths(path)})
def version_tuple(value):
    return tuple(int(part) for part in value.split("."))
minimum = max(minimums, key=version_tuple)
plist_path = contents / "Info.plist"
with plist_path.open("rb") as stream:
    info = plistlib.load(stream)
info["LSMinimumSystemVersion"] = minimum
info["NSHighResolutionCapable"] = True
with plist_path.open("wb") as stream:
    plistlib.dump(info, stream)
manifest = {"applicationVersion": info["CFBundleShortVersionString"], "qtVersion": version, "minimumMacOS": minimum, "signing": "ad hoc; not notarized", "binaries": inventory, "dependencyProvenance": provenance,
    "sourceCommit": output("git", "-C", str(repo), "rev-parse", "HEAD").strip(),
    "sourceWorkingTreeDirty": bool(output("git", "-C", str(repo), "status", "--porcelain").strip())}
(contents / "Resources" / "package-inventory.json").write_text(json.dumps(manifest, indent=2) + "\n")
shutil.copy2(repo / "docs" / "versioning.md", notices / "APPLICATION-VERSION-POLICY.md")
(contents / "Resources" / "Notices" / "README.txt").write_text(
    "Dungeoning a Dragon v" + info["CFBundleShortVersionString"] + "\n"
    "Release policy: see APPLICATION-VERSION-POLICY.md.\n"
    "Application code: BSD-3-Clause; see APPLICATION-BSD-3-Clause.txt.\n"
    "Qt and its dependencies retain their own licenses and copyright notices.\n"
    "The bundled content packs each contain their own manifest license and source metadata.\n"
    "The bundled package-inventory.json records deployed libraries and build provenance.\n"
    "This package is intended for local validation. Public redistribution needs a separate source/license and signing release review.\n")

# Sign individual code first, then framework containers, then the outer application.
for path in binaries:
    subprocess.check_call(["codesign", "--force", "--sign", "-", "--timestamp=none", str(path)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
for framework in sorted(contents.rglob("*.framework"), key=lambda p: len(p.parts), reverse=True):
    subprocess.check_call(["codesign", "--force", "--sign", "-", "--timestamp=none", str(framework)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
subprocess.check_call(["codesign", "--force", "--sign", "-", "--timestamp=none", str(app)], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
print("Verified " + str(len(binaries)) + " bundled Mach-O files; minimum macOS " + minimum)
PY

codesign --verify --deep --strict --verbose=1 "$staged_app"
ditto -c -k --sequesterRsrc --keepParent "$staged_app" "$package_stage/Dungeoning-a-Dragon-macOS.zip"

# Exercise the actual archive after extraction outside the checkout and deployment location.
relocated_root="$(mktemp -d "${TMPDIR:-/tmp}/dnd-relocated.XXXXXX")"
trap 'rm -rf "$package_stage" "$relocated_root"' EXIT
ditto -x -k "$package_stage/Dungeoning-a-Dragon-macOS.zip" "$relocated_root"
smoke_output="$package_dist/packaged-smoke"
mkdir -p "$smoke_output"
(
    cd "$relocated_root"
    unset QT_PLUGIN_PATH QT_QPA_PLATFORM_PLUGIN_PATH QML2_IMPORT_PATH DYLD_LIBRARY_PATH DYLD_FRAMEWORK_PATH
    QT_QPA_PLATFORM=offscreen "$relocated_root/$app_name/Contents/MacOS/Dungeoning a Dragon" --smoke "$smoke_output"
)
codesign --verify --deep --strict --verbose=1 "$relocated_root/$app_name"

# Replace only the two named generated artifacts after all deployment checks pass.
if [[ -e "$package_dist/$app_name" ]]; then
    mv "$package_dist/$app_name" "$package_stage/previous.app"
fi
mv "$staged_app" "$package_dist/$app_name"
mv -f "$package_stage/Dungeoning-a-Dragon-macOS.zip" "$package_dist/Dungeoning-a-Dragon-macOS.zip"
(
    cd "$package_dist"
    shasum -a 256 Dungeoning-a-Dragon-macOS.zip > Dungeoning-a-Dragon-macOS.zip.sha256
)
echo "Validated local bundle: $package_dist/$app_name"
echo "Validated local archive: $package_dist/Dungeoning-a-Dragon-macOS.zip"
echo "Relocated smoke artifacts: $smoke_output"
