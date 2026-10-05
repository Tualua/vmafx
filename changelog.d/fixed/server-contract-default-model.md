- **The server's API contracts name the default model it actually uses.** The
  gRPC proto, the OpenAPI document and the server pages said an omitted
  `model` selects `vmaf_v0.6.1`; `vmafx-server` uses the library default,
  `vmaf_v1.0.16_3d0h`. The OpenAPI document the server serves at
  `/openapi.json` (and in the Swagger UI) is regenerated from the current
  contract; it still carried the licence from before the EUPL move. The
  default-model gate now checks these contracts, and a test fails when the
  embedded document drifts from `api/openapi/vmafx-server-v1.yaml`. See
  [the REST page](docs/server/rest.md).
