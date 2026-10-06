- `make docs-build` runs the strict MkDocs build the docs workflow and the
  pre-push gate run, and `make docs-serve` starts the live preview. The Metal
  lane comment of the build matrix no longer calls the build stub-only, ADR-0581
  names ADR-0597 in its status line, and the rebase notes carry the
  `vmaf_cuda_picture_get_pix_fmt()` accessor of PR #1118.
