- **RC4 adds a new VMAFx C API and moves the FFmpeg filters to VMAFx names
  (decision record, ADR-1852).** The new API (`vmafx/*.h`, `libvmafx.so.1`)
  and every other surface (bindings, CLI / FFmpeg / MCP / gRPC option tables,
  reference docs) are generated from one definition; `libvmaf.h` stays as a
  separate, deprecated compatibility library until 2.0. Every FFmpeg filter
  capability of the patch series continues under a VMAFx name (`vmafx`,
  `vmafx_tune`, `vmafx_pre`, `-vmafx-profile`), and the `vmaf`-named filters
  are removed in the same RC4 change. Nothing changes in this release; see
  [ADR-1852](docs/adr/1852-vmafx-api-redesign.md) and the
  [roadmap](docs/roadmap.md).
