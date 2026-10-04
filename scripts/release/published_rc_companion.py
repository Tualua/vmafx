#!/usr/bin/env python3
# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Notices and corresponding source for the images published before ADR-1513.

The v1.0.0-rc.1 and rc.2 images shipped without notices and without the source
of their copyleft parts. They stay published, unchanged (ADR-1578); this tool
writes what they lack, from the published bytes:

  scan     build a release tag's configuration and record its licence scan
           (tools/rc1-tester/image/published-rc/scans/<release>/<build>.json)
  plan     the images of a release, as a JSON list for a workflow matrix
  export   check that each tag still names its recorded digest, then unpack
           every platform of it
  licence  write the notices of every unpacked platform and the list of
           source packages, module zips and archives its copyleft parts need

`tools/rc1-tester/image/licensing.py` does the licence work; this tool only
feeds it the published images. The workflow
.github/workflows/published-rc-licence-companions.yml fetches the sources,
pushes <tag>-source and attaches the notices to the release page.
"""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
import shutil
import sys
import tempfile
from pathlib import Path
from typing import Any

REPO = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(REPO))

from scripts.lib.safe_subprocess import (  # noqa: E402
    CommandFailed,
    CommandTimedOut,
    TextCommandResult,
)
from scripts.lib.safe_subprocess import run as run_command  # noqa: E402

IMAGE_TOOLS = REPO / "tools/rc1-tester/image"
DATA = IMAGE_TOOLS / "published-rc/artifacts.json"
SCANS = IMAGE_TOOLS / "published-rc/scans"
LICENSING = IMAGE_TOOLS / "licensing.py"
DIGEST = re.compile(r"sha256:[0-9a-f]{64}")
TIMEOUT_S = 1800
# Build parallelism of `scan`: the host is shared with other builds.
BUILD_JOBS = 8


class CompanionError(RuntimeError):
    """A recorded fact does not hold; main() prints it and exits 1."""


def load(data: Path = DATA) -> dict[str, Any]:
    loaded: dict[str, Any] = json.loads(data.read_text(encoding="utf-8"))
    return loaded


def release_entry(data: dict[str, Any], release: str) -> dict[str, Any]:
    if release not in data["releases"]:
        raise CompanionError(f"{release} is not a recorded release of {DATA.name}")
    entry: dict[str, Any] = data["releases"][release]
    return entry


def artifact_entry(data: dict[str, Any], release: str, name: str) -> dict[str, Any]:
    found: list[dict[str, Any]] = [
        a for a in release_entry(data, release)["keep"] if a["name"] == name
    ]
    if len(found) != 1:
        raise CompanionError(f"{release} records {len(found)} images named {name!r}")
    return found[0]


def tool(name: str) -> str:
    """The absolute path of a program the tool runs; it must be installed."""
    found = shutil.which(name)
    if found is None:
        raise CompanionError(f"{name} is not on PATH")
    return found


def run(
    argv: list[str], cwd: Path | None = None, env: dict[str, str] | None = None
) -> TextCommandResult:
    """Run one allowlisted program with a deadline; a non-zero exit is an error."""
    program = argv[0] if Path(argv[0]).is_absolute() else tool(argv[0])
    try:
        return run_command([program, *argv[1:]], allowed_executables=(program,), cwd=cwd, env=env,
                           capture_output=True, text=True, check=True,
                           timeout_seconds=TIMEOUT_S, max_output_bytes=64 * 1_048_576)  # fmt: skip
    except CommandFailed as error:
        stderr = error.result.stderr
        text = stderr.decode(errors="replace") if isinstance(stderr, bytes) else stderr or ""
        detail = text.strip()[-400:]
        raise CompanionError(f"{Path(program).name} {argv[1]} failed: {detail}") from error


def licensing(*argv: str, cwd: Path | None = None) -> None:
    run([sys.executable, str(LICENSING), *argv], cwd=cwd)


# ------------------------------------------------------------------ export


def tag_digest(image: str, tag: str) -> str:
    """The digest the registry serves for image:tag (an index for a multi-platform image)."""
    shown = run(["docker", "buildx", "imagetools", "inspect", f"{image}:{tag}",
                 "--format", "{{json .Manifest}}"])  # fmt: skip
    digest = json.loads(shown.stdout).get("digest", "")
    return digest if isinstance(digest, str) else ""


def verify_digest(artifact: dict[str, Any]) -> None:
    """Refuse an image whose tag no longer names the recorded digest: the notices
    and the source would describe other bytes than the ones published."""
    if not DIGEST.fullmatch(artifact["digest"]):
        raise CompanionError(f"{artifact['tag']}: {artifact['digest']!r} is not a digest")
    current = tag_digest(artifact["image"], artifact["tag"])
    if current != artifact["digest"]:
        raise CompanionError(f"{artifact['image']}:{artifact['tag']} names {current!r}, "
                             f"not the recorded {artifact['digest']}")  # fmt: skip


def platform_dir(work: Path, platform: str) -> Path:
    return work / "rootfs" / platform.replace("/", "-")


def export_platform(artifact: dict[str, Any], platform: str, target: Path) -> None:
    """Unpack the file system of one platform of the published digest."""
    target.mkdir(parents=True, exist_ok=False)
    reference = f"{artifact['image']}@{artifact['digest']}"
    tarball = target.parent / f"{target.name}.tar"
    run(["docker", "pull", "--quiet", "--platform", platform, reference])
    container = run(["docker", "create", "--platform", platform, reference, "true"]).stdout.strip()
    try:
        run(["docker", "export", "--output", str(tarball), container])
    finally:
        run(["docker", "rm", container])
    run(["docker", "image", "rm", reference])
    run(["tar", "-x", "--no-same-owner", "-f", str(tarball), "-C", str(target)])
    tarball.unlink()


def export(args: argparse.Namespace) -> None:
    artifact = artifact_entry(load(), args.release, args.artifact)
    verify_digest(artifact)
    for platform in artifact["platforms"]:
        export_platform(artifact, platform, platform_dir(Path(args.work).resolve(), platform))


# ----------------------------------------------------------------- licence


def scan_file(release: str, build: str) -> Path:
    path = SCANS / release / f"{build}.json"
    if not path.is_file():
        raise CompanionError(f"no recorded scan {path.relative_to(REPO)}")
    return path


def platform_notices(
    artifact: dict[str, Any], release: str, root: Path, args: argparse.Namespace
) -> None:
    """Notices of one unpacked platform, written into its licence root (only for
    the export: the published image is not changed)."""
    record = artifact["record"]
    go_licences = []
    if artifact.get("go_binaries"):
        out = Path(args.work).resolve() / "go-licences" / root.name
        binaries = [arg for rel in artifact["go_binaries"] for arg in ("--binary", str(root / rel))]
        licensing("go-licences", *binaries, "--out", str(out), cwd=Path(args.source_tree).resolve())
        go_licences = ["--go-licences", str(out)]
    licensing("notices", "--artifact", record, "--root", str(root), "--repo", str(REPO),
              "--build-scan", str(scan_file(release, artifact["build"])),
              "--texts", str(Path(args.work).resolve() / "texts"),
              "--source-commit", load()["releases"][release]["source_commit"],
              "--tag", artifact["tag"], *go_licences)  # fmt: skip


def check_image_texts(record: str, root: Path) -> None:
    """Notices name some texts by their path in the image (a vendor's EULA, a
    package's copyright file); refuse notices that name a file the image lacks."""
    spec = importlib.util.spec_from_file_location("licensing", LICENSING)
    if spec is None or spec.loader is None:
        raise CompanionError(f"cannot load {LICENSING}")
    tool = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(tool)
    components = tool.load_manifest()["artifacts"][record]["components"]
    targets = [tool.text_target(entry) for c in components for entry in c.get("texts", [])]
    missing = [t for t in targets if t.startswith("/") and not (root / t[1:]).is_file()]
    if missing:
        raise CompanionError(f"{root.name}: the notices name texts the image lacks: {missing}")


def collect_notices(artifact: dict[str, Any], root: Path, out: Path) -> None:
    licence_root = root / "usr/local/share/vmafx/licenses"
    target = out / root.name
    shutil.copytree(licence_root, target)
    shutil.copyfile(target / "THIRD_PARTY_NOTICES.txt",
                    out / f"THIRD_PARTY_NOTICES-{artifact['name']}-{root.name}.txt")  # fmt: skip


def platform_sources(artifact: dict[str, Any], root: Path) -> set[str]:
    with tempfile.TemporaryDirectory() as tmp:
        listing = Path(tmp) / "sources.list"
        licensing("sources", "--artifact", artifact["record"], "--root", str(root),
                  "--repo", str(REPO), "--out", str(listing))  # fmt: skip
        return {line for line in listing.read_text(encoding="utf-8").splitlines() if line}


def licence(args: argparse.Namespace) -> None:
    artifact = artifact_entry(load(), args.release, args.artifact)
    work = Path(args.work).resolve()
    licensing("fetch-texts", "--artifact", artifact["record"],
              "--python-version", artifact.get("python_version", "none"),
              "--out", str(work / "texts"))  # fmt: skip
    notices = work / "notices"
    notices.mkdir(parents=True, exist_ok=True)
    sources: set[str] = set()
    for platform in artifact["platforms"]:
        root = platform_dir(work, platform)
        platform_notices(artifact, args.release, root, args)
        check_image_texts(artifact["record"], root)
        collect_notices(artifact, root, notices)
        sources |= platform_sources(artifact, root)
    (work / "sources.list").write_text("\n".join(sorted(sources)) + "\n", encoding="utf-8")


def release_notices(record: str, root: Path, release: str, work: Path) -> Path:
    """Notices of a tree of release files; returns its licence root."""
    data = load()
    texts = work / "texts"
    licensing("fetch-texts", "--artifact", record, "--python-version", "none", "--out", str(texts))
    licensing("notices", "--artifact", record, "--root", str(root), "--repo", str(REPO),
              "--build-scan", str(scan_file(release, release_entry(data, release)["native"]["build"])),
              "--texts", str(texts), "--source-commit", release_entry(data, release)["source_commit"],
              "--tag", release)  # fmt: skip
    return root / "licenses"


def native(args: argparse.Namespace) -> None:
    """Notices of a release's native files and of its models.tar.gz, from the
    assets downloaded into --assets."""
    spec = release_entry(load(), args.release)["native"]
    work, assets = Path(args.work).resolve(), Path(args.assets).resolve()
    files_root, models_root = work / "native-root", work / "models-root"
    files_root.mkdir(parents=True)
    models_root.mkdir()
    for name in spec["files"] + [spec["models"]]:
        shutil.copyfile(assets / name, files_root / name)
    run(["tar", "-x", "-z", "-f", str(assets / spec["models"]), "-C", str(models_root)])
    out = work / "notices"
    out.mkdir(parents=True, exist_ok=True)
    for kind, root in (("native", files_root), ("models", models_root)):
        licence_root = release_notices(f"published-rc-{kind}", root, args.release, work)
        shutil.copytree(licence_root, out / kind)
        shutil.copyfile(licence_root / "THIRD_PARTY_NOTICES.txt",
                        out / f"THIRD_PARTY_NOTICES-{kind}.txt")  # fmt: skip


def show(args: argparse.Namespace) -> None:
    """`key=value` lines of one image for a workflow's $GITHUB_OUTPUT."""
    artifact = artifact_entry(load(), args.release, args.artifact)
    for key in ("image", "tag", "digest", "record"):
        print(f"{key}={artifact[key]}")
    print(f"platforms={','.join(artifact['platforms'])}")


# -------------------------------------------------------------------- scan


def build_options(data: dict[str, Any], build: str) -> tuple[list[str], dict[str, str], list[str]]:
    """(meson options, extra environment, Go main packages) of a recorded build."""
    spec = data["builds"][build]
    base = data["builds"][spec["from"]] if "from" in spec else spec
    return base.get("options", []), base.get("env", {}), spec.get("go_packages", [])


def scan(args: argparse.Namespace) -> None:
    data = load()
    commit = release_entry(data, args.release)["source_commit"]
    options, env, go_packages = build_options(data, args.build)
    out = SCANS / args.release / f"{args.build}.json"
    out.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="published-rc-scan-") as tmp:
        tree, build = Path(tmp) / "src", Path(tmp) / "build"
        tree.mkdir()
        export_tree(commit, tree)
        scan_tree(tree, build, options, env, go_packages, out)
    print(f"wrote {out.relative_to(REPO)}")


