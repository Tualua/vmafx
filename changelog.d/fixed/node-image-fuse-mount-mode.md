- **Mount mode works in the node image
  ([ADR-1593](docs/adr/1593-helm-node-fuse-and-ebpf.md)).** The published
  `vmafx-node` image had no FUSE helper, so a node with
  `VMAFX_STORAGE_MODE=mount` refused to start. The image now carries the
  setuid `fusermount3` and the util-linux `mount` and `umount` it runs, listed
  in the licence record with their Debian sources in the `-source` image. A
  container needs `/dev/fuse` and the capabilities `SYS_ADMIN` and
  `DAC_READ_SEARCH`; the node process itself keeps none.
