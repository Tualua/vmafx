- **`scripts/dev/speed_gpu_parity.py` can leave a fixture out, with a stated reason.**
  `--skip-fixture 3840x2160 --skip-reason "<why>"` skips the untracked BBB fixture and prints a
  `SKIPPED` line; a missing fixture file without the option is now a usage error naming the
  file ([SpEED](docs/metrics/speed_qa.md#checking-a-gpu-twin-against-the-cpu)). FFmpeg patch
  impact: none.
