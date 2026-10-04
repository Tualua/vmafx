- **`vmaf-tune` checks the coarse-to-fine window against the adapter, honours
  `ladder --workdir` and `--max-concurrent-decodes`, gives `auto --execute` the
  source geometry, and names a missing uncertainty interval.** The
  coarse-to-fine search used libx264's fixed 10..50 window for every encoder
  and died with a traceback for libx265, libsvtav1, libvvenc, AMF and ProRes;
  it now searches the part of
  each adapter's `quality_range` inside that window, refines toward the right
  side for `-q:v` adapters, and refuses a window the adapter rejects before the
  first encode (exit 2). `ladder` puts each rung's scratch directory under
  `--workdir` and caps reference decodes with `--max-concurrent-decodes`.
  `auto --execute` probes a container source's width, height, frame rate and
  pixel format and takes `--width/--height/--framerate/--pix-fmt` for raw YUV,
  refusing raw YUV without them. `recommend --with-uncertainty` on a corpus
  without interval columns prints `uncertainty=unavailable` and a stderr note
  instead of answering silently without intervals, and reports
  `rows_examined=N/M`.
