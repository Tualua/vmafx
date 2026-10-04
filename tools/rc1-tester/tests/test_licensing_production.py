# Copyright 2026 Lusoris
# SPDX-License-Identifier: EUPL-1.2
"""The licence gate applied to the production artifacts (ADR-1513): distroless
package records, PEP 639 licence directories, and the contracts that keep every
published production image behind its licence check."""

from __future__ import annotations

import argparse
import importlib.util
import json
import os
import re
from pathlib import Path

import pytest

_IMAGE = Path(__file__).resolve().parents[1] / "image"
_spec = importlib.util.spec_from_file_location("licensing", _IMAGE / "licensing.py")
lic = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(lic)

REPO = Path(__file__).resolve().parents[3]
TAG = "SPDX-" + "License-Identifier:"


def write(path: Path, text: str) -> Path:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(text)
    return path


# ------------------------------------------------------- distroless records


def distroless(tmp: Path) -> Path:
    """A distroless tree: package stanzas and md5sums in status.d, no info/*.list."""
    root = tmp / "root"
    write(root / "var/lib/dpkg/status.d/libc6", "Package: libc6\nSource: glibc\nVersion: 2.41-12\n")
    write(root / "var/lib/dpkg/status.d/libc6.md5sums",
          "00  usr/lib/x86_64-linux-gnu/libc.so.6\n00  usr/share/doc/libc6/copyright\n")  # fmt: skip
    write(root / "usr/lib/x86_64-linux-gnu/libc.so.6", "elf")
    write(root / "usr/share/doc/libc6/copyright", "LGPL-2.1+\n")
    os.symlink("libc.so.6", root / "usr/lib/x86_64-linux-gnu/libc.so")
    os.symlink("usr/lib", root / "lib")
    return root


def test_distroless_packages_and_their_files_are_read(tmp_path: Path) -> None:
    root = distroless(tmp_path)
    assert [p["Package"] for p in lic.dpkg_packages(root)] == ["libc6"]
    assert "usr/lib/x86_64-linux-gnu/libc.so.6" in lic.dpkg_owned(root)
    assert lic.package_sources(lic.dpkg_packages(root)[0]) == ["glibc=2.41-12"]


def test_a_link_onto_a_package_file_or_directory_is_claimed(tmp_path: Path) -> None:
    root = distroless(tmp_path)
    ctx = lic.Context(root, tmp_path, {"components": []})
    assert lic.dpkg_claims(ctx, "usr/lib/x86_64-linux-gnu/libc.so")
    assert lic.dpkg_claims(ctx, "lib")
    os.symlink("nowhere", root / "usr/lib/x86_64-linux-gnu/libdangling.so")
    os.symlink("../../../../etc/shadow", root / "usr/lib/x86_64-linux-gnu/libescape.so")
    assert not lic.dpkg_claims(ctx, "usr/lib/x86_64-linux-gnu/libdangling.so")
    assert not lic.dpkg_claims(ctx, "usr/lib/x86_64-linux-gnu/libescape.so")


def test_a_distroless_package_without_its_copyright_file_fails(tmp_path: Path) -> None:
    root = distroless(tmp_path)
    ctx = lic.Context(root, tmp_path, {"components": []})
    assert lic.check_dpkg(ctx) == []
    (root / "usr/share/doc/libc6/copyright").unlink()
    assert lic.check_dpkg(ctx) == ["package libc6 has no /usr/share/doc/*/copyright"]


# ------------------------------------------------------------ Python dists


def dist(root: Path, name: str, files: dict[str, str]) -> Path:
    info = root / f"site/{name}-1.0.dist-info"
    write(info / "METADATA", f"Metadata-Version: 2.4\nName: {name}\nVersion: 1.0\n"
          "License-Expression: EUPL-1.2\n")  # fmt: skip
    for rel, text in files.items():
        write(info / rel, text)
    write(info / "RECORD", "")
    return info


def py_record(texts: list[dict] | None = None) -> dict:
    component = {"id": "py", "kind": "python-dist", "name": "packages", "roots": ["site"]}
    if texts:
        component["texts"] = texts
    return {"components": [component]}


def test_a_pep639_licence_directory_counts_as_licence_files(tmp_path: Path) -> None:
    info = dist(tmp_path, "own", {"licenses/LICENSES/EUPL-1.2.txt": "EUPL text\n"})
    assert lic.dist_metadata(info)["licence_files"] == ["licenses/LICENSES/EUPL-1.2.txt"]
    ctx = lic.Context(tmp_path, tmp_path, py_record())
    assert lic.check_dists(ctx) == []


