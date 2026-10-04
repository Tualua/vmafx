- **`vmaf-tune` honours `--vmaf-model` and `--neg`, keys its cache on every
  input, gives real QSV encodes their device chain, names each ladder rung's
  codec, and emits AMF's rate control once.** `corpus` and live `recommend`
  scored every row with the height-rule model whatever `--vmaf-model` or
  `--neg` said, and `ladder --neg` was ignored; an explicit `--vmaf-model` now
  scores every row with it, `--neg` takes the NEG variant of the model that
  applies, and stderr names the choice. The encode cache now keys on the
  adapter and ffmpeg versions, the pass count, the sample-clip window, the
  geometry, the model and the backend (old entries miss), and a hit replays the
  miss row. Every QSV encode (Python and `vmafx-tune-go`) carries the VA-API /
  QSV device chain and the upload filter, with the QSV device as the filter
  device, and `corpus`, `recommend` and `ladder` probe a hardware encoder,
  VideoToolbox included, before the first encode (exit 2 when the host cannot
  run it). The HLS and DASH writers name each rung's RFC 6381 codec string,
  read from a two-frame encode, instead of `avc1.640028`; VVC and ProRes
  ladders refuse those formats. AMF encodes carry `-quality / -rc / -qp_i /
  -qp_p` once.
