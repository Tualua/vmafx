// The controller's node API and job lifecycle of docs/server/controller.md, drawn from the code at
// the evidence anchors: RegisterNode, Heartbeat, PullWork and ReportResult in grpc_server.go, the
// node registry that evicts a silent node (reaper), the scheduler's Assign, and the five status
// strings of the SQLite queue.
import type { PraetorFigure } from '../../tools/figures/types.ts';

export default {
  title: 'Controller: node API and job lifecycle',
  alt: 'Nodes register, send heartbeats, pull matching pending jobs and report results; a job ends completed, failed or cancelled.',
  evidence: [
    'cmd/vmafx-controller/grpc_server.go:SubmitJob',
    'cmd/vmafx-controller/grpc_server.go:RegisterNode',
    'cmd/vmafx-controller/grpc_server.go:Heartbeat',
    'cmd/vmafx-controller/grpc_server.go:PullWork',
    'cmd/vmafx-controller/grpc_server.go:ReportResult',
    'cmd/vmafx-controller/grpc_server.go:CancelJob',
    'cmd/vmafx-controller/nodes/registry.go:reaper',
    'cmd/vmafx-controller/scheduler/scheduler.go:Assign',
    'cmd/vmafx-controller/queue/queue.go:StatusPending',
    'cmd/vmafx-controller/queue/queue.go:StatusCancelled',
  ],
  describe: [
    'A node that misses heartbeats for 60 s is evicted, and its running jobs return to pending.',
    'A running job that is cancelled is stopped by its node after the next heartbeat names it.',
    'PullWork assigns the oldest pending job whose backend the node lists in its capability.',
  ],
  props: {
    layout: {
      direction: 'column',
      gap: 44,
      children: [
        {
          direction: 'row',
          gap: 44,
          children: [
            { id: 'client', label: 'Client', sub: 'SubmitJob, CancelJob' },
            { id: 'controller', label: 'vmafx-controller', sub: 'queue, node registry, scheduler', width: 250 },
            { id: 'node', label: 'vmafx-node', sub: 'RegisterNode, Heartbeat' },
          ],
        },
        {
          id: 'states',
          label: 'Job status in the queue',
          direction: 'row',
          gap: 26,
          children: [
            { id: 'pending', label: 'pending' },
            { id: 'running', label: 'running' },
            {
              direction: 'column',
              gap: 14,
              children: [
                { id: 'completed', label: 'completed' },
                { id: 'failed', label: 'failed' },
                { id: 'cancelled', label: 'cancelled' },
              ],
            },
          ],
        },
      ],
    },
    edges: [
      { from: 'client', to: 'controller', label: 'SubmitJob' },
      { from: 'node', to: 'controller', label: 'PullWork' },
      { id: 'report', from: 'node', to: 'controller', label: 'ReportResult', around: 'above' },
      { from: 'pending', to: 'running', label: 'PullWork' },
      { from: 'running', to: 'completed', label: 'ok' },
      { from: 'running', to: 'failed', label: 'error' },
      { from: 'pending', to: 'cancelled', label: 'CancelJob', around: 'below', quiet: true },
      { from: 'running', to: 'cancelled', label: 'CancelJob', quiet: true },
      { id: 'evict', from: 'running', to: 'pending', label: 'node evicted', around: 'above', quiet: true },
    ],
    steps: [
      {
        label: 'Job runs',
        caption: 'A submitted job is pulled by a matching node and reported.',
        flow: [
          { edges: 'client->controller', say: 'The client submits a job; it is pending.', light: ['pending'] },
          { edges: 'node->controller', say: 'A node with the required backend pulls work.' },
          { edges: 'pending->running', say: 'The scheduler assigns the oldest matching job.' },
          { edges: 'report', say: 'The node reports the result.' },
          { edges: 'running->completed', say: 'The job is completed, or failed on an error.' },
        ],
      },
      {
        label: 'Node lost',
        caption: 'An evicted node gives its jobs back.',
        flow: [
          { edges: 'pending->running', say: 'A node runs a job.' },
          { edges: 'evict', say: 'No heartbeat for 60 s: the node is evicted and the job is pending again.' },
        ],
      },
    ],
  },
} satisfies PraetorFigure;
