# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""Tests for the licence record, notices and gate of the tester artifacts (ADR-1503)."""

from __future__ import annotations

import argparse
import base64
import hashlib
import importlib.util
import json
import struct
import subprocess
from pathlib import Path

import pytest

_IMAGE = Path(__file__).resolve().parents[1] / "image"
_spec = importlib.util.spec_from_file_location("licensing", _IMAGE / "licensing.py")
lic = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lic)

REPO = Path(__file__).resolve().parents[3]
BUILD_ID = "ab" * 20
TAG = "SPDX-" + "License-Identifier:"


# ------------------------------------------------------------------ helpers


def make_elf(build_id: str) -> bytes:
    """A minimal 64-bit little-endian ELF with one SHT_NOTE section holding a GNU build ID."""
    desc = bytes.fromhex(build_id)
    note = struct.pack("<III", 4, len(desc), 3) + b"GNU\0" + desc
    shoff = 64
    header = bytearray(64)
    header[:6] = b"\x7fELF\x02\x01"
    struct.pack_into("<Q", header, 0x28, shoff)
    struct.pack_into("<HH", header, 0x3A, 64, 2)
    null_section = bytes(64)
    section = bytearray(64)
    struct.pack_into("<I", section, 4, 7)
    struct.pack_into("<QQ", section, 0x18, shoff + 128, len(note))
    return bytes(header) + null_section + bytes(section) + note


def record_line(path: Path, rel: str) -> str:
    digest = base64.urlsafe_b64encode(hashlib.sha256(path.read_bytes()).digest()).decode()
    return f"{rel},sha256={digest.rstrip('=')},{path.stat().st_size}"


def write(path: Path, text: str | bytes) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(text) if isinstance(text, bytes) else path.write_text(text)
    return path


def fake_repo(tmp: Path) -> Path:
    repo = tmp / "repo"
    write(repo / "REUSE.toml", 'version = 1\n[[annotations]]\npath = ["data/**"]\n'
          'precedence = "closest"\nSPDX-FileCopyrightText = "2020 Data Owner"\n'
          'SPDX-License-Identifier = "MIT"\n')  # fmt: skip
    write(repo / "LICENSES/MIT.txt", "MIT text\n")
    write(repo / "LICENSES/EUPL-1.2.txt", "EUPL text\n")
    write(repo / "tools/report.py", f"# Copyright 2026 Lusoris\n# {TAG} EUPL-1.2\n")
    write(repo / "data/table.json", "{}\n")
    write(repo / "fixtures.sha256", "00  clip.yuv\n")
    return repo


def manifest() -> dict:
    return {
        "spdx_texts": {"MIT": "LICENSES/MIT.txt", "EUPL-1.2": "LICENSES/EUPL-1.2.txt"},
        "cpython_license_rst": {},
        "generated_build_files": [{"pattern": "src/config.h", "licence": "NONE"}],
        "grafted_libraries": [
            {"pattern": "libquadmath-*.so*", "licence": "LGPL-2.1-or-later", "copyleft": True,
             "sources": {BUILD_ID: "gcc-src"}},
            {"pattern": "libopenblas-*.so", "licence": "BSD-3-Clause", "copyleft": False},
        ],
        "source_archives": {"gcc-src": {"file": "gcc.src.rpm", "url": "https://example.invalid/gcc.src.rpm",
                                        "sha256": "0" * 64, "why": "test"}},
        "artifacts": {"kit": record()},
    }  # fmt: skip


def record() -> dict:
    return {
        "title": "the test kit", "licence_root": "licenses", "source_offer": "the image {tag}-source",
        "python": {"version": "3.14.8"},
        "components": [
            {"id": "bins", "kind": "build", "name": "binaries", "paths": ["bin/**"],
             "licences": ["EUPL-1.2", "MIT"]},
            {"id": "videos", "kind": "fixed", "name": "videos", "licence": "MIT",
             "manifest": {"file": "fixtures.sha256", "prefix": "res/"}},
            {"id": "files", "kind": "repo", "name": "repository files", "licences": ["EUPL-1.2", "MIT"],
             "map": [{"artifact": "tester/", "repo": "tools/"}, {"artifact": "data/", "repo": "data/"}]},
            {"id": "py", "kind": "python-dist", "name": "packages", "roots": ["site"]},
            {"id": "deb", "kind": "dpkg", "name": "packages"},
            {"id": "state", "kind": "state", "name": "state", "paths": ["var/lib/dpkg/**"]},
            {"id": "notices", "kind": "notices", "name": "notices", "paths": ["licenses/**"]},
        ],
    }  # fmt: skip


