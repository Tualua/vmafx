- **`vmaf-tune` help texts match the code, its ADR references name the right
  records, and `fast` says when the proxy scores an encoder as `unknown`.**
  `ladder --crf-sweep` names the sampler's sweep (`20,25,30,35,40`), the
  `auto` help counts its ten short-circuits, `corpus --two-pass` lists the five
  adapters that run a 2-pass encode, and `compare` / `tune-per-shot --workdir`
  cite ADR-0598. A production `fast` run with an encoder outside the proxy's
  vocabulary (`libaom-av1`, AMF, VideoToolbox) now notes on stderr that the
  proxy used its `unknown` slot and adds `"proxy_encoder_slot": "unknown"` to
  the JSON. ADR numbers in the help, the usage pages and the code comments that
  pointed at renumbered, unrelated records now point at the vmaf-tune records
  they meant.
