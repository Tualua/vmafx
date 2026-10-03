- Restored the section links that older pages and ADRs use into the CLI,
  `vmaf_bench`, environment-variable and Getting started pages after their
  rewrite (#1934, #1938): each former section name is a short heading that
  points to the section now holding its content, and Getting started keeps its
  "Build from source (any platform)" heading. `mkdocs build --strict` passes
  on master again.
