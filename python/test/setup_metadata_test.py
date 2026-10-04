# SPDX-License-Identifier: EUPL-1.2
# Copyright 2026 Lusoris
"""Regression tests for the compatibility package's setup metadata."""

from __future__ import annotations

import ast
import fnmatch
import functools
import importlib.util
import os
import re
import runpy
import shutil
import subprocess
import sys
import tomllib
from pathlib import Path
from unittest import mock

import pytest

# `packaging` is a hard (non-extra) dependency of pytest itself
# (`pytest` requires `packaging>=22`), so it is available wherever this test
# runs and needs no entry in python/requirements.txt.
from packaging.requirements import Requirement
from packaging.version import InvalidVersion, Version

import vmaf

REPO_ROOT = Path(__file__).resolve().parents[2]
PYPROJECT = REPO_ROOT / "python" / "pyproject.toml"
SETUP_PY = REPO_ROOT / "python" / "setup.py"
PACKAGE_PYPROJECTS = (
    REPO_ROOT / "ai" / "pyproject.toml",
    REPO_ROOT / "dev-llm" / "pyproject.toml",
    REPO_ROOT / "mcp-server" / "vmaf-mcp" / "pyproject.toml",
    REPO_ROOT / "python" / "pyproject.toml",
    REPO_ROOT / "tools" / "ensemble-training-kit" / "pyproject.toml",
    REPO_ROOT / "tools" / "vmaf-roi-score" / "pyproject.toml",
    REPO_ROOT / "tools" / "vmaf-tune" / "pyproject.toml",
)

# The hash locks that install the build backend for `make cythonize` and the
# CI `setup.py` runs. The declared floor must admit the setuptools they pin;
# otherwise no locked environment could satisfy `[build-system].requires`.
BUILD_BACKEND_LOCKS = (
    REPO_ROOT / "requirements" / "locks" / "cythonize.txt",
    REPO_ROOT / "requirements" / "locks" / "package-build.txt",
)
# The newest setuptools that predates PEP 639 support, plus the version Ubuntu
# 24.04 ships in /usr/lib/python3/dist-packages. Both must be excluded by the
# declared floor, or `setup.py` aborts before it can report a version.
PRE_PEP639_SETUPTOOLS = ("76.1.0", "68.1.2")
# Releases affected by PYSEC-2025-49 (fixed in 78.1.1) and PYSEC-2026-3447
# (fixed in 83.0.0). The declared floor must exclude them as well.
VULNERABLE_SETUPTOOLS = ("78.1.0", "82.9.0")

# The licence tool of ADR-1503 / ADR-1513: the one reader of SPDX headers,
# REUSE.toml annotations and SPDX expressions, shared with the image gates.
LICENSING_TOOL = REPO_ROOT / "tools" / "rc1-tester" / "image" / "licensing.py"
GIT_TIMEOUT_S = 60
# Upper bound on the files one extension's include closure may visit.
MAX_INCLUDE_VISITS = 1 << 14
INCLUDE_LINE = re.compile(r'^[ \t]*#[ \t]*include[ \t]*[<"]([^>"]+)[>"]', re.MULTILINE)
CYTHON_EXTERN_LINE = re.compile(r'^[ \t]*cdef[ \t]+extern[ \t]+from[ \t]+"([^"]+)"', re.MULTILINE)
HATCH_BACKEND = "hatchling.build"
# hatchling options that change which files the sdist holds.
HATCH_SELECTION_KEYS = {"include", "exclude", "only-include", "packages", "sources"}
SETUPTOOLS_BACKENDS = ("setuptools.build_meta", "setuptools.build_meta:__legacy__")


def _load_licensing_tool():
    spec = importlib.util.spec_from_file_location("vmafx_licensing", LICENSING_TOOL)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


licensing = _load_licensing_tool()


def _locked_version(lock: Path, name: str) -> str:
    """The exact version `lock` pins for `name`."""
    prefix = f"{name}=="
    for line in lock.read_text(encoding="utf-8").splitlines():
        if line.startswith(prefix):
            return line[len(prefix) :].split()[0]
    raise AssertionError(f"{lock} pins no {name}")


