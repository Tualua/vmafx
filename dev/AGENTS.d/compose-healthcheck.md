---
paths:
  - dev/docker-compose.yml
  - dev/scripts/dev-mcp-healthcheck.sh
  - dev/scripts/test-dev-mcp-healthcheck.sh
invariant: Healthcheck matches stdio transport via vmaf --version; 45s start period for CUDA; no socket check.
---
<!-- markdownlint-disable MD013 -->
# Compose healthcheck invariant (ADR-0641)

`dev-mcp` service healthcheck must match entrypoint transport.
Entrypoint exposes MCP over stdio (`docker exec -i vmaf-dev-mcp
vmafx-mcp` — Go binary, ADR-1229), doesn't create
`/sockets/vmaf-mcp.sock` by default. Compose healthcheck must
therefore remain CLI check, not `test -S /sockets/vmaf-mcp.sock`.
`dev-mcp-healthcheck.sh` first runs `vmaf --version`; when
`/dev/nvidia0` exists it also requires `nvidia-smi` to answer driver query.
Keep Compose's 45-second start period for CUDA cold-start. hermetic
`test-dev-mcp-healthcheck.sh` and its pre-commit hook pin all three branches.
Reverting to socket check leaves container permanently `unhealthy` and
prevents `smoke-probe-cron` from starting even though stdio is usable.
