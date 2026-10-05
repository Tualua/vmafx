- **The known-upstream-bugs page lists the upstream GPU defects checked on
  2026-10-05.** The CUDA motion kernel that advanced a 16-bit pointer by the
  byte stride (upstream #1566, fixed upstream by #1552) does not affect the
  fork. The three integer ADM defects of upstream #1564 were fixed in the fork
  earlier. Each row names the fork's code and the test that holds it. See
  [known upstream bugs](docs/development/known-upstream-bugs.md#upstream-gpu-defects-checked-against-the-fork-2026-10-05).
