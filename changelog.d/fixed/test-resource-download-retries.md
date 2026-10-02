- **Golden tests no longer fail on a dropped connection while downloading
  test videos.** `vmaf.config` fetches missing test resources from
  github.com/Netflix/vmaf_resource; a timed-out or reset connection used to
  fail the test. Transient network errors are now retried (4 attempts, 2, 4
  and 8 s apart); HTTP errors such as 404 still fail at once
  ([ADR-1594](docs/adr/1594-vmafx-sycl-ffmpeg-container.md)).
