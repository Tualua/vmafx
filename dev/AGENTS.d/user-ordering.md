---
paths:
  - dev/Containerfile
invariant: WORKDIR creates directories as root; precede non-root directory creations with explicit chown.
---
<!-- markdownlint-disable MD013 -->
# USER ordering and directory ownership (stage 3)

`WORKDIR` always creates directories as **root**, regardless of any
previous `USER` directive. After `COPY --chown=vmaf:vmaf . /dest/`,
only *contents* owned by `vmaf`; destination directory itself still
owned by root.

**Rule**: any `RUN` step executing as non-root user and needing to
create subdirectory inside `WORKDIR`-created path must be preceded
by:

```dockerfile
RUN chown <user>:<group> /parent /parent/dest
USER <user>
```

Do NOT rely on `COPY --chown` alone to make directory writable —
does not change directory entry's owner, only file/subdirectory
contents.

Violating this causes `meson setup build` (and any other tool
calling `os.makedirs`) to fail with
`PermissionError: [Errno 13] Permission denied` at exactly build-dir
creation step, exit code 13.
