---
paths:
  - ai/scripts/export_u2netp_mirror.py
  - LICENSES/LicenseRef-Apache-2.0-u2netp.txt
  - docs/ai/u2netp-mirror.md
invariant: Never commit u2netp weights to git; exporter imports audited upstream; Apache-2.0 notices stay paired.
---
<!-- markdownlint-disable MD013 MD060 -->
# `u2netp` fork-local mirror invariants (ADR-0412 / ADR-0671)

Fork ships release-artefact mirror for upstream U-2-Net
`u2netp` checkpoint via GitHub Release attachments. Scaffold
(license, model card, operator doc, supply-chain staging step)
landed in PR scope ADR-0412; exporter landed in ADR-0671; binary
upload is separate PR.

- **Never commit `model/u2netp_mirror.onnx` or
  `model/u2netp_mirror.pth` to git.** Both paths are gitignored
  (see `.gitignore`). Binary lives in GitHub Release assets
  only — signed via Sigstore, hashed as provenance subject, paired with
  `LICENSES/LicenseRef-Apache-2.0-u2netp.txt` at upload time. Binary
  upload PR ever attempting to commit either file -> ADR-0412
  contract is broken; reject PR.
- **Exporter imports upstream code; does not vendor it.**
  `ai/scripts/export_u2netp_mirror.py` expects audited local
  `xuebinqin/U-2-Net` checkout plus `u2netp.pth`, then exports
  ONNX and `u2netp-mirror-export-manifest-v1` sidecar. Keep this
  boundary intact: copying U-2-Net source into this repository or
  silently accepting non-Apache license text breaks ADR-0671.
- **Recommended saliency weights remain
  `saliency_student_v1`** (ADR-0286, fork-trained DUTS student
  under BSD-2-Clause-Patent). `u2netp_mirror` is named
  *fallback* for upstream-lineage citation, comparative
  evaluation, or downstream pipelines pinned to upstream
  behaviour. Do NOT flip `model/tiny/registry.json`'s default
  `mobilesal` resolution to `u2netp_mirror_v1` without ADR
  superseding ADR-0286.
- **Apache-2.0 §4 () + (c) compliance is non-negotiable.**
  Every release that carries `u2netp_mirror_v*` must also carry
  `LICENSES/LicenseRef-Apache-2.0-u2netp.txt` with its attribution block
  intact. supply-chain.yml staging step pairs them
  automatically; future refactor decoupling them -> downstream
  operators inherit license-non-compliant artefact. §4 (b)
  applies only to ONNX rewraps (export script writes   `metadata_props` block recording conversion provenance);
  verbatim `.pth` redistribution does not trigger (b). §4 (d) is
  moot — upstream ships no NOTICE file.
- **Binary upload PR re-pins upstream commit.** Scaffold-time
  pin is HEAD `ac7e1c81`. Binary upload PR
  must verify upstream `LICENSE` SPDX is still Apache-2.0
  and tree still carries no NOTICE file at
  upload-time HEAD, then bump model card's commit pin
  accordingly.
