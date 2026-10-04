- **The dev container is pushed only into a private package.**
  `dev-container-publish.yml` reads the visibility of
  `ghcr.io/vmafx/vmafx-dev-mcp` before it builds and refuses to push unless
  the package is private, also when the visibility cannot be read: the image
  holds the full CUDA toolkit, Intel's oneAPI Base Kit and the ROCm payload,
  which may be used internally but not redistributed
  ([ADR-1564](docs/adr/1564-dev-image-private-guard.md),
  [publishing](docs/development/publishing.md)).
