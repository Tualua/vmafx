- **The FFmpeg `libvmaf_sycl` filter no longer scores fewer frames than it
  decoded, or the wrong surfaces.**
  - When a VA surface import failed, the filter used to pass the frame on
    unscored, so the pooled score covered fewer frames than the input
    without saying so. Now it tries again, three tries in all with 1 ms
    between them, and then stops with an error that names the frame. It
    prints no score after stopping
    ([ADR-1761](docs/adr/1761-sycl-filter-import-retry-then-fail.md)).
  - The reference input's surfaces were imported with the distorted
    input's VA display. With the two decoders on two VA devices, that
    scored other surfaces without an error: 0 of 24 frames equal to the
    CPU on an Arc A380, and a plausible pooled score. When a surface did
    not exist in that display, the import failed, which is the failure the
    skip used to hide. Each input is now imported with its own display.
  - The software path also left the last chroma row of an odd-height frame
    at zero; it now copies every row.
  - The `libvmaf_metal` filter already stopped on a failed import, but then
    printed a pooled score over the frames before the failure. It now
    prints no score after it stops, as `libvmaf_sycl` does.