def test_a_dist_without_licence_file_needs_a_recorded_text(tmp_path: Path) -> None:
    dist(tmp_path, "bare", {})
    assert lic.check_dists(lic.Context(tmp_path, tmp_path, py_record())) == [
        "bare-1.0.dist-info keeps no licence file"
    ]
    recorded = py_record([{"fetched": "bare-LICENSE", "name": "bare-LICENSE.txt", "dist": "bare"}])
    assert lic.check_dists(lic.Context(tmp_path, tmp_path, recorded)) == []
    other = py_record([{"fetched": "x", "name": "x.txt", "dist": "another"}])
    assert lic.check_dists(lic.Context(tmp_path, tmp_path, other)) != []


# ------------------------------------------------------------- contracts


def published_targets(workflow: str) -> set[tuple[str, str]]:
    """(Dockerfile, target) of every image a publish workflow pushes."""
    text = (REPO / ".github/workflows" / workflow).read_text()
    pairs = re.findall(r"file: (\S+)\n\s+target: (\S+)", text)
    return {(f, t) for f, t in pairs if not t.endswith("source-export")}


# Published production targets whose licence gate lands with a later PR of the
# production-licensing train (ADR-1513; docs/state.md row
# T-PROD-LICENCE-GPU-IMAGES-2026-10-04).
# The set only shrinks; the test fails on an entry that has gained its gate.
PENDING = {
    ("docker/Dockerfile.production-gpu", "final-cuda13"),
    ("docker/Dockerfile.production-gpu", "final-rocm10"),
    ("docker/Dockerfile.production-gpu", "final-oneapi2026"),
}


def final_stage(dockerfile: str, target: str) -> str:
    text = (REPO / dockerfile).read_text()
    match = re.search(
        rf"^FROM \S+ AS {re.escape(target)}\n(.*?)(?=^FROM |\Z)", text, re.MULTILINE | re.DOTALL
    )
    assert match, f"{dockerfile} has no target {target}"
    return match.group(1)


def has_licence_gate(dockerfile: str, target: str) -> bool:
    stage = final_stage(dockerfile, target)
    gate = r"^COPY --from=\S*licence-check /out/licence-check.json"
    return re.search(gate, stage, re.MULTILINE) is not None


@pytest.mark.parametrize(
    "workflow", ["docker-publish-production.yml", "docker-publish-operator-node.yml"]
)
def test_every_published_production_target_passes_a_licence_check(workflow: str) -> None:
    for dockerfile, target in sorted(published_targets(workflow)):
        gated = has_licence_gate(dockerfile, target)
        if (dockerfile, target) in PENDING:
            assert not gated, f"{target} has its gate now: remove it from PENDING"
        else:
            assert gated, f"{dockerfile} target {target} is published without a licence check"


@pytest.mark.parametrize(("target", "kind", "source"), [
    ("cli", "production-cli-image", "cli-source-export"),
    ("server", "production-server-image", "server-source-export"),
])  # fmt: skip
def test_the_cpu_images_carry_notices_and_publish_their_source(
    target: str, kind: str, source: str
) -> None:
    text = (REPO / "docker/Dockerfile.production").read_text()
    assert f"licensing.py notices --artifact {kind}" in text
    assert f"licensing.py check --artifact {kind}" in text
    assert f"FROM scratch AS {source}\n" in text
    workflow = (REPO / ".github/workflows/docker-publish-production.yml").read_text()
    assert f"source-target: {source}\n" in workflow
    assert "VMAFX_SOURCE_COMMIT=${{ needs.validate-release.outputs.source-sha }}" in workflow


def test_the_licence_artifacts_action_attests_spdx_with_the_pinned_tools() -> None:
    action = (REPO / ".github/actions/image-licence-artifacts/action.yml").read_text()
    assert "syft-version: v1.51.1" in action
    assert action.count("actions/attest@1e69f48acb82d1966a394da916b4c1698aa569d6") == 2
    assert "sbom-path: sbom-${{ inputs.name }}-amd64.spdx.json" in action
    assert "attest-sbom" not in action  # deprecated (ADR-1503 rule 6)


