- **The Windows ARM64 MSVC build links again.** A unit test added on
  2026-10-01 called `pthread_self()` and `pthread_equal()`, which the Windows
  thread shim does not define, so that build failed at link time on every
  commit since. The test now identifies the calling thread portably, and a
  contract test rejects pthread calls the shim lacks.
