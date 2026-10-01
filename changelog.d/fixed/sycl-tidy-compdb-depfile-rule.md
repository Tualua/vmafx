- **The SYCL clang-tidy lane measures the SYCL sources again.** Since the
  SYCL build targets record their headers (PR #1764), the script that adds
  the SYCL compile commands to the lint database found 4 of 31 translation
  units: Ninja names a rule with a depfile differently and the script matched
  the old name only. `make tidy-ratchet LANE=sycl` and the changed-file SYCL
  lint job therefore saw none of the kernels. The script reads both rule
  forms, and it now stops with an error when a SYCL source is compiled by a
  command it cannot read, so the lane cannot go blind silently again.
