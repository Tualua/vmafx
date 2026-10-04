- Every published image, the dev container included, now stores its layers as zstd
  at BuildKit's strongest level (ADR-1594): 5 to 25 % smaller downloads (the CPU
  tester image 268 to 200 MB). Pulling them needs Docker Engine 23.0 or later,
  Docker Desktop 4.19 or later, containerd 1.5 or later, or Podman; an older Docker
  stops with `failed to register layer: ... archive/tar: invalid tar header`
  (`docs/usage/docker.md`, "What can pull the images"). Images published up to
  `v1.0.0-rc.2` keep gzip layers.
- The Windows tester zips are encoded by zopfli: still Deflate, which every Windows
  tool opens, and 3.7 to 4.2 % smaller than zlib's strongest level (the CUDA zip
  344 to 329 MB).
