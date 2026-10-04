- **`pkg/codecadapter` defines each codec once.** Eight codecs (libx264, libx265,
  libaom-av1, libvvenc, libsvtav1, libvpx-vp9, prores_videotoolbox,
  av1_videotoolbox) were written twice, a constructor nothing called and the
  literal in the registry list. The lists call the constructors; no argv, range
  or probe value changes. `TestEveryCodecIsDefinedOnce` keeps it that way.
