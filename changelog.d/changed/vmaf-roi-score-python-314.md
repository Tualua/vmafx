- **`vmaf-roi-score` installs on Python 3.13 and 3.14** (ADR-1528). Its
  metadata allowed only Python 3.10 to 3.12, although nothing in the tool or
  its optional runtime dependencies needs that limit. The package's tests now
  run in CI on the pinned Python 3.14 interpreter
  (`Python Package Tests (vmaf-roi-score)`).
