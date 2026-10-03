- The `vmaf-dev-mcp` container starts again on an image built from the
  current `dev/Containerfile`. Its entrypoint ran `chmod 1777 /tmp` as the
  unprivileged `vmaf` user; uutils coreutils 0.10 in the updated Ubuntu
  26.04 base issues that call even when the mode is already 1777, so it
  failed and the container restarted in a loop. The mode is now changed
  only when it is wrong and `/tmp` belongs to the entrypoint's user.
