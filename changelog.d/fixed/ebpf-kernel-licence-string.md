- **The node's eBPF program declares `"GPL"` to the kernel instead of
  `"Dual BSD/GPL"`
  ([ADR-1559](docs/adr/1559-ebpf-kernel-licence-string.md)).** The source of
  the descriptor tracker is EUPL-1.2, but its compiled object claimed a BSD
  grant the project never made. Loaded into the kernel, the program is
  combined with GPL-2.0 code and calls GPL-only helpers, which EUPL-1.2's
  compatibility clause (Article 5, GPL v2 and v3 in its Appendix) allows to be
  distributed under the GPL. The object loads as before; only its licence
  section changed.