def fake_artifact(tmp: Path) -> Path:
    root = tmp / "root"
    write(root / "bin/vmaf", "binary")
    write(root / "res/clip.yuv", "yuv")
    write(root / "tester/report.py", f"# Copyright 2026 Lusoris\n# {TAG} EUPL-1.2\n")
    write(root / "data/table.json", "{}\n")
    write(root / "var/lib/dpkg/status", "Package: tar\nStatus: install ok installed\n"
          "Version: 1.35-1\nSource: tar (1.35+dfsg-1)\nBuilt-Using: glibc (= 2.41-1)\n\n")  # fmt: skip
    write(
        root / "var/lib/dpkg/info/tar.list",
        "/.\n/usr\n/usr/bin/tar\n/usr/share/doc/tar/copyright\n",
    )
    write(root / "usr/bin/tar", "tar")
    write(root / "usr/share/doc/tar/copyright", "GPL-3+\n")
    dist = root / "site/pkg-1.0.dist-info"
    lib = write(root / "site/pkg.libs/libquadmath-1234.so.0", make_elf(BUILD_ID))
    write(root / "site/pkg/__init__.py", "")
    write(
        dist / "METADATA",
        "Metadata-Version: 2.4\nName: pkg\nVersion: 1.0\nLicense-Expression: MIT\n",
    )
    write(dist / "licenses/LICENSE", "MIT text\n")
    rows = [record_line(root / "site/pkg/__init__.py", "pkg/__init__.py"),
            record_line(lib, "pkg.libs/libquadmath-1234.so.0"),
            "pkg-1.0.dist-info/METADATA,,", "pkg-1.0.dist-info/RECORD,,",
            "pkg-1.0.dist-info/licenses/LICENSE,,"]  # fmt: skip
    write(dist / "RECORD", "\n".join(rows) + "\n")
    return root


def scan(licences: tuple[str, ...] = ("EUPL-1.2",)) -> dict:
    files = [{"path": f"core/f{i}.c", "licence": e, "copyright": ["Copyright 2026 Lusoris"]}
             for i, e in enumerate(licences)]  # fmt: skip
    return {"schema_version": 1, "licences": sorted(licences), "files": files, "system_inputs": 0}


def setup_tree(tmp: Path, licences: tuple[str, ...] = ("EUPL-1.2",)) -> argparse.Namespace:
    repo, root = fake_repo(tmp), fake_artifact(tmp)
    scan_path = write(tmp / "scan.json", json.dumps(scan(licences)))
    (tmp / "texts").mkdir()
    return argparse.Namespace(artifact="kit", root=str(root), repo=str(repo), build_scan=str(scan_path),
                              texts=str(tmp / "texts"), source_commit="c0ffee", tag="t1",
                              python_version="3.14.8", receipt=None)  # fmt: skip


def notices_then_check(args: argparse.Namespace, data: dict | None = None) -> list[str]:
    data = data or manifest()
    lic.write_notices(args, data)
    return lic.run_check(args, data)


# ------------------------------------------------------------- expressions


def test_spdx_ids_split_operators_and_parentheses() -> None:
    expr = "(EUPL-1.2 AND BSD-2-Clause) OR GPL-3.0-or-later WITH GCC-exception-3.1"
    assert lic.spdx_ids(expr) == {
        "EUPL-1.2",
        "BSD-2-Clause",
        "GPL-3.0-or-later",
        "GCC-exception-3.1",
    }
    assert lic.spdx_ids("") == set()


def test_clean_expression_drops_comment_closers() -> None:
    assert lic.clean_expression(" BSD-2-Clause-Patent */") == "BSD-2-Clause-Patent"
    assert lic.clean_expression('EUPL-1.2\\n",') == "EUPL-1.2"


def test_reuse_glob_star_stays_in_one_directory() -> None:
    assert lic.reuse_glob("model/*").match("model/a.json")
    assert not lic.reuse_glob("model/*").match("model/x/a.json")
    assert lic.reuse_glob("model/**").match("model/x/a.json")
    assert lic.reuse_glob("a\\*b").match("a*b") and not lic.reuse_glob("a\\*b").match("axb")


def test_clean_copyright_needs_a_year_or_mark() -> None:
    assert lic.clean_copyright('Copyright 2016, Netflix, Inc."') == "Copyright 2016, Netflix, Inc."
    assert lic.clean_copyright("copyright notice in its entirety") == ""