def export_tree(commit: str, tree: Path) -> None:
    tarball = tree.parent / "source.tar"
    run(["git", "-C", str(REPO), "archive", "--format=tar", "-o", str(tarball), commit])
    run(["tar", "-x", "-f", str(tarball), "-C", str(tree)])
    tarball.unlink()


def scan_tree(tree: Path, build: Path, options: list[str], env: dict[str, str],
              go_packages: list[str], out: Path) -> None:  # fmt: skip
    environment = {**os.environ, **env}
    if options:
        run(["meson", "setup", str(build), "core", *options], cwd=tree, env=environment)
        targets = load()["ninja_targets"]
        run(["ninja", "-C", str(build), f"-j{BUILD_JOBS}", *targets], env=environment)
        licensing("scan-build", "--build", str(build), "--repo", str(tree), "--out", str(out))
    if go_packages:
        merge = ["--merge", str(out)] if options else []
        packages = [arg for package in go_packages for arg in ("--package", package)]
        licensing("scan-go", "--repo", ".", *packages, *merge, "--out", str(out), cwd=tree)


# -------------------------------------------------------------------- plan


def plan(args: argparse.Namespace) -> None:
    keep = release_entry(load(), args.release)["keep"]
    names = [a["name"] for a in keep if args.artifact in ("all", a["name"])]
    if not names and args.artifact != "native":
        raise CompanionError(f"{args.release} has no image named {args.artifact!r}")
    print(json.dumps(names))


