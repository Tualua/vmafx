- **The controller evicts silent nodes after startup and returns their
  running jobs to the queue.** The node registry's reaper stopped about 15 s
  after startup, because it was tied to the fx start context, which fx lets
  expire after its start timeout; dead nodes stayed registered for ever. And
  an evicted node's running jobs stayed `RUNNING` for ever, although
  `controller.proto` promised they would be re-queued. The reaper now runs
  until shutdown, and an eviction returns the node's running jobs to
  `PENDING` ahead of newer work. See [the controller guide](docs/server/controller.md#node-api).
