- **The files attached to a GitHub release carry their licence notices.** A
  release now has `THIRD_PARTY_NOTICES.txt` (every component, licence and
  copyright line of `libvmaf` and `vmaf`, computed from the SPDX headers of the
  files the build compiled) and `licenses.tar.gz` (the same notices with every
  licence text), and `models.tar.gz` carries a `licenses/` directory for the
  models in it; the release build fails when a file has no recorded licence. The
  SPDX SBOMs are attested on the release files and on the `vmaf-mcp` wheel and
  sdist ([release](docs/development/release.md#what-is-signed),
  [licensing](docs/licensing.md)).