NOTE_BEGIN = "<!-- published-rc-licences:begin -->"
NOTE_END = "<!-- published-rc-licences:end -->"


def release_note(data: dict[str, Any], release: str) -> str:
    """The licence section of a release page: what was withdrawn and why, and where
    the notices and the source of the images that stay are."""
    entry = release_entry(data, release)
    lines = [NOTE_BEGIN, "## Licences of the published images", "",
             "These images were published before the project's licensing rules (ADR-1513).",
             ""]  # fmt: skip
    withdrawn = [w for w in data["withdrawn"] if w["tag"].startswith(release)]
    if withdrawn:
        lines += [f"**Withdrawn** (deleted from the registry on {data['withdrawn_on']}):", ""]
        lines += [f"- `{w['image']}:{w['tag']}` ({w['digest']}): "
                  f"{data['withdrawal_reasons'][w['reason']]}" for w in withdrawn]  # fmt: skip
        lines += [f"- The whole `{p['package']}` package: {p['note']}"
                  for p in data["deleted_packages"]]  # fmt: skip
        lines.append("")
    yanked = entry.get("pypi_yanked", [])
    if yanked:
        lines += [f"**Yanked on PyPI** ({data['pypi_yank_reason']}):", ""]
        lines += [f"- `{y['project']}` {y['version']}" for y in yanked]
        lines.append("")
    lines += ["**Kept, unchanged.** Each image's notices are attached to this release as "
              "`THIRD_PARTY_NOTICES-<image>-<platform>.txt`, its licence texts as "
              "`licenses-<image>.tar.gz`, and the source of its copyleft parts is the "
              "image `<tag>-source` in the same registry package:", ""]  # fmt: skip
    lines += [f"- `{a['image']}:{a['tag']}` ({a['digest']}), source "
              f"`{a['image']}:{a['tag']}-source`" for a in entry["keep"]]  # fmt: skip
    lines += ["", "See docs/licensing.md in the repository (ADR-1578).", NOTE_END]
    return "\n".join(lines) + "\n"


