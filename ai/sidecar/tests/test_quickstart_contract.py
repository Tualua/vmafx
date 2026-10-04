# SPDX-License-Identifier: EUPL-1.2
# Copyright 2026 Lusoris
#
# ai/sidecar/tests/test_quickstart_contract.py — regression tests for standalone
# quick-start documentation contract and writable checkpoint configuration.
#
# ADR-0781: sidecar online training — SGD + EMA + replay buffer.
# ADR-1309: socket path ownership and owner-only mode.

from __future__ import annotations

import pathlib
import re
import tempfile
import unittest.mock as mock

import pytest

from ai.sidecar import online_trainer as ot_mod


class TestQuickstartDocumentationContract:
    """Fail-closed regression tests for the standalone sidecar documentation contract."""

    @classmethod
    def _doc_path(cls) -> pathlib.Path:
        repo_root = pathlib.Path(__file__).resolve().parents[3]
        return repo_root / "docs" / "ai" / "sidecar-online-training.md"

    def test_doc_exists_and_readable(self) -> None:
        doc = self._doc_path()
        assert doc.is_file(), f"Documentation missing at {doc}"

    @staticmethod
    def _bash_blocks(section: str) -> list[str]:
        """Return the dedented body of every ```bash fence in ``section``.

        Fences may be indented (a numbered list item), so the fence's own
        indent is stripped from each body line.
        """
        blocks: list[str] = []
        for match in re.finditer(
            r"^([ \t]*)```bash\n(.*?)\n\1```", section, re.DOTALL | re.MULTILINE
        ):
            indent = match.group(1)
            lines = match.group(2).split("\n")
            blocks.append("\n".join(ln.removeprefix(indent) for ln in lines))
        return blocks

    @classmethod
    def _sections(cls, doc_text: str) -> list[str]:
        """Split the page at its level-2 headings (the heading text is not pinned)."""
        return re.split(r"^## ", doc_text, flags=re.MULTILINE)[1:]

    @classmethod
    def _launch_section(cls, doc_text: str) -> str:
        """The one section that starts the server; found by content, not by title."""
        found = [
            sec
            for sec in cls._sections(doc_text)
            if any("ai.sidecar.online_trainer" in b for b in cls._bash_blocks(sec))
        ]
        assert len(found) == 1, "exactly one section must carry the standalone launch procedure"
        return found[0]

    def test_quickstart_configures_writable_checkpoint_and_cleanup(self) -> None:
        doc_text = self._doc_path().read_text(encoding="utf-8")

        # The launch procedure may span several fenced blocks (one per step).
        section = self._launch_section(doc_text)
        snippet = "\n".join(self._bash_blocks(section))

        # Must allocate private runtime dir
        assert 'runtime_dir="$(mktemp -d)"' in snippet
        # Must include shell-safe cleanup trap
        assert "trap 'rm -rf \"$runtime_dir\"' EXIT INT TERM" in snippet
        # Must restrict runtime permissions
        assert 'chmod 700 "$runtime_dir"' in snippet
        # Must allocate a writable checkpoint directory
        assert 'mkdir -p "$runtime_dir/checkpoints"' in snippet
        # Must configure socket override
        assert 'VMAFX_SIDECAR_SOCKET="$runtime_dir/vmafx-sidecar.sock"' in snippet
        # Must explicitly configure writable checkpoint directory
        assert 'VMAFX_SIDECAR_CHECKPOINT_DIR="$runtime_dir/checkpoints"' in snippet
        # Must invoke module
        assert "python -m ai.sidecar.online_trainer" in snippet

        # Document must explicitly explain the /mnt container default and PermissionError risk
        assert "/mnt/vmafx-models/online" in doc_text
        assert "PermissionError" in doc_text

    def test_no_standalone_snippet_omits_checkpoint_dir(self) -> None:
        doc_text = self._doc_path().read_text(encoding="utf-8")
        launches = 0
        # A launch command is safe only if its own section (the procedure it
        # belongs to) also sets the checkpoint directory and the cleanup trap.
        for section in self._sections(doc_text):
            blocks = self._bash_blocks(section)
            if not any("ai.sidecar.online_trainer" in b for b in blocks):
                continue
            launches += 1
            joined = "\n".join(blocks)
            assert (
                "VMAFX_SIDECAR_CHECKPOINT_DIR=" in joined
            ), "Standalone invocation snippet lacks VMAFX_SIDECAR_CHECKPOINT_DIR override"
            assert "trap " in joined, "Standalone invocation snippet lacks cleanup trap"
        assert launches >= 1, "no standalone launch snippet found; the check would be vacuous"

    def test_contract_rejects_launch_without_checkpoint_dir(self) -> None:
        # Negative case: the section scanner must flag a launch that omits the override.
        bad = "## Run\n\n    ```bash\n    python -m ai.sidecar.online_trainer\n    ```\n"
        blocks = self._bash_blocks(self._sections(bad)[0])
        assert blocks == ["python -m ai.sidecar.online_trainer"]
        assert "VMAFX_SIDECAR_CHECKPOINT_DIR=" not in "\n".join(blocks)


class TestCheckpointDirectoryContract:
    """Functional tests verifying checkpoint directory configuration and failure modes."""

    def test_default_checkpoint_dir_targets_container_mount(self) -> None:
        assert ot_mod._CHECKPOINT_DIR == "/mnt/vmafx-models/online"

    def test_unwritable_checkpoint_dir_fails_closed(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            ro_parent = pathlib.Path(td) / "readonly_root"
            ro_parent.mkdir(parents=True, exist_ok=True)
            ro_parent.chmod(0o500)
            target_ckpt = ro_parent / "unwritable_ckpt"

            try:
                with (
                    mock.patch("ai.sidecar.online_trainer._load_base_model"),
                    mock.patch("ai.sidecar.online_trainer.SGDEMATrainer"),
                    pytest.raises(PermissionError),
                ):
                    ot_mod.OnlineTrainer(n_features=8, checkpoint_dir=str(target_ckpt))
            finally:
                ro_parent.chmod(0o700)

    def test_writable_checkpoint_dir_creates_and_succeeds(self) -> None:
        with tempfile.TemporaryDirectory() as td:
            target_ckpt = pathlib.Path(td) / "checkpoints"
            assert not target_ckpt.exists()

            with (
                mock.patch("ai.sidecar.online_trainer._load_base_model"),
                mock.patch("ai.sidecar.online_trainer.SGDEMATrainer"),
            ):
                trainer = ot_mod.OnlineTrainer(n_features=8, checkpoint_dir=str(target_ckpt))
                assert target_ckpt.is_dir()
                assert trainer._checkpoint_dir == target_ckpt
