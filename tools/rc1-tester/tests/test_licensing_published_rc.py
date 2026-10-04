# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Notices and source for the images published before ADR-1513 (ADR-1578): the
record of what stays and what is withdrawn, the source fetch for Ubuntu bases
(Launchpad), vendor packages named by pattern, and the companion tool's refusal
to describe a tag that moved."""

from __future__ import annotations

import importlib.util
import json
import re
import urllib.error
from pathlib import Path

import pytest

_IMAGE = Path(__file__).resolve().parents[1] / "image"
REPO = Path(__file__).resolve().parents[3]


def _load(name: str, path: Path):
    spec = importlib.util.spec_from_file_location(name, path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


lic = _load("licensing", _IMAGE / "licensing.py")
companion = _load("published_rc_companion", REPO / "scripts/release/published_rc_companion.py")
DATA = json.loads((_IMAGE / "published-rc/artifacts.json").read_text(encoding="utf-8"))
DIGEST = re.compile(r"sha256:[0-9a-f]{64}")


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


# ------------------------------------------------------------- the record


def kept() -> list[tuple[str, dict]]:
    return [(release, a) for release, entry in DATA["releases"].items() for a in entry["keep"]]


@pytest.mark.parametrize(("release", "artifact"), kept(), ids=lambda v: v if isinstance(v, str) else v["name"])  # fmt: skip
def test_every_kept_image_names_a_digest_a_record_and_a_scan(release: str, artifact: dict) -> None:
    assert DIGEST.fullmatch(artifact["digest"]), artifact
    assert artifact["tag"].startswith(release)
    assert artifact["platforms"] and all(p.startswith("linux/") for p in artifact["platforms"])
    assert artifact["record"] in lic.load_manifest()["artifacts"]
    assert companion.scan_file(release, artifact["build"]).is_file()
    for binary in artifact.get("go_binaries", []):
        assert not binary.startswith("/")


def test_every_release_names_its_source_commit_and_native_scan() -> None:
    for release, entry in DATA["releases"].items():
        assert re.fullmatch(r"[0-9a-f]{40}", entry["source_commit"]), release
        assert companion.scan_file(release, entry["native"]["build"]).is_file()
        names = [a["name"] for a in entry["keep"]]
        assert len(names) == len(set(names)), release


def test_withdrawn_and_kept_images_do_not_overlap() -> None:
    kept_refs = {(a["image"], a["tag"]) for _, a in kept()}
    for entry in DATA["withdrawn"]:
        assert (entry["image"], entry["tag"]) not in kept_refs
        assert DIGEST.fullmatch(entry["digest"])
        assert entry["reason"] in DATA["withdrawal_reasons"]


def test_the_release_note_lists_withdrawn_and_kept_images() -> None:
    note = companion.release_note(DATA, "v1.0.0-rc.2")
    assert "v1.0.0-rc.2-rocm10" in note and "vmafx-node:v1.0.0-rc.2" in note
    assert "librocprof-trace-decoder" in note and "--enable-nonfree" in note
    for _, artifact in kept():
        if artifact["tag"].startswith("v1.0.0-rc.2"):
            assert f"{artifact['image']}:{artifact['tag']}-source" in note
    assert "v1.0.0-rc.1" not in note
    assert "`vmaf-mcp` 1.0.0rc2" in note and "1.0.0rc1" not in note
    assert "The whole `ghcr.io/vmafx/vmafx-node` package" in note


def test_the_release_note_replaces_its_own_section_only() -> None:
    note = companion.release_note(DATA, "v1.0.0-rc.1")
    body = "Release notes.\n\n" + note + "\nFooter.\n"
    updated = companion.with_note(body, note.replace("Withdrawn", "WITHDRAWN"))
    assert updated.startswith("Release notes.") and updated.endswith("Footer.\n")
    assert updated.count(companion.NOTE_BEGIN) == 1 and "WITHDRAWN" in updated


# ------------------------------------------------------ a tag that moved


def test_a_tag_that_names_another_digest_is_refused(monkeypatch: pytest.MonkeyPatch) -> None:
    """Planted defect: the registry serves a different digest for the tag."""
    artifact = DATA["releases"]["v1.0.0-rc.2"]["keep"][0]
    monkeypatch.setattr(companion, "tag_digest", lambda image, tag: "sha256:" + "0" * 64)
    with pytest.raises(companion.CompanionError, match="not the recorded"):
        companion.verify_digest(artifact)
    monkeypatch.setattr(companion, "tag_digest", lambda image, tag: artifact["digest"])
    companion.verify_digest(artifact)


# ------------------------------------------------- vendor packages by name


def status_tree(root: Path, packages: dict[str, str]) -> Path:
    stanzas = [f"Package: {name}\nStatus: install ok installed\nVersion: {version}\n"
               for name, version in packages.items()]  # fmt: skip
    write(root / "var/lib/dpkg/status", "\n".join(stanzas))
    return root


def test_vendor_packages_matched_by_pattern_bring_no_debian_source(tmp_path: Path) -> None:
    root = status_tree(tmp_path / "root", {"cuda-cudart-13-4": "13.4.92-1", "zlib1g": "1:1.3-1"})
    record = {"components": [{"kind": "dpkg-foreign", "package_patterns": ["cuda-*"]}]}
    ctx = lic.Context(root, REPO, record)
    assert lic.debian_specs(ctx, record) == {"zlib1g=1:1.3-1"}
    record["components"][0]["package_patterns"] = []
    assert "cuda-cudart-13-4=13.4.92-1" in lic.debian_specs(lic.Context(root, REPO, record), record)


INTEL_APT = {
    "intel-opencl-icd": "25.18.33578.15-1146~24.04",
    "libze-intel-gpu1": "25.18.33578.15-1146~24.04",
    "libigc2": "2.11.12-1146~24.04",
    "libigdfcl2": "2.11.12-1146~24.04",
    "libigdgmm12": "22.7.2-1135~24.04",
    "libze1": "1.21.9.0-1136~24.04",
    "libze-dev": "1.21.9.0-1136~24.04",
}
SOURCES_OF = {
    "intel-opencl-icd": "intel-compute-runtime",
    "libze-intel-gpu1": "intel-compute-runtime",
}


def intel_tree(root: Path, licence: str) -> Path:
    stanzas = [f"Package: {n}\nStatus: install ok installed\nVersion: {v}\n"
               + (f"Source: {SOURCES_OF[n]}\n" if n in SOURCES_OF else "")
               for n, v in {**INTEL_APT, "zlib1g": "1:1.3-1"}.items()]  # fmt: skip
    write(root / "var/lib/dpkg/status", "\n".join(stanzas))
    for name in INTEL_APT:
        write(root / f"usr/share/doc/{name}/copyright",
              f"Format: https://www.debian.org/doc/packaging-manuals/copyright-format/1.0/\n\n"
              f"Files: *\nCopyright: Intel Corporation\nLicense: {licence}\n")  # fmt: skip
    return root


def oneapi_record() -> dict:
    return lic.artifact_record(lic.load_manifest(), "published-rc-oneapi-image")


def test_intel_apt_packages_of_the_oneapi_image_bring_no_source(tmp_path: Path) -> None:
    """The rc oneapi image's MIT / BSD Intel GPU packages (version suffix -1146~24.04)
    are not in any archive; no licence obliges their source (dry runs 37222220784 and
    37222222915 failed fetching intel-compute-runtime)."""
    record = oneapi_record()
    specs = lic.debian_specs(
        lic.Context(intel_tree(tmp_path / "root", "MIT"), REPO, record), record
    )
    assert specs == {"zlib1g=1:1.3-1"}


def test_the_old_source_for_everything_rule_asks_for_the_intel_packages(tmp_path: Path) -> None:
    """Planted defect: the record without the vendor component, the old behaviour."""
    record = oneapi_record()
    record["components"] = [c for c in record["components"] if c["id"] != "intel-gpu-stack-apt"]
    ctx = lic.Context(intel_tree(tmp_path / "root", "MIT"), REPO, record)
    assert "intel-compute-runtime=25.18.33578.15-1146~24.04" in lic.debian_specs(ctx, record)


@pytest.mark.parametrize("licence", ["GPL-2+", "LGPL-2.1", "MPL-2.0", "GPL-3 or MIT"])
def test_a_vendor_package_declaring_copyleft_keeps_its_source_and_fails_closed(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch, licence: str
) -> None:
    record = oneapi_record()
    ctx = lic.Context(intel_tree(tmp_path / "root", licence), REPO, record)
    specs = lic.debian_specs(ctx, record)
    assert "intel-compute-runtime=25.18.33578.15-1146~24.04" in specs
    assert "libigc2=2.11.12-1146~24.04" in specs
    listing = write(tmp_path / "sources.list", "\n".join(f"debian {s}" for s in sorted(specs)))
    for name in ("snapshot_fetch", "launchpad_fetch"):
        monkeypatch.setattr(
            lic, name, lambda spec, out: (_ for _ in ()).throw(lic.LicensingError("none"))
        )
    monkeypatch.setattr(
        lic.subprocess,
        "run",
        lambda *a, **k: lic.subprocess.CompletedProcess(a, 100, "", "E: none"),
    )
    args = lic.argparse.Namespace(out=str(tmp_path / "out"), list=str(listing))
    with pytest.raises(lic.LicensingError, match="no source for"):
        lic.fetch_sources(args, lic.load_manifest())


def test_permissive_licence_names_are_not_copyleft() -> None:
    for name in ("MIT", "Expat", "BSD-3-clause", "SGI", "Apache-2.0", "Zlib"):
        assert lic.copyright_copyleft(f"License: {name}\n") == set(), name


# ---------------------------------------------------- Ubuntu source fetch


class Answers:
    """A stand-in for Launchpad: JSON per URL prefix, the files it serves."""

    def __init__(self, entries: list[dict], files: list[dict]) -> None:
        self.entries, self.files, self.downloads = entries, files, []

    def json_get(self, url: str):
        return self.files if "sourceFileUrls" in url else {"entries": self.entries}

    def download(self, url: str, destination: Path, sha256: str | None) -> None:
        self.downloads.append((url, destination.name, sha256))


def entry(version: str, status: str, link: str = "https://lp/pub/1") -> dict:
    return {"source_package_version": version, "status": status, "self_link": link}


def test_launchpad_serves_the_exact_version(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    files = [
        {"url": "https://lp/+sourcefiles/zlib/1:1.3-1ubuntu2/zlib_1.3-1ubuntu2.dsc", "sha256": "ab"}
    ]
    answers = Answers([entry("1:1.3-1ubuntu2", "Deleted", "https://lp/gone"),
                       entry("1:1.3-1ubuntu2", "Published")], files)  # fmt: skip
    monkeypatch.setattr(lic, "json_get", answers.json_get)
    monkeypatch.setattr(lic, "download", answers.download)
    lic.launchpad_fetch("zlib=1:1.3-1ubuntu2", tmp_path)
    assert answers.downloads == [(files[0]["url"], "zlib_1.3-1ubuntu2.dsc", "ab")]


def test_launchpad_serves_a_deleted_version_whose_files_remain(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    """linux=6.8.0-90.91 (the linux-libc-dev headers of the rc oneapi image) is
    "Deleted" on Launchpad in both pockets, and its files are still served: the
    dry runs failed on it with 'no source files'."""
    files = [{"url": "https://lp/+sourcefiles/linux/6.8.0-90.91/linux_6.8.0-90.91.dsc", "sha256": "ab"}]  # fmt: skip
    answers = Answers([entry("6.8.0-90.91", "Deleted", "https://lp/security"),
                       entry("6.8.0-90.91", "Deleted", "https://lp/updates")], files)  # fmt: skip
    monkeypatch.setattr(lic, "json_get", answers.json_get)
    monkeypatch.setattr(lic, "download", answers.download)
    lic.launchpad_fetch("linux=6.8.0-90.91", tmp_path)
    assert answers.downloads == [(files[0]["url"], "linux_6.8.0-90.91.dsc", "ab")]


def test_launchpad_deleted_version_with_purged_files_fails(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    def purged(url: str, destination: Path, sha256: str | None) -> None:
        raise urllib.error.URLError(f"404 {url}")

    answers = Answers([entry("6.8.0-90.91", "Deleted")], [{"url": "https://lp/x.dsc", "sha256": "ab"}])  # fmt: skip
    monkeypatch.setattr(lic, "json_get", answers.json_get)
    monkeypatch.setattr(lic, "download", purged)
    with pytest.raises(lic.LicensingError, match="no source files"):
        lic.launchpad_fetch("linux=6.8.0-90.91", tmp_path)


def test_launchpad_refuses_another_version(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    answers = Answers(
        [entry("1:1.3-1ubuntu3", "Published")], [{"url": "https://lp/x.dsc", "sha256": "ab"}]
    )
    monkeypatch.setattr(lic, "json_get", answers.json_get)
    monkeypatch.setattr(lic, "download", answers.download)
    with pytest.raises(lic.LicensingError, match="no source files"):
        lic.launchpad_fetch("zlib=1:1.3-1ubuntu2", tmp_path)
    assert answers.downloads == []


def test_a_source_nobody_has_fails(tmp_path: Path, monkeypatch: pytest.MonkeyPatch) -> None:
    """apt, snapshot.debian.org and Launchpad all fail: the fetch fails, it does
    not leave the package out."""

    class NoApt:
        returncode, stderr = 100, "E: Unable to find a source package"

    def missing(spec: str, out: Path) -> None:
        raise urllib.error.URLError("404")

    monkeypatch.setattr(lic.subprocess, "run", lambda *a, **k: NoApt())
    monkeypatch.setattr(lic, "snapshot_fetch", missing)
    monkeypatch.setattr(lic, "launchpad_fetch", missing)
    with pytest.raises(lic.LicensingError, match="no source for zlib=1"):
        lic.fetch_debian("zlib=1", tmp_path)


def test_launchpad_answers_when_snapshot_has_nothing(
    tmp_path: Path, monkeypatch: pytest.MonkeyPatch
) -> None:
    class NoApt:
        returncode, stderr = 100, ""

    def no_snapshot(spec: str, out: Path) -> None:
        raise lic.LicensingError("snapshot.debian.org has no source files")

    monkeypatch.setattr(lic.subprocess, "run", lambda *a, **k: NoApt())
    monkeypatch.setattr(lic, "snapshot_fetch", no_snapshot)
    monkeypatch.setattr(lic, "launchpad_fetch", lambda spec, out: None)
    assert lic.fetch_debian("attr=1:2.5.2-4ubuntu0.1", tmp_path) == "launchpad"


# ------------------------------------------------------------ the workflow


def test_the_workflow_publishes_only_when_asked() -> None:
    text = (REPO / ".github/workflows/published-rc-licence-companions.yml").read_text()
    assert "workflow_dispatch:" in text and "push:" not in text and "schedule:" not in text
    for step in ("Log in to GHCR", "SBOM of the published digest, and the source image",
                 "Attach the notices to the release page"):  # fmt: skip
        block = text.split(f"name: {step}", 1)[1].split("\n      - ", 1)[0]
        assert "if: inputs.publish" in block, step
    assert text.index("Check the digest and unpack") < text.index("Write the notices")


def workflow_path_problems(text: str) -> list[str]:
    """What actions/upload-artifact refuses, or what cannot work: a `..` in a work or
    artifact path, and the runner context in a job-level env (six-space keys), where it
    does not exist."""
    problems = [
        f"parent path: {m}"
        for m in re.findall(r"^ *(?:WORK:|\$\{\{).*\.\./.*$", text, re.MULTILINE)
    ]
    problems += [f"job-level runner context: {m}" for m in re.findall(r"^ {6}\w+: .*runner\..*$", text, re.MULTILINE)]  # fmt: skip
    return problems


def test_the_workflow_writes_its_outputs_without_parent_paths() -> None:
    text = (REPO / ".github/workflows/published-rc-licence-companions.yml").read_text()
    assert workflow_path_problems(text) == []
    assert text.count('echo "WORK=${RUNNER_TEMP}/') == 2  # companion and native jobs
    # planted defects: the dry runs' path, and the same under a job-level runner context
    assert workflow_path_problems("    env:\n      WORK: ${{ github.workspace }}/../companion\n")  # fmt: skip
    assert workflow_path_problems("    env:\n      WORK: ${{ runner.temp }}/x\n")  # fmt: skip


def test_notices_naming_a_text_the_image_lacks_are_refused(tmp_path: Path) -> None:
    """Planted defect: the old CUDA image without its EULA copyright file."""
    root = tmp_path / "linux-amd64"
    with pytest.raises(companion.CompanionError, match="cuda-cudart-13-4/copyright"):
        companion.check_image_texts("published-rc-cuda-image", root)
    write(root / "usr/share/doc/cuda-cudart-13-4/copyright", "NVIDIA CUDA Toolkit EULA\n")
    companion.check_image_texts("published-rc-cuda-image", root)
