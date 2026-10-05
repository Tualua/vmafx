# Dependency advisories

This page covers how a security advisory against a third-party dependency is
triaged. Vulnerabilities in VMAFx's own code are reported through
[SECURITY.md](https://github.com/VMAFx/vmafx/blob/master/SECURITY.md).

## Where advisories show up

- **Python:** the dependency dashboard (issue #941, Renovate) lists OSV
  advisories against a declared dependency. It lists one entry per package that
  declares the dependency.
- **Go:** module advisories from the Go vulnerability database.

## Triage, in order

1. **A fixed release exists.** Update to it. Renovate opens the pull request;
   it lands like any dependency update.
2. **No fixed release, and the dependency is not needed where it is
   declared.** Remove it from that package. torch is the standing example: it
   may appear only in the two training environments (`ai/`,
   `tools/ensemble-training-kit/`), and `scripts/ci/check-torch-scope.py` fails
   when any other package names a torch-family distribution in any dependency
   group, or when one of its locks resolves one
   ([ADR-1886](../adr/1886-torch-training-environments-only.md)).
3. **No fixed release, and the dependency is needed.** Record an
   [OpenVEX](https://github.com/openvex/spec/blob/main/OPENVEX-SPEC.md)
   statement under `security/vex/`. A `not_affected` statement names one of
   the specification's five justifications and an `impact_statement` with the
   evidence: which function the advisory concerns, and where the code does not
   reach it. `scripts/ci/tests/test_openvex_documents.py` rejects a malformed
   document.
4. **The code is affected.** Fix or mitigate it under the timelines of
   SECURITY.md, and record the statement as `affected` with an
   `action_statement`.

Re-read a statement when the dependency publishes a fix, or when our code
starts to call an affected function.

## Current statements

| Document | Dependency | Advisories | Products | Status |
| --- | --- | --- | --- | --- |
| `security/vex/torch.openvex.json` | torch 2.14.1 | PYSEC-2025-189, -190, -192 to -197, -210 (no fixed release in OSV) | vmaf-train (`ai/`), vmaf-ensemble-training-kit | `not_affected`: no profiler, RNN, TorchScript loading or eager-quantized modules in the training code; `torch.jit.script` and the CUDA allocator never receive adversary input |

## Checking a change locally

```bash
python3 scripts/ci/check-torch-scope.py
python3 -m unittest scripts/ci/tests/test_check_torch_scope.py scripts/ci/tests/test_openvex_documents.py
```

The check also runs as the `check-torch-scope` pre-commit hook, and the
`Lint` workflow (`lint-and-format.yml`) runs every pre-commit hook.
