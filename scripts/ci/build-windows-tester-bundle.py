#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Build the Windows tester zip (ADR-1515) on a hosted Windows runner.

    build-windows-tester-bundle.py <output-dir>

Runs on the hosted Windows runner of .github/workflows/windows-tester-bundle.yml,
inside an MSVC developer environment (vcvarsall, which sets VCToolsRedistDir) with
meson, ninja, xxd and, on x64, nasm on PATH. Never on a workstation.

Environment (all required):
  VMAFX_ARCH              x64 or arm64 (the zip's architecture; checked against the host)
  PBS_URL, PBS_SHA256     python-build-standalone install_only_stripped archive and SHA-256
  PBS_FULL_URL, PBS_FULL_SHA256
                          the same release's `full` archive, read only for its licence
                          texts (python/licenses, PYTHON.json; ADR-1503)
  VMAF_RESOURCE_COMMIT    Netflix/vmaf_resource commit the fixtures come from
  VMAFX_SOURCE_COMMIT, VMAFX_SOURCE_REF, VMAFX_RECIPE_COMMIT, VMAFX_IMAGE_TAG

Result in <output-dir>: vmafx-tester-windows-<arch>-<tag>.zip, its .sha256,
report.json (the zip's own report run on this runner) and bundle-files.txt. The zip
carries licenses/ (THIRD_PARTY_NOTICES.txt and every licence text) and is not packed
when a file has no recorded licence (ADR-1503). The C and C++ runtime is linked
statically (/MT); the only Microsoft DLLs in the zip are the interpreter's
vcruntime140*.dll, replaced by the unmodified copies of this runner's Visual Studio
redistributable folder.
"""

from __future__ import annotations

import filecmp
import hashlib
import importlib
import io
import json
import os
import platform
import shutil
import subprocess
import sys
import tarfile
import urllib.request
import zipfile
from pathlib import Path

REQUIRED = ("VMAFX_ARCH", "PBS_URL", "PBS_SHA256", "PBS_FULL_URL", "PBS_FULL_SHA256",
            "VMAF_RESOURCE_COMMIT", "VMAFX_SOURCE_COMMIT", "VMAFX_SOURCE_REF",
            "VMAFX_RECIPE_COMMIT", "VMAFX_IMAGE_TAG")  # fmt: skip
ARCHES = {"x64": "AMD64", "arm64": "ARM64"}  # VMAFX_ARCH -> PROCESSOR_ARCHITECTURE
IMAGE_DIR = Path("tools/rc1-tester/image")
UNIT_TESTS = IMAGE_DIR / "unit-tests-windows.txt"
RESOURCE_URL = (
    "https://raw.githubusercontent.com/Netflix/vmaf_resource/{commit}/python/test/resource/{file}"
)
MESON_OPTIONS = (
    "--buildtype=release", "--default-library=static", "-Db_vscrt=mt", "-Db_lto=false",
    "-Denable_float=true", "-Denable_tests=true", "-Denable_docs=false", "-Denable_cuda=false",
    "-Denable_sycl=false", "-Denable_hip=false", "-Denable_metal=disabled",
    "-Denable_dnn=disabled",
)  # fmt: skip
# The interpreter keeps the standard library the report uses; these go (as in the macOS
# bundle: headers, import libraries, pip, tests, IDLE, Tcl/Tk and the zlib1.dll only
# Tcl links, the test extension modules, the GUI launcher).
PRUNE_DIRS = ("include", "libs", "Scripts", "tcl", "Lib/test", "Lib/idlelib", "Lib/tkinter",
              "Lib/turtledemo", "Lib/ensurepip", "Lib/lib2to3")  # fmt: skip
PRUNE_GLOBS = ("DLLs/tcl*.dll", "DLLs/tk*.dll", "DLLs/_tkinter.pyd", "DLLs/zlib1.dll",
               "DLLs/_test*.pyd", "DLLs/_ctypes_test.pyd", "pythonw.exe",
               "Lib/site-packages/pip", "Lib/site-packages/pip-*.dist-info")  # fmt: skip
VC_RUNTIME_GLOB = "vcruntime140*.dll"
DOWNLOAD_LIMIT = 512 * 1024 * 1024
USAGE_ARGS = 2
GOLDEN_NOT_APPLICABLE = (
    "the Python golden stack is not in the Windows zip; the Netflix pairs are covered by "
    "the dispatch and reference checks"
)
TIMEOUT = 600
ZIP_TIME = (1980, 1, 1, 0, 0, 0)


class BuildError(RuntimeError):
    """A build input or step is wrong; main() prints it and exits 1."""


def step(message: str) -> None:
    print(f"\n== {message}", flush=True)


def run(argv: list[str], *, cwd: Path | None = None, env: dict[str, str] | None = None,
        timeout: float = 3 * 3600, capture: bool = False) -> str:  # fmt: skip
    """Run a command, failing the build on a non-zero exit; its stdout when captured."""
    print("+ " + " ".join(argv), flush=True)
    # argv is assembled by this script from fixed program names and its own paths.
    result = subprocess.run(argv, cwd=cwd, env=env, timeout=timeout, check=False,  # noqa: S603
                            capture_output=capture, text=True)  # fmt: skip
    if result.returncode != 0:
        detail = (result.stderr or "")[-2000:] if capture else ""
        raise BuildError(f"{argv[0]} exited {result.returncode}\n{detail}")
    return result.stdout or ""


def require_environment(env: dict[str, str]) -> dict[str, str]:
    missing = [name for name in REQUIRED if not env.get(name)]
    if missing:
        raise BuildError("missing environment: " + ", ".join(missing))
    if env["VMAFX_ARCH"] not in ARCHES:
        raise BuildError(f"VMAFX_ARCH must be one of {sorted(ARCHES)}")
    return {name: env[name] for name in REQUIRED}


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def download(url: str, target: Path, expected: str) -> Path:
    """Fetch a file over HTTPS, refusing it when its SHA-256 is not the expected one."""
    if not url.startswith("https://"):
        raise BuildError(f"{url} is not an https URL")
    target.parent.mkdir(parents=True, exist_ok=True)
    headers = {"User-Agent": "vmafx-tester-build"}
    request = urllib.request.Request(url, headers=headers)  # noqa: S310 (https only, above)
    response = urllib.request.urlopen(request, timeout=TIMEOUT)  # noqa: S310
    with response, target.open("wb") as out:
        for _ in range(DOWNLOAD_LIMIT // 65536 + 1):
            chunk = response.read(65536)
            if not chunk:
                break
            out.write(chunk)
        else:
            raise BuildError(f"{url} is larger than {DOWNLOAD_LIMIT} bytes")
    if sha256(target) != expected:
        raise BuildError(f"{url}: SHA-256 {sha256(target)} is not the pinned {expected}")
    return target


def prepare_build(*args: str, capture: bool = False) -> str:
    return run([sys.executable, str(IMAGE_DIR / "prepare_build.py"), *args], capture=capture)


def compiler_line(build: Path) -> str:
    """The C compiler Meson used, from its introspection (MSVC's cl has no --version)."""
    data = json.loads(run(["meson", "introspect", "--compilers", str(build)], capture=True))
    compiler = data["host"]["c"]
    return str(compiler.get("full_version") or f"{compiler['id']} {compiler['version']}")


def configure_and_build(build: Path) -> None:
    step("configure and build (MSVC, static libvmaf, static C runtime /MT, no GPU, no DNN)")
    env = {**os.environ, "CFLAGS": "/experimental:c11atomics",
           "CXXFLAGS": "/experimental:c11atomics"}  # fmt: skip
    run(["meson", "setup", str(build), "core", *MESON_OPTIONS], env=env)
    targets = prepare_build("select", str(build), str(UNIT_TESTS), capture=True).split()
    run(["ninja", "-C", str(build), "tools/vmaf.exe", *targets])


def stage(build: Path, bundle: Path, env: dict[str, str]) -> None:
    step("stage the bundle")
    (bundle / "build" / "tools").mkdir(parents=True)
    shutil.copy2(build / "tools" / "vmaf.exe", bundle / "build" / "tools" / "vmaf.exe")
    prepare_build("stage", str(build), str(UNIT_TESTS), str(bundle))
    shutil.copytree("tools/rc1-tester/src", bundle / "tester" / "src",
                    ignore=shutil.ignore_patterns("__pycache__"))  # fmt: skip
    shutil.copy2("tools/rc1-tester/vmaf-tester-report", bundle / "tester")
    shutil.copy2(IMAGE_DIR / "fixtures.json", bundle / "image" / "fixtures.json")
    (bundle / "image" / "package-arch.txt").write_text(ARCHES[env["VMAFX_ARCH"]] + "\n")
    shutil.copy2(IMAGE_DIR / "windows" / "run.cmd", bundle / "run.cmd")
    shutil.copy2(IMAGE_DIR / "windows" / "README.txt", bundle / "README.txt")


def fetch_fixtures(bundle: Path, out: Path, commit: str) -> None:
    step("fixtures (pinned commit, SHA-256 checked)")
    lines = prepare_build("fixtures", str(IMAGE_DIR / "fixtures.sha256"),
                          str(IMAGE_DIR / "fixtures.json"), capture=True)  # fmt: skip
    (out / "fixtures-needed.sha256").write_text(lines)
    resource = bundle / "python" / "test" / "resource"
    for line in lines.splitlines():
        digest, name = line.split()
        download(RESOURCE_URL.format(commit=commit, file=name), resource / name, digest)


def remove(path: Path) -> None:
    if path.is_dir():
        shutil.rmtree(path)
    elif path.exists():
        path.unlink()


def prune_runtime(runtime: Path) -> None:
    """Drop what the report does not use; never edit a file that stays."""
    for rel in PRUNE_DIRS:
        remove(runtime / rel)
    for pattern in PRUNE_GLOBS:
        for path in sorted(runtime.glob(pattern)):
            remove(path)
    for path in sorted(runtime.rglob("__pycache__"), reverse=True):
        remove(path)
    for path in sorted(runtime.rglob("*.pyc")):
        remove(path)


def redist_crt_dir(redist_root: Path, arch: str) -> Path:
    """`<VCToolsRedistDir>\\<arch>\\Microsoft.VC14x.CRT`: the one folder ADR-1503 lets a
    Microsoft runtime DLL come from (never debug_nonredist)."""
    found = sorted((redist_root / arch).glob("Microsoft.VC14*.CRT"))
    if len(found) != 1:
        raise BuildError(f"{redist_root / arch} holds {len(found)} Microsoft.VC14*.CRT folders")
    return found[0]


def replace_vc_runtime(runtime: Path, crt: Path) -> list[dict[str, str]]:
    """Replace the interpreter's vcruntime140*.dll with the unmodified copies of this
    runner's Visual Studio redistributable folder; returns what was copied."""
    copied = []
    for shipped in sorted(runtime.glob(VC_RUNTIME_GLOB)):
        source = crt / shipped.name
        if not source.is_file():
            raise BuildError(f"{shipped.name} is not in {crt}")
        shutil.copyfile(source, shipped)
        if not filecmp.cmp(source, shipped, shallow=False):
            raise BuildError(f"{shipped.name} differs from {source} after the copy")
        copied.append({"file": shipped.relative_to(runtime.parent).as_posix(),
                       "source": str(source), "sha256": sha256(shipped)})  # fmt: skip
    if not copied:
        raise BuildError(f"the interpreter in {runtime} ships no {VC_RUNTIME_GLOB}")
    return copied


def install_interpreter(bundle: Path, out: Path, env: dict[str, str]) -> Path:
    step("interpreter (python-build-standalone, SHA-256 checked)")
    archive = download(env["PBS_URL"], out / "pbs.tar.gz", env["PBS_SHA256"])
    with tarfile.open(archive) as tar:
        tar.extractall(out / "pbs", filter="data")
    shutil.move(out / "pbs" / "python", bundle / "runtime")
    # The interpreter archive is an input, never a product of this workflow.
    remove(out / "pbs")
    remove(archive)
    runtime = bundle / "runtime"
    prune_runtime(runtime)
    # VCToolsRedistDir of vcvarsall; Windows environment names ignore case.
    crt = redist_crt_dir(Path(os.environ["VCTOOLSREDISTDIR"]), env["VMAFX_ARCH"])
    record = {"redist_dir": str(crt), "files": replace_vc_runtime(runtime, crt)}
    (bundle / "image" / "msvc-redist.json").write_text(json.dumps(record, indent=1) + "\n")
    return runtime / "python.exe"


def zstd_decompress(data: bytes) -> bytes:
    """Python 3.14's compression.zstd, else the zstd program."""
    try:
        module = importlib.import_module("compression.zstd")
    except ImportError:
        module = None
    if module is not None:
        decompressed: bytes = module.decompress(data)
        return decompressed
    program = shutil.which("zstd")
    if program is None:
        raise BuildError("neither compression.zstd (Python 3.14) nor a zstd program exists")
    return subprocess.run([program, "-dc"], input=data, capture_output=True, check=True,  # noqa: S603
                          timeout=TIMEOUT).stdout  # fmt: skip


def pbs_licence_texts(out: Path, texts: Path, env: dict[str, str]) -> None:
    """python/licenses and PYTHON.json of the release's full archive (ADR-1503 rule 4)."""
    archive = download(env["PBS_FULL_URL"], out / "pbs-full.tar.zst", env["PBS_FULL_SHA256"])
    target = texts / "python-build-standalone"
    target.mkdir(parents=True, exist_ok=True)
    with tarfile.open(fileobj=io.BytesIO(zstd_decompress(archive.read_bytes()))) as tar:
        for member in tar.getmembers():
            name = member.name
            if member.isfile() and (
                name.startswith("python/licenses/") or name == "python/PYTHON.json"
            ):
                handle = tar.extractfile(member)
                if handle is None:
                    raise BuildError(f"cannot read {name} from the full archive")
                (target / Path(name).name).write_bytes(handle.read())
    remove(archive)
    if not (target / "PYTHON.json").is_file():
        raise BuildError("the full archive holds no python/PYTHON.json")


def licensing(*args: str) -> None:
    run([sys.executable, "-I", "-B", str(IMAGE_DIR / "licensing.py"), *args])


def bundle_python_version(python: Path) -> str:
    code = "import platform; print(platform.python_version())"
    return run([str(python), "-I", "-c", code], capture=True).strip()


def info_environment(build: Path, env: dict[str, str]) -> dict[str, str]:
    """What `prepare_build.py info` writes into image/build-info.json."""
    keys = ("VMAFX_SOURCE_COMMIT", "VMAFX_SOURCE_REF", "VMAFX_RECIPE_COMMIT", "VMAFX_IMAGE_TAG")
    runner = f"{os.environ.get('IMAGEOS', 'windows')}-{os.environ.get('IMAGEVERSION', 'runner')}"
    libc = "MSVC runtime and Universal CRT, linked statically (/MT)"
    return {
        **os.environ,
        **{key: env[key] for key in keys},
        "VMAFX_BUILT_BY_WORKFLOW": os.environ.get("VMAFX_BUILT_BY_WORKFLOW", "true"),
        "VMAFX_ARTIFACT_KIND": "windows-zip",
        "VMAFX_BASE_IMAGE": runner,
        "VMAFX_COMPILER": compiler_line(build),
        "VMAFX_LIBC": f"{libc}; built on Windows {platform.version()}",
        "VMAFX_NOT_APPLICABLE": json.dumps({"golden": GOLDEN_NOT_APPLICABLE}),
    }


def records(bundle: Path, build: Path, python: Path, env: dict[str, str]) -> None:
    step("bundle metadata and references")
    run([sys.executable, str(IMAGE_DIR / "prepare_build.py"), "info", str(bundle)],
        env=info_environment(build, env))  # fmt: skip
    run([str(python), "-I", "-B", str(bundle / "tester" / "vmaf-tester-report"),
         "--image-root", str(bundle), "generate-reference", str(bundle / "reference")])  # fmt: skip


def run_own_report(bundle: Path, out: Path) -> None:
    step("the zip's own report on this runner (run.cmd, as a tester runs it)")
    with (out / "report.stderr").open("w", encoding="utf-8") as stderr:
        shell = os.environ.get("COMSPEC", r"C:\Windows\System32\cmd.exe")
        result = subprocess.run([shell, "/d", "/c", str(bundle / "run.cmd")], cwd=out,  # noqa: S603
                                stdout=subprocess.DEVNULL, stderr=stderr,
                                timeout=4 * 3600, check=False)  # fmt: skip
    print((out / "report.stderr").read_text(encoding="utf-8", errors="replace"))
    print(f"report exit status: {result.returncode}")
    if result.returncode not in (0, 1) or not (out / "report.json").is_file():
        raise BuildError(f"the report did not complete (exit {result.returncode})")


def pack(bundle: Path, out: Path) -> Path:
    """A zip with one top-level folder, entries sorted, every timestamp 1980-01-01."""
    step("pack")
    target = out / f"{bundle.name}.zip"
    files = sorted(p for p in bundle.rglob("*") if p.is_file())
    with zipfile.ZipFile(target, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9) as zf:
        for path in files:
            entry = zipfile.ZipInfo(
                f"{bundle.name}/{path.relative_to(bundle).as_posix()}", ZIP_TIME
            )
            entry.compress_type = zipfile.ZIP_DEFLATED
            entry.external_attr = 0o644 << 16
            zf.writestr(entry, path.read_bytes())
    listing = [f"{p.stat().st_size} {p.relative_to(bundle).as_posix()}" for p in files]
    (out / "bundle-files.txt").write_text("\n".join(listing) + "\n", encoding="utf-8")
    (out / f"{target.name}.sha256").write_text(f"{sha256(target)}  {target.name}\n")
    print(
        f"{target.name}: {target.stat().st_size} bytes, {len(files)} files, sha256 {sha256(target)}"
    )
    return target


def check_host(arch: str) -> None:
    host = os.environ.get("PROCESSOR_ARCHITECTURE", "")
    if platform.system() != "Windows" or host.upper() != ARCHES[arch]:
        raise BuildError(
            f"build the {arch} zip on a {ARCHES[arch]} Windows runner, not {host or platform.system()}"
        )


def build_all(out: Path, env: dict[str, str]) -> None:
    check_host(env["VMAFX_ARCH"])
    tag = env["VMAFX_IMAGE_TAG"]
    bundle = out / f"vmafx-tester-windows-{env['VMAFX_ARCH']}-{tag}"
    build, texts = Path("build-tester-windows").resolve(), out / "licence-texts"
    configure_and_build(build)
    stage(build, bundle, env)
    fetch_fixtures(bundle, out, env["VMAF_RESOURCE_COMMIT"])
    python = install_interpreter(bundle, out, env)
    step("licence texts (python-build-standalone full archive, CPython Doc/license.rst)")
    pbs_licence_texts(out, texts, env)
    version = bundle_python_version(python)
    scan = out / "vmafx-sources.json"
    licensing(
        "fetch-texts", "--artifact", "windows-zip", "--python-version", version, "--out", str(texts)
    )
    licensing("scan-build", "--build", str(build), "--repo", ".", "--out", str(scan))
    records(bundle, build, python, env)
    step("licence notices (ADR-1503)")
    licensing("notices", "--artifact", "windows-zip", "--root", str(bundle), "--repo", ".",
              "--build-scan", str(scan), "--texts", str(texts),
              "--source-commit", env["VMAFX_SOURCE_COMMIT"], "--tag", tag)  # fmt: skip
    step("every program loads Windows DLLs or the bundle's own (no runtime DLL for VMAFx)")
    run([sys.executable, "scripts/ci/check-windows-bundle-imports.py", str(bundle),
         "--machine", env["VMAFX_ARCH"]])  # fmt: skip
    run_own_report(bundle, out)
    step("every file of the zip has a recorded licence (ADR-1503)")
    licensing("check", "--artifact", "windows-zip", "--root", str(bundle), "--repo", ".",
              "--build-scan", str(scan), "--python-version", version,
              "--receipt", str(bundle / "licence-check.json"))  # fmt: skip
    pack(bundle, out)


def main(argv: list[str]) -> int:
    if len(argv) != USAGE_ARGS:
        print(__doc__, file=sys.stderr)
        return 64
    out = Path(argv[1]).resolve()  # before the chdir: relative to the caller's directory
    os.chdir(Path(__file__).resolve().parents[2])
    out.mkdir(parents=True, exist_ok=True)
    try:
        build_all(out, require_environment(dict(os.environ)))
    except (BuildError, OSError, subprocess.SubprocessError, ValueError, KeyError) as error:
        print(f"build-windows-tester-bundle: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