def with_note(body: str, note: str) -> str:
    """`body` with its licence section replaced by `note`, or `note` appended."""
    if not body.strip():
        return note
    if NOTE_BEGIN in body and NOTE_END in body:
        head, _, rest = body.partition(NOTE_BEGIN)
        _, _, tail = rest.partition(NOTE_END)
        return head + note.rstrip("\n") + tail
    return body.rstrip("\n") + "\n\n" + note


def note(args: argparse.Namespace) -> None:
    body = Path(args.body).read_text(encoding="utf-8") if args.body else ""
    sys.stdout.write(with_note(body, release_note(load(), args.release)))


def parser() -> argparse.ArgumentParser:
    top = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = top.add_subparsers(dest="command", required=True)
    for name in ("scan", "plan", "show", "export", "licence", "note", "native"):
        command = sub.add_parser(name)
        command.add_argument("--release", required=True)
        if name == "scan":
            command.add_argument("--build", required=True)
        elif name == "note":
            command.add_argument("--body", help="the current release body, to update in place")
        elif name == "native":
            command.add_argument("--assets", required=True, help="the downloaded release files")
            command.add_argument("--work", required=True)
        else:
            command.add_argument("--artifact", required=True)
        if name in ("export", "licence"):
            command.add_argument("--work", required=True, help="directory for the unpacked images")
        if name == "licence":
            command.add_argument(
                "--source-tree",
                required=True,
                help="a checkout of the release tag (Go module resolution)",
            )
    return top


def main(argv: list[str] | None = None) -> int:
    args = parser().parse_args(argv)
    handlers = {
        "scan": scan,
        "plan": plan,
        "show": show,
        "export": export,
        "licence": licence,
        "note": note,
        "native": native,
    }
    try:
        handlers[args.command](args)
    except (CompanionError, OSError, CommandTimedOut) as error:
        print(f"published-rc: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