def test_file_licence_header_wins_unless_override(tmp_path: Path) -> None:
    repo = fake_repo(tmp_path)
    path = write(repo / "data/own.py", f"# Copyright 2026 Lusoris\n# {TAG} EUPL-1.2\n")
    reuse = lic.load_reuse(repo)
    assert lic.file_licence(path, "data/own.py", reuse)[0] == "EUPL-1.2"
    assert lic.file_licence(repo / "data/table.json", "data/table.json", reuse) == (
        "MIT",
        ["Copyright 2020 Data Owner"],
    )
    reuse[0]["precedence"] = "override"
    assert lic.file_licence(path, "data/own.py", reuse)[0] == "MIT"


def test_elf_build_id_reads_the_gnu_note(tmp_path: Path) -> None:
    assert lic.elf_build_id(write(tmp_path / "lib.so", make_elf(BUILD_ID))) == BUILD_ID
    assert lic.elf_build_id(write(tmp_path / "text.so", b"not an elf at all, just text")) is None


# ---------------------------------------------------------------- the gate


def test_a_recorded_tree_passes_and_carries_its_texts(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    assert notices_then_check(args) == []
    licences = Path(args.root) / "licenses"
    text = (licences / lic.NOTICES_NAME).read_text()
    assert "https://github.com/VMAFx/vmafx/tree/c0ffee" in text and "the image t1-source" in text
    assert "  tar 1.35-1  source tar=1.35+dfsg-1 glibc=2.41-1" in text
    assert "libquadmath-1234.so.0: LGPL-2.1-or-later; source gcc.src.rpm" in text
    assert (licences / "texts/EUPL-1.2.txt").read_text() == "EUPL text\n"
    assert (licences / "texts/MIT.txt").is_file()  # the repo-mapped data file is MIT


def test_an_unrecorded_file_fails(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    write(Path(args.root) / "opt/vendor/libsecret.so", "x")
    assert "no recorded licence: opt/vendor/libsecret.so" in notices_then_check(args)


def test_a_package_without_its_copyright_file_fails(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    (Path(args.root) / "usr/share/doc/tar/copyright").unlink()
    problems = notices_then_check(args)
    assert "package tar has no /usr/share/doc/*/copyright" in problems


def test_a_dist_info_without_licence_file_fails(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    (Path(args.root) / "site/pkg-1.0.dist-info/licenses/LICENSE").unlink()
    assert "pkg-1.0.dist-info keeps no licence file" in notices_then_check(args)


def test_a_modified_grafted_library_fails(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    lib = Path(args.root) / "site/pkg.libs/libquadmath-1234.so.0"
    lib.write_bytes(lib.read_bytes() + b"\0")  # what `strip` does to a vendor binary
    problems = notices_then_check(args)
    assert any("libquadmath-1234.so.0 differs from its wheel RECORD" in p for p in problems)


def test_copyleft_graft_without_recorded_source_fails(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    data = manifest()
    data["grafted_libraries"][0]["sources"] = {}
    problems = notices_then_check(args, data)
    assert any("has no recorded source" in p and BUILD_ID in p for p in problems)


def test_unknown_grafted_library_fails(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    data = manifest()
    data["grafted_libraries"] = data["grafted_libraries"][1:]
    assert any("has no grafted_libraries entry" in p for p in notices_then_check(args, data))


def test_a_compiled_licence_outside_the_record_fails(tmp_path: Path) -> None:
    args = setup_tree(tmp_path, licences=("EUPL-1.2", "MIT", "GPL-2.0-only"))
    data = manifest()
    data["spdx_texts"]["GPL-2.0-only"] = "LICENSES/MIT.txt"
    problems = notices_then_check(args, data)
    assert any("GPL-2.0-only, not in component bins" in p for p in problems)


def test_a_repository_file_with_another_licence_fails(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    write(Path(args.root) / "tester/vendored.py", f"# {TAG} GPL-3.0-only\n")
    data = manifest()
    data["spdx_texts"]["GPL-3.0-only"] = "LICENSES/MIT.txt"
    problems = notices_then_check(args, data)
    assert any("tester/vendored.py declares GPL-3.0-only" in p for p in problems)


def test_a_licence_without_text_stops_the_notices(tmp_path: Path) -> None:
    args = setup_tree(tmp_path, licences=("EUPL-1.2", "Unlisted-1.0"))
    with pytest.raises(lic.LicensingError, match="Unlisted-1.0 has no text"):
        lic.write_notices(args, manifest())


def test_another_interpreter_version_fails(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    args.python_version = "3.14.9"
    assert any("Python 3.14.9" in p for p in notices_then_check(args))


def test_missing_or_stale_notices_fail(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    assert any("THIRD_PARTY_NOTICES.txt is missing" in p for p in lic.run_check(args, manifest()))
    lic.write_notices(args, manifest())
    status = Path(args.root) / "var/lib/dpkg/status"
    status.write_text(status.read_text().replace("1.35-1\n", "1.36-1\n", 1))
    assert "the notices do not list package tar" in lic.run_check(args, manifest())


def test_report_check_exit_status_and_receipt(tmp_path: Path, capsys) -> None:
    args = argparse.Namespace(
        artifact="kit", python_version="3.14.8", receipt=str(tmp_path / "r.json")
    )
    assert lic.report_check(args, ["no recorded licence: x"]) == 1
    assert not (tmp_path / "r.json").exists()
    assert lic.report_check(args, []) == 0
    assert json.loads((tmp_path / "r.json").read_text())["result"] == "pass"
    assert "no recorded licence: x" in capsys.readouterr().err


# -------------------------------------------------------------- scan-build


def fake_build(tmp: Path, monkeypatch, generated: str = "src/config.h") -> tuple[Path, Path]:
    repo = fake_repo(tmp)
    write(repo / "core/a.c", f"/* Copyright 2019 Someone\n * {TAG} MIT */\n")
    build = tmp / "build"
    write(build / generated, "#define X 1\n")
    deps = ["../repo/core/a.c", generated, "/usr/include/stdio.h"]
    monkeypatch.setattr(lic, "ninja_deps", lambda _build: deps)
    return build, repo


def test_scan_build_reads_compiled_files_and_generated_rules(tmp_path: Path, monkeypatch) -> None:
    build, repo = fake_build(tmp_path, monkeypatch)
    result = lic.scan_build(build, repo, manifest())
    assert result["licences"] == ["MIT"]
    assert {
        "path": "core/a.c",
        "licence": "MIT",
        "copyright": ["Copyright 2019 Someone"],
    } in result["files"]
    assert result["system_inputs"] == 1


def test_scan_build_refuses_an_unruled_generated_file(tmp_path: Path, monkeypatch) -> None:
    build, repo = fake_build(tmp_path, monkeypatch, generated="src/blob.c")
    with pytest.raises(
        lic.LicensingError, match="src/blob.c matches no generated_build_files rule"
    ):
        lic.scan_build(build, repo, manifest())


def test_scan_build_refuses_a_compiled_file_without_licence(tmp_path: Path, monkeypatch) -> None:
    build, repo = fake_build(tmp_path, monkeypatch)
    write(repo / "core/a.c", "int a;\n")
    with pytest.raises(lic.LicensingError, match="core/a.c has no SPDX header"):
        lic.scan_build(build, repo, manifest())


def test_generated_source_is_identified_by_its_bytes(tmp_path: Path) -> None:
    repo, build = tmp_path / "repo", tmp_path / "build"
    write(repo / "model/a/m.json", "one")
    write(repo / "model/b/m.json", "two")
    write(build / "src/m.json", "two")
    rule = {"pattern": "src/*.json.c", "repo": "model/**/{stem}"}
    assert lic.generated_source(rule, "src/m.json.c", build, repo) == "model/b/m.json"
    (build / "src/m.json").unlink()
    with pytest.raises(lic.LicensingError, match="identifies 2 files"):
        lic.generated_source(rule, "src/m.json.c", build, repo)


# ----------------------------------------------------------------- sources


def test_sources_list_debian_built_using_and_copyleft_grafts(tmp_path: Path) -> None:
    args = setup_tree(tmp_path)
    lines = lic.source_list(args, manifest())
    assert lines == ["debian glibc=2.41-1", "debian tar=1.35+dfsg-1", "archive gcc-src"]


def test_fetch_sources_uses_apt_then_records_archives(tmp_path: Path, monkeypatch) -> None:
    listing = write(tmp_path / "list", "debian tar=1.35+dfsg-1\narchive gcc-src\n")
    calls = []

    def run(argv, **kwargs):
        calls.append(argv)
        return subprocess.CompletedProcess(argv, 0, "", "")

    monkeypatch.setattr(lic.subprocess, "run", run)
    monkeypatch.setattr(lic, "download", lambda url, dest, sha: dest.write_text(url))
    args = argparse.Namespace(list=str(listing), out=str(tmp_path / "out"))
    index = lic.fetch_sources(args, manifest())
    assert calls == [["apt-get", "source", "--download-only", "-qq", "tar=1.35+dfsg-1"]]
    assert index[0] == "tar=1.35+dfsg-1  debian/  (archive)"
    assert (
        tmp_path / "out/archives/gcc.src.rpm"
    ).read_text() == "https://example.invalid/gcc.src.rpm"


def test_fetch_debian_falls_back_to_snapshot(tmp_path: Path, monkeypatch) -> None:
    monkeypatch.setattr(lic.subprocess, "run",
                        lambda argv, **kw: subprocess.CompletedProcess(argv, 100, "", "gone"))  # fmt: skip
    seen = []
    monkeypatch.setattr(lic, "snapshot_fetch", lambda spec, out: seen.append(spec))
    assert lic.fetch_debian("tar=1.0", tmp_path) == "snapshot" and seen == ["tar=1.0"]


def test_download_refuses_a_wrong_hash(tmp_path: Path, monkeypatch) -> None:
    class Response:
        def __init__(self) -> None:
            self.chunks = [b"payload", b""]

        def read(self, _size: int) -> bytes:
            return self.chunks.pop(0)

        def __enter__(self):
            return self

        def __exit__(self, *_exc) -> None:
            return None

    monkeypatch.setattr(lic.urllib.request, "urlopen", lambda *a, **k: Response())
    with pytest.raises(lic.LicensingError, match="is not the recorded"):
        lic.download("https://example.invalid/x", tmp_path / "x", "0" * 64)
    assert not (tmp_path / "x").exists()


# ------------------------------------------------- the record and the recipes


def test_the_record_names_texts_and_archives_that_exist() -> None:
    data = lic.load_manifest()
    missing = [text for text in data["spdx_texts"].values() if not (REPO / text).is_file()]
    assert missing == []
    used = {s for rule in data["grafted_libraries"] for s in rule.get("sources", {}).values()}
    assert used <= set(data["source_archives"])
    entries = [
        e for r in data["artifacts"].values() for c in r["components"] for e in c.get("texts", [])
    ]
    assert [e for e in entries if "repo" in e and not (REPO / e["repo"]).is_file()] == []


def test_every_artifact_records_its_interpreter_and_core_components() -> None:
    data = lic.load_manifest()
    for kind, record in data["artifacts"].items():
        assert record["python"]["version"] in data["cpython_license_rst"], kind
        kinds = {c["kind"] for c in record["components"]}
        assert {"build", "repo", "python-dist", "notices"} <= kinds, kind


def test_the_record_allows_every_licence_the_repo_files_declare() -> None:
    """Every licence a build component allows has a text to ship."""
    data = lic.load_manifest()
    for record in data["artifacts"].values():
        for component in record["components"]:
            missing = set(component.get("licences", [])) - set(data["spdx_texts"])
            assert not missing, (component["id"], missing)


def test_the_image_cannot_be_built_without_the_licence_check() -> None:
    text = (REPO / "docker/Dockerfile.tester").read_text()
    final = text.split("FROM assembled AS final", 1)[1]
    assert "COPY --from=licence-check /out/licence-check.json" in final
    assert "licensing.py check --artifact image" in text
    assert "licensing.py notices --artifact image" in text
    assert "-not -path '*.libs/*'" in text  # vendor libraries ship unmodified
    assert "FROM scratch AS source-export" in text


def test_the_bundle_script_checks_before_it_packs() -> None:
    text = (REPO / "scripts/ci/build-macos-tester-bundle.sh").read_text()
    assert text.index("licensing check --artifact macos") < text.index('step "pack"')
    assert text.index("licensing notices --artifact macos") < text.index('step "the bundle')
    assert "PBS_FULL_SHA256" in text


@pytest.mark.parametrize(("workflow", "needle"), [
    ("docker-publish-tester.yml", "sbom-path: ${{ runner.temp }}/sbom/tester-sbom-arm64/sbom.spdx.json"),
    ("docker-publish-tester.yml", "target: source-export"),
    ("macos-tester-bundle.yml", "sbom-path: ${{ steps.sbom.outputs.path }}"),
    ("macos-tester-bundle.yml", "PBS_FULL_SHA256:"),
])  # fmt: skip
def test_both_workflows_attest_an_sbom(workflow: str, needle: str) -> None:
    assert needle in (REPO / ".github/workflows" / workflow).read_text()
