---
paths:
  - pyproject.toml
  - renovate.json
invariant: Root pyproject.toml keeps requires-python >=3.14 without patch component; renovate excludes root requires-python.
---
# Root Python requirement stays minor-series scoped

Root [`pyproject.toml`](../../pyproject.toml) is tool-only metadata, keeps
`requires-python = ">=3.14"`, without patch component. Dependabot updater
images can lag newest CPython patch, so patch-specific floor makes
automatic pip dependency graph fail before it can inspect any package.
Exact-root `pep621` rule in [`renovate.json`](../../renovate.json) disables updates
for that one `requires-python` entry. Keep workflow `setup-python` pins and real
package constraints independently managed; never broaden exclusion to
subdirectory `pyproject.toml` files.