def test_the_image_licence_label_is_the_compiled_licence_set() -> None:
    record = lic.load_manifest()["artifacts"]["production-cli-image"]
    binaries = next(c for c in record["components"] if c["id"] == "vmafx-binaries")
    expected = " AND ".join(binaries["licences"])
    labels = re.findall(r'org\.opencontainers\.image\.licenses="([^"]*)"',
                        (REPO / "docker/Dockerfile.production").read_text())  # fmt: skip
    assert labels and set(labels) == {expected}


@pytest.mark.parametrize(("project", "sources"), [
    ("mcp-server/vmaf-mcp", "src"),
    ("tools/vmaf-tune", "src"),
])  # fmt: skip
def test_a_python_package_declares_the_licences_of_its_files(project: str, sources: str) -> None:
    """The wheel metadata names every licence its files carry, and the wheel
    ships each text (PEP 639 license-files), byte for byte the repository's."""
    pyproject = (REPO / project / "pyproject.toml").read_text()
    declared = re.search(r'^license = "([^"]+)"$', pyproject, re.MULTILINE).group(1)
    found = set()
    for path in sorted((REPO / project / sources).rglob("*.py")):
        match = re.search(re.escape(TAG) + r"\s*(\S+)", path.read_text(encoding="utf-8"))
        assert match, f"{path} has no SPDX header"
        found |= lic.spdx_ids(match.group(1))
    assert lic.spdx_ids(declared) == found
    assert 'license-files = ["LICENSES/*"]' in pyproject
    spdx_texts = lic.load_manifest()["spdx_texts"]
    for identifier in found:
        shipped = REPO / project / "LICENSES" / f"{identifier}.txt"
        assert shipped.read_bytes() == (REPO / spdx_texts[identifier]).read_bytes()


def test_the_production_records_name_every_text_they_fetch() -> None:
    data = lic.load_manifest()
    for kind in ("production-cli-image", "production-server-image"):
        record = data["artifacts"][kind]
        assert lic.recorded_fetches(record, data).keys() <= data["fetched_texts"].keys()
        assert json.dumps(record).count("vmafx-binaries") == 1
    assert (
        data["artifacts"]["production-server-image"]["python"]["version"]
        in data["cpython_license_rst"]
    )


def test_notices_of_a_distroless_tree_list_its_packages(tmp_path: Path) -> None:
    root = distroless(tmp_path)
    repo = tmp_path / "repo"
    write(repo / "REUSE.toml", "version = 1\n")
    scan_file = write(tmp_path / "scan.json", json.dumps({"licences": [], "files": []}))
    (tmp_path / "texts").mkdir()
    record = {"title": "t", "licence_root": "licenses", "source_offer": "{tag}-source",
              "components": [{"id": "deb", "kind": "dpkg", "name": "packages"},
                             {"id": "state", "kind": "state", "name": "s",
                              "paths": ["var/lib/dpkg/status.d/**"]},
                             {"id": "notices", "kind": "notices", "name": "n", "paths": ["licenses/**"]}]}  # fmt: skip
    data = {
        "spdx_texts": {},
        "grafted_libraries": [],
        "source_archives": {},
        "artifacts": {"k": record},
    }
    args = argparse.Namespace(artifact="k", root=str(root), repo=str(repo), build_scan=str(scan_file),
                              texts=str(tmp_path / "texts"), source_commit="c0ffee", tag="t1",
                              python_version="none", receipt=None)  # fmt: skip
    lic.write_notices(args, data)
    assert (
        "  libc6 2.41-12  source glibc=2.41-12"
        in (root / "licenses/THIRD_PARTY_NOTICES.txt").read_text()
    )
    assert lic.run_check(args, data) == []
    assert lic.source_list(args, data) == ["debian glibc=2.41-12"]


def test_model_annotations_match_the_registry() -> None:
    """Every tiny model's weights carry, through REUSE.toml, the licence its registry
    entry declares, so the computed notices name the upstream holder (ADR-1513)."""
    registry = json.loads((REPO / "model/tiny/registry.json").read_text())
    reuse = lic.load_reuse(REPO)
    for model in registry["models"]:
        rel = f"model/tiny/{model['onnx']}"
        expression, copyrights = lic.file_licence(REPO / rel, rel, reuse)
        assert lic.spdx_ids(model["license"]) <= lic.spdx_ids(expression), (rel, expression)
        assert copyrights, rel
    for rel in ("model/predictor_libx264.onnx", "model/konvid_mos_head_v1.onnx"):
        assert lic.file_licence(REPO / rel, rel, reuse)[1] == ["Copyright 2026 Lusoris"], rel