def _setup_py_version() -> str:
    """What `setup.py --version` reports, i.e. the version setuptools ships."""
    result = subprocess.run(
        [sys.executable, "setup.py", "--version"],
        cwd=REPO_ROOT / "python",
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        # `check=True` would raise CalledProcessError, whose message carries
        # the exit status and nothing else -- setuptools writes every
        # configuration error to stderr, which capture_output() then swallows.
        # That is how a metadata form the ambient setuptools could not parse
        # reached CI as a bare "returned non-zero exit status 1".
        raise AssertionError(
            f"`{sys.executable} setup.py --version` exited {result.returncode} "
            f"in {REPO_ROOT / 'python'}\n"
            f"--- stdout ---\n{result.stdout}\n"
            f"--- stderr ---\n{result.stderr}"
        )
    return result.stdout.strip()


def _build_requirement(name: str) -> Requirement:
    """The `[build-system].requires` entry for `name`, as a parsed Requirement."""
    build_requires = tomllib.loads(PYPROJECT.read_text(encoding="utf-8"))["build-system"][
        "requires"
    ]
    for raw in build_requires:
        requirement = Requirement(raw)
        if requirement.name.lower() == name:
            return requirement
    raise AssertionError(f"{PYPROJECT} declares no {name!r} build requirement: {build_requires}")


def _extension_include_dirs(setup_py: Path = SETUP_PY) -> list[str]:
    """The string entries of every `include_dirs = [...]` assignment in `setup_py`."""
    include_dirs: list[str] = []
    for node in ast.walk(ast.parse(setup_py.read_text(encoding="utf-8"))):
        if not isinstance(node, ast.Assign) or not isinstance(node.value, ast.List):
            continue
        if not any(
            isinstance(target, ast.Attribute) and target.attr == "include_dirs"
            for target in node.targets
        ):
            continue
        include_dirs.extend(
            element.value
            for element in node.value.elts
            if isinstance(element, ast.Constant) and isinstance(element.value, str)
        )
    return include_dirs


def _appended_extension_sources(setup_py: Path = SETUP_PY) -> set[tuple[str, ...]]:
    """The path parts of every `<list>.append(os.path.join(...))` call in `setup_py`."""
    return {
        tuple(
            part.value
            for part in node.args[0].args
            if isinstance(part, ast.Constant) and isinstance(part.value, str)
        )
        for node in ast.walk(ast.parse(setup_py.read_text(encoding="utf-8")))
        if isinstance(node, ast.Call)
        and isinstance(node.func, ast.Attribute)
        and node.func.attr == "append"
        and len(node.args) == 1
        and isinstance(node.args[0], ast.Call)
    }


def test_cython_extension_include_dirs_cover_private_and_public_core_headers():
    """The direct-source ADM extension must resolve libvmaf's public headers."""
    assert {"../core/src", "../core/include"} <= set(_extension_include_dirs()), (
        "the adm_dwt2 Cython extension directly includes core/src/feature/adm.c; "
        "its include_dirs must cover both private core/src and public core/include headers"
    )


def test_cython_extension_links_the_nonfinite_logger_implementation():
    """The text-included ADM source must not leave ``vmaf_log`` unresolved."""
    assert ("..", "core", "src", "log.c") in _appended_extension_sources(), (
        "adm.c now uses nonfinite_score.h logging helpers; the Cython extension "
        "must link core/src/log.c or its shared object imports with undefined symbol vmaf_log"
    )


def test_setup_metadata_version_matches_package_marker():
    """The release-please marker comment must not become the package version.

    `vmaf.__version__` carries the SemVer string release-please writes, which
    for a release candidate is a hyphenated prerelease such as ``1.0.0-rc.1``
    (ADR-1201). setuptools canonicalises whatever it is handed to PEP 440
    before publishing it, so the same release reaches `setup.py --version` as
    ``1.0.0rc1``. Comparing the two raw strings therefore fails on every RC
    even though nothing is wrong -- it did, on eleven build lanes at once, on
    the 1.0.0-rc.1 release PR.

    Comparing the parsed versions keeps what this test is actually for: a
    marker comment or a mangled substitution leaking into the shipped version
    still fails, either as an inequality or as `InvalidVersion`.
    """
    setup_version = _setup_py_version()

    assert Version(setup_version) == Version(vmaf.__version__), (
        f"setup.py ships {setup_version!r} but the package marker says "
        f"{vmaf.__version__!r}; these must describe the same release"
    )


def test_package_marker_is_a_valid_version():
    """A mangled release-please substitution must not parse as a version."""
    try:
        Version(vmaf.__version__)
    except InvalidVersion as exc:  # pragma: no cover - only on a broken release
        pytest.fail(f"vmaf.__version__ is not a valid version: {vmaf.__version__!r} ({exc})")

    assert "x-release-please" not in vmaf.__version__
    assert "${" not in vmaf.__version__


def test_build_backend_floor_covers_the_license_metadata_form():
    """`[build-system].requires` must exclude a setuptools that cannot read us.

    `[project].license` is a PEP 639 SPDX expression -- a bare string. Every
    setuptools before 77.0.1 rejects that shape outright, and the rejection is
    fatal: `setup.py --version` never gets as far as printing a version, so a
    stale build backend looks like a broken package rather than a stale build
    backend. `make cythonize` and the CI lanes run `setup.py` against the
    ambient interpreter, with no PEP 517 isolation to fetch a newer backend on
    their own, so the floor has to be declared here and honoured there.

    The table form (`license = { text = ... }`) needs no floor -- it is what
    old setuptools wants -- so this only asserts anything while we ship the
    SPDX expression.
    """
    project = tomllib.loads(PYPROJECT.read_text(encoding="utf-8"))["project"]
    if not isinstance(project["license"], str):
        return

    specifier = _build_requirement("setuptools").specifier
    for lock in BUILD_BACKEND_LOCKS:
        pinned = _locked_version(lock, "setuptools")
        assert specifier.contains(pinned), (
            f"{PYPROJECT} setuptools requirement {str(specifier)!r} excludes {pinned}, "
            f"the build backend {lock.relative_to(REPO_ROOT)} installs"
        )
    for version in PRE_PEP639_SETUPTOOLS:
        assert not specifier.contains(version), (
            f"{PYPROJECT} ships a PEP 639 license expression but its setuptools "
            f"requirement {str(specifier)!r} still admits {version}, which "
            "cannot parse one -- setup.py would abort with a configuration error"
        )
    for version in VULNERABLE_SETUPTOOLS:
        assert not specifier.contains(version), (
            f"{PYPROJECT} setuptools requirement {str(specifier)!r} still admits "
            f"{version}, a release with a known advisory"
        )


# ------------------------------------------------------------------ licences
#
# ADR-1250 makes each file's SPDX header the truth about its licence, and
# ADR-1513 has every Python package declare the union of the licences of the
# files it ships, with each text shipped through PEP 639 `license-files`.
# What a package ships is read from its build configuration:
#
# - hatchling: the source distribution holds the project directory and the
#   nearest .gitignore, and the wheel adds its force-included paths (hatchling's default sdist selection;
#   a package that configures its own sdist selection fails until it is
#   modelled);
# - setuptools (`vmaf`): the wheel holds the modules of the listed packages,
#   their package data, and the extension compiled from every `.pyx` in them
#   plus the sources setup.py appends, so it ships the code of each
#   repository file that extension includes.
#
# Files that are not tracked by git are not shipped by a clean checkout's
# build and are ignored; a shipped file whose licence neither its header nor
# REUSE.toml states fails. Licence texts named by `license-files` are not
# themselves licensed content and do not count.


def _tracked_files(root: Path, paths: list[Path]) -> set[Path]:
    """The regular files git tracks under `paths`, resolved."""
    relative = [path.resolve().relative_to(root.resolve()).as_posix() for path in paths]
    result = subprocess.run(
        ["git", "-C", str(root), "ls-files", "-z", "--", *relative],
        check=False,
        capture_output=True,
        text=True,
        timeout=GIT_TIMEOUT_S,
    )
    if result.returncode != 0:
        raise AssertionError(f"git ls-files exited {result.returncode} in {root}: {result.stderr}")
    files = (root / name for name in result.stdout.split("\0") if name)
    return {path.resolve() for path in files if path.is_file()}


def _hatch_shipped(root: Path, project_dir: Path, data: dict) -> set[Path]:
    build = data.get("tool", {}).get("hatch", {}).get("build", {})
    targets = build.get("targets", {})
    if HATCH_SELECTION_KEYS & set(build) or "sdist" in targets:
        raise AssertionError(f"{project_dir} selects its own sdist files; model that selection")
    force_included = targets.get("wheel", {}).get("force-include", {})
    roots = [project_dir, *_nearest_gitignore(root, project_dir)]
    return _tracked_files(root, [*roots, *(project_dir / source for source in force_included)])


def _nearest_gitignore(root: Path, project_dir: Path) -> list[Path]:
    """The .gitignore hatchling copies into the sdist: the first one found from
    the project directory up to the repository root."""
    top = root.resolve()
    for directory in (project_dir.resolve(), *project_dir.resolve().parents):
        if (directory / ".gitignore").is_file():
            return [directory / ".gitignore"]
        if directory == top:
            break
    return []


def _setup_arguments(setup_py: Path) -> dict:
    """The keyword arguments `setup_py` passes to setuptools.setup(), without building."""
    captured: dict = {}
    with mock.patch("setuptools.setup", lambda **kwargs: captured.update(kwargs)):
        runpy.run_path(str(setup_py), run_name="vmafx_setup_metadata_probe")
    return captured


def _package_directory(project_dir: Path, package_dir: dict, package: str) -> Path:
    """Where setuptools reads `package` from, given setup()'s `package_dir`."""
    parts = package.split(".")
    for cut in range(len(parts), -1, -1):
        prefix = ".".join(parts[:cut])
        if prefix in package_dir:
            return (project_dir / package_dir[prefix]).joinpath(*parts[cut:]).resolve()
    return project_dir.joinpath(*parts).resolve()


def _included_files(source: Path, include_dirs: list[Path], root: Path) -> list[Path]:
    """Repository files `source` includes (`#include`, Cython `cdef extern from`),
    resolved as a C preprocessor does: the includer's directory, then the include
    path. Conditional inclusion is not evaluated, so the set can only be larger."""
    text = source.read_text(encoding="utf-8", errors="replace")
    found = []
    for name in INCLUDE_LINE.findall(text) + CYTHON_EXTERN_LINE.findall(text):
        for base in (source.parent, *include_dirs):
            candidate = Path(os.path.normpath(base / name))
            if candidate.is_file() and candidate.resolve().is_relative_to(root.resolve()):
                found.append(candidate.resolve())
                break
    return found


def _include_closure(sources: list[Path], include_dirs: list[Path], root: Path) -> set[Path]:
    """`sources` and every repository file they include, transitively."""
    seen: set[Path] = set()
    pending = [source.resolve() for source in sources]
    for _ in range(MAX_INCLUDE_VISITS):
        if not pending:
            return seen
        source = pending.pop()
        if source not in seen:
            seen.add(source)
            pending.extend(_included_files(source, include_dirs, root))
    raise AssertionError(f"the include closure of {sources} exceeds {MAX_INCLUDE_VISITS} visits")


def _setuptools_shipped(root: Path, project_dir: Path) -> set[Path]:
    setup_py = project_dir / "setup.py"
    arguments = _setup_arguments(setup_py)
    shipped: set[Path] = set()
    extension_sources = [
        project_dir.joinpath(*parts) for parts in _appended_extension_sources(setup_py)
    ]
    for package in arguments["packages"]:
        directory = _package_directory(project_dir, arguments.get("package_dir", {}), package)
        patterns = arguments.get("package_data", {}).get(package, [])
        for path in _tracked_files(root, [directory]):
            relative = path.relative_to(directory).as_posix()
            if path.parent == directory and path.suffix == ".py":
                shipped.add(path)
            elif any(fnmatch.fnmatchcase(relative, pattern) for pattern in patterns):
                shipped.add(path)
            if path.parent == directory and path.suffix == ".pyx":
                extension_sources.append(path)
    include_dirs = [project_dir / entry for entry in _extension_include_dirs(setup_py)]
    return shipped | _include_closure(extension_sources, include_dirs, root)


def _shipped_files(root: Path, pyproject: Path, data: dict) -> set[Path]:
    """Every repository file the package's sdist or wheel carries."""
    backend = data["build-system"].get("build-backend", "setuptools.build_meta:__legacy__")
    if backend == HATCH_BACKEND:
        return _hatch_shipped(root, pyproject.parent, data)
    if backend in SETUPTOOLS_BACKENDS:
        return _setuptools_shipped(root, pyproject.parent)
    raise AssertionError(f"{pyproject}: what the build backend {backend!r} ships is not modelled")


@functools.cache
def _reuse_annotations(root: Path) -> list[dict]:
    return licensing.load_reuse(root)


def _shipped_licences(root: Path, files: set[Path]) -> tuple[set[str], list[str]]:
    """(identifiers in the shipped files' SPDX expressions, files that state none)."""
    identifiers: set[str] = set()
    unlicensed: list[str] = []
    for path in sorted(files):
        relative = path.relative_to(root.resolve()).as_posix()
        expression, _ = licensing.file_licence(path, relative, _reuse_annotations(root))
        if expression:
            identifiers |= licensing.spdx_ids(expression)
        else:
            unlicensed.append(relative)
    return identifiers, unlicensed


def _licence_text_problems(
    name: str, project_dir: Path, project: dict, identifiers: set[str]
) -> list[str]:
    """PEP 639 texts: `LICENSES/<id>.txt` for exactly the shipped identifiers,
    byte for byte the repository's copy (licensing.json `spdx_texts`)."""
    if project.get("license-files") != ["LICENSES/*"]:
        return [
            f"{name}: license-files must be ['LICENSES/*'], is {project.get('license-files')!r}"
        ]
    shipped = {path.stem for path in (project_dir / "LICENSES").glob("*.txt")}
    problems = [f"{name}/LICENSES lacks {i}.txt" for i in sorted(identifiers - shipped)]
    problems += [
        f"{name}/LICENSES/{i}.txt is no shipped licence" for i in sorted(shipped - identifiers)
    ]
    texts = licensing.load_manifest()["spdx_texts"]
    for identifier in sorted(identifiers & shipped):
        reference = REPO_ROOT / texts[identifier]
        if (project_dir / "LICENSES" / f"{identifier}.txt").read_bytes() != reference.read_bytes():
            problems.append(f"{name}/LICENSES/{identifier}.txt differs from {texts[identifier]}")
    return problems


def _package_licence_problems(root: Path, pyproject: Path) -> list[str]:
    """Why `pyproject`'s licence metadata does not describe the files it ships."""
    name = pyproject.parent.resolve().relative_to(root.resolve()).as_posix()
    data = tomllib.loads(pyproject.read_text(encoding="utf-8"))
    project = data["project"]
    texts = {
        path.resolve()
        for pattern in project.get("license-files", [])
        for path in pyproject.parent.glob(pattern)
    }
    identifiers, unlicensed = _shipped_licences(root, _shipped_files(root, pyproject, data) - texts)
    declared = project.get("license")
    problems = [f"{path} states no licence (SPDX header or REUSE.toml)" for path in unlicensed]
    if not isinstance(declared, str) or " OR " in f" {declared} ":
        problems.append(f"{name}: license must be a PEP 639 AND expression, is {declared!r}")
    elif licensing.spdx_ids(declared) != identifiers:
        problems.append(
            f"{name}: license = {declared!r}, but its files carry {' AND '.join(sorted(identifiers))}"
        )
    return problems + _licence_text_problems(name, pyproject.parent, project, identifiers)


@pytest.mark.parametrize(
    "pyproject", PACKAGE_PYPROJECTS, ids=lambda path: path.parent.relative_to(REPO_ROOT).as_posix()
)
def test_every_python_package_declares_the_licences_of_the_files_it_ships(pyproject: Path):
    """The PEP 639 expression is the union of the shipped files' SPDX identifiers
    (ADR-1250, ADR-1513), and the package ships each licence's text."""
    problems = _package_licence_problems(REPO_ROOT, pyproject)
    assert not problems, "\n".join(problems)


def _git_tree(root: Path, files: dict[str, str]) -> Path:
    """A git work tree at `root` holding `files` (path -> text), all staged."""
    for name, text in files.items():
        (root / name).parent.mkdir(parents=True, exist_ok=True)
        (root / name).write_text(text, encoding="utf-8")
    for command in (["init", "-q"], ["add", "-A"]):
        subprocess.run(["git", "-C", str(root), *command], check=True, timeout=GIT_TIMEOUT_S)
    return root


def _hatch_package(licence: str, module_licence: str) -> dict[str, str]:
    tag = "SPDX-" + "License-Identifier:"
    return {
        "REUSE.toml": "version = 1\n",
        "pkg/pyproject.toml": (
            f"# {tag} EUPL-1.2\n"
            f'[project]\nname = "pkg"\nversion = "1"\nlicense = "{licence}"\n'
            'license-files = ["LICENSES/*"]\n'
            '[build-system]\nrequires = ["hatchling"]\nbuild-backend = "hatchling.build"\n'
            '[tool.hatch.build.targets.wheel.force-include]\n"../shared" = "pkg/shared"\n'
        ),
        "pkg/src/pkg/__init__.py": f"# {tag} {module_licence}\n",
        "shared/data.py": f"# {tag} {module_licence}\n",
    }


def _with_texts(root: Path, identifiers: list[str]) -> None:
    texts = licensing.load_manifest()["spdx_texts"]
    (root / "pkg" / "LICENSES").mkdir(parents=True, exist_ok=True)
    for identifier in identifiers:
        shutil.copyfile(
            REPO_ROOT / texts[identifier], root / "pkg" / "LICENSES" / f"{identifier}.txt"
        )


def test_the_licence_check_refuses_metadata_its_files_contradict(tmp_path: Path):
    """Planted defect: an EUPL-1.2 package that declares BSD-2-Clause-Patent."""
    _with_texts(tmp_path, ["BSD-2-Clause-Patent"])
    root = _git_tree(tmp_path, _hatch_package("BSD-2-Clause-Patent", "EUPL-1.2"))
    problems = _package_licence_problems(root, root / "pkg" / "pyproject.toml")
    assert any("but its files carry EUPL-1.2" in problem for problem in problems), problems
    assert any("lacks EUPL-1.2.txt" in problem for problem in problems), problems


def test_the_licence_check_counts_force_included_files_outside_the_project(tmp_path: Path):
    """A force-included file's licence joins the union; a matching declaration passes."""
    files = _hatch_package("EUPL-1.2 AND MIT", "EUPL-1.2")
    files["shared/data.py"] = "# " + "SPDX-" + "License-Identifier: MIT\n"
    _with_texts(tmp_path, ["EUPL-1.2", "MIT"])
    root = _git_tree(tmp_path, files)
    assert _package_licence_problems(root, root / "pkg" / "pyproject.toml") == []


def test_the_licence_check_refuses_a_shipped_file_without_a_licence(tmp_path: Path):
    files = _hatch_package("EUPL-1.2", "EUPL-1.2")
    files["pkg/src/pkg/untagged.py"] = "VALUE = 1\n"
    _with_texts(tmp_path, ["EUPL-1.2"])
    root = _git_tree(tmp_path, files)
    problems = _package_licence_problems(root, root / "pkg" / "pyproject.toml")
    assert problems == ["pkg/src/pkg/untagged.py states no licence (SPDX header or REUSE.toml)"]


def test_the_licence_check_refuses_a_text_that_is_not_the_repository_copy(tmp_path: Path):
    _with_texts(tmp_path, ["EUPL-1.2"])
    (tmp_path / "pkg" / "LICENSES" / "EUPL-1.2.txt").write_text(
        "not the licence\n", encoding="utf-8"
    )
    root = _git_tree(tmp_path, _hatch_package("EUPL-1.2", "EUPL-1.2"))
    problems = _package_licence_problems(root, root / "pkg" / "pyproject.toml")
    assert problems == ["pkg/LICENSES/EUPL-1.2.txt differs from LICENSES/EUPL-1.2.txt"]


def test_the_extension_closure_follows_cython_externs_and_includes(tmp_path: Path):
    """The compiled extension carries every repository file it includes."""
    root = _git_tree(
        tmp_path,
        {
            "pkg/core/ext.pyx": 'cdef extern from "../../src/a.c":\n    pass\n',
            "src/a.c": '#include "inc/b.h"\n#include <stdio.h>\n',
            "include/inc/b.h": '#  include "c.h"\n',
            "include/inc/c.h": "",
            "src/unused.h": "",
        },
    )
    closure = _include_closure([root / "pkg/core/ext.pyx"], [root / "include"], root)
    names = {path.relative_to(root.resolve()).as_posix() for path in closure}
    assert names == {"pkg/core/ext.pyx", "src/a.c", "include/inc/b.h", "include/inc/c.h"}
