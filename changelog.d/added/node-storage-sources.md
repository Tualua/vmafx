- **`vmafx-node` scores jobs whose sources are rclone remotes or http(s)
  URLs.** The executor now prepares a job's reference and distorted through
  `pkg/storage` (`VMAFX_STORAGE_MODE`: `http-serve`, `mount` or `auto`, the
  default); before, it handed them to the vmaf CLI unchanged, so only local
  paths worked. In `http-serve` mode, and for any http(s) URL, the clips are
  streamed into the CLI through pipes without touching the node's disk, and a
  stream that breaks fails the job instead of yielding the score of the frames
  it delivered. `auto` picks `mount` when FUSE is usable and logs its choice;
  an unknown mode, or `mount` without FUSE, stops the node at startup.
  `libvmaf.Scorer.ScoreReaders` and `storage.Open` are new; `storage.New` is
  deprecated. See [job sources](docs/server/node.md#job-sources-local-paths-urls-and-rclone-remotes)
  and [ADR-1526](docs/adr/1526-node-storage-streamed-inputs.md).
