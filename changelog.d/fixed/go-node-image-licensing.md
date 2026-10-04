- **The `vmafx-operator`, `vmafx-server` and `vmafx-node` images carry the
  licences of everything they link, and the node image's FFmpeg is
  redistributable.** Each Go program's modules are read from its build
  information; their licence and notice files ship under
  `/usr/local/share/vmafx/licenses/go/`, the notices list every module with its
  licence, and the source of the copyleft modules (MPL-2.0, EUPL-1.2, LGPL) is
  published as `<image>:<tag>-source`, checked against the binary's module sums.
  The node image's FFmpeg is no longer configured `--enable-nonfree` (which made
  the binary declare itself not redistributable although no nonfree part was
  built); it is published under the GNU GPL version 3 or later with its exact
  patched source and configure line. The libraries the node image copies out of
  Debian packages keep their copyright files and package records, and rclone is
  built from its release's module source (`RCLONE_VERSION` in `build-config.env`
  replaces `RCLONE_IMAGE`), so its source is exact. Every image build fails on a
  file or module without a recorded licence
  ([ADR-1514](docs/adr/1514-go-and-node-image-licensing.md),
  [licensing](docs/licensing.md)).
