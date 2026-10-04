- **Helm: `storage.mode` accepts `http-serve`, `mount` and `auto`.** The
  previous `rclone` value matched no node mode and was ignored; the schema now
  refuses it. Use `http-serve` (the default) or `auto`. `storage.mountRoot`
  sets `VMAFX_STORAGE_MOUNT_ROOT`, and `VMAFX_RCLONE_CONFIG` is set only when
  `storage.rclone.config` provides the file. See
  [ADR-1526](docs/adr/1526-node-storage-streamed-inputs.md).
