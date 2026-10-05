- **The licence pages say what an embedder of VMAFx owes.** The README,
  `docs/licensing.md` and the GPU API page said the EUPL-1.2 source obligation
  applies to a *modified* `libvmaf`. Article 5 of the EUPL-1.2 applies to every
  copy you distribute, modified or not: provide the source or point to a
  repository that has it. The API page also listed `libvmaf_cuda.h` as
  EUPL-1.2; it is Netflix's BSD-2-Clause-Patent header. `docs/licensing.md`
  gains a section, *Embedding VMAFx in another product*. It covers
  distribution, static and dynamic linking, modification, network use and
  patents under both licences, quotes the EUPL text and the European
  Commission's FAQ with links, and says it is not legal advice. ADR-1250 (the
  EUPL relicence) and ADR-1199 (the CUDA picture handover barrier) are
  recorded as Accepted, since both are applied.
