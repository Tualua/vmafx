- **The images published for 1.0.0-rc.1 and rc.2 are completed or withdrawn.**
  The ROCm images (they carried a binary-only AMD library whose licence
  forbids distributing it) are withdrawn from the registry, and the whole
  `vmafx-node` package is deleted (its FFmpeg was built `--enable-nonfree`);
  the rc.3 node image starts a new package. `vmaf-mcp` 1.0.0rc1 and 1.0.0rc2
  are yanked on PyPI for their wrong licence metadata. Every other rc.1 and
  rc.2 image stays unchanged and gets its notices on the release page, the
  source of its copyleft parts as `<tag>-source`, and an SBOM attested on its
  digest. The release files and `models.tar.gz` get their notices on the same
  pages. A manual workflow does this from recorded digests and licence scans
  of the release tags' builds; the licence tool now fetches Ubuntu sources
  from Launchpad
  ([ADR-1578](docs/adr/1578-published-rc-licence-companions.md),
  [licensing](docs/licensing.md#releases-100-rc1-and-100-rc2)).
