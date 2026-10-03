// The vmafx-operator of docs/development/operator.md, drawn from the code at the evidence anchors:
// one controller-runtime Manager (main.go) runs three reconcilers. VmafxJobReconciler polls the
// controller's GetJob RPC every jobPollInterval, VmafxNodeReconciler probes the controller's
// /healthz every nodeProbeInterval and marks a node stale after nodeStaleThreshold, and
// VmafxModelTrainingReconciler polls the trainer sidecar's /status every trainingRequeueInterval
// and emits a CheckpointWritten event.
import type { PraetorFigure } from '../../tools/figures/types.ts';

export default {
  title: 'vmafx-operator: one manager, three reconcilers',
  alt: 'The operator manager runs three reconcilers that poll the controller and the trainer and write status to their CRDs.',
  evidence: [
    'cmd/vmafx-operator/main.go:SetupWithManager',
    'cmd/vmafx-operator/internal/controller/vmafxjob_controller.go:VmafxJobReconciler',
    'cmd/vmafx-operator/internal/controller/vmafxjob_controller.go:jobPollInterval',
    'cmd/vmafx-operator/internal/controller/vmafxjob_controller.go:getRemoteJob',
    'cmd/vmafx-operator/internal/controller/vmafxnode_controller.go:VmafxNodeReconciler',
    'cmd/vmafx-operator/internal/controller/vmafxnode_controller.go:nodeProbeInterval',
    'cmd/vmafx-operator/internal/controller/vmafxnode_controller.go:nodeStaleThreshold',
    'cmd/vmafx-operator/internal/controller/vmafxmodeltraining_controller.go:VmafxModelTrainingReconciler',
    'cmd/vmafx-operator/internal/controller/vmafxmodeltraining_controller.go:pollTrainerStatus',
    'cmd/vmafx-operator/internal/controller/vmafxmodeltraining_controller.go:CheckpointWritten',
  ],
  describe: [
    'Each reconciler requeues itself: jobs every 10 s, nodes every 30 s, trainings every 60 s.',
    'The manager serves Prometheus metrics on :8080 and health probes on :8081.',
  ],
  props: {
    layout: {
      gap: 56,
      children: [
        {
          id: 'manager',
          label: 'vmafx-operator pod: controller-runtime Manager',
          direction: 'column',
          gap: 24,
          children: [
            { id: 'job', label: 'VmafxJobReconciler', sub: 'every 10 s: GetJob, map status to phase', width: 300 },
            { id: 'node', label: 'VmafxNodeReconciler', sub: 'every 30 s: /healthz; stale after 60 s', width: 300 },
            { id: 'training', label: 'VmafxModelTrainingReconciler', sub: 'every 60 s: trainer /status', width: 300 },
          ],
        },
        {
          direction: 'column',
          gap: 40,
          children: [
            { id: 'controller', label: 'vmafx-controller', sub: 'gRPC GetJob, HTTP /healthz' },
            { id: 'trainer', label: 'Trainer sidecar', sub: 'HTTP /status on :9091' },
            { id: 'crds', label: 'CRD status', sub: 'VmafxJob, VmafxNode, VmafxModelTraining', shape: 'store', width: 260 },
          ],
        },
      ],
    },
    edges: [
      { from: 'job', to: 'controller', label: 'GetJob' },
      { from: 'node', to: 'controller', label: '/healthz' },
      { from: 'training', to: 'trainer', label: '/status' },
      { from: 'manager', to: 'crds', label: 'status, events', around: 'below' },
    ],
    steps: [
      {
        label: 'Job',
        caption: 'A VmafxJob follows the controller job it submitted.',
        flow: [
          { edges: 'job->controller', say: 'Every 10 s the reconciler asks the controller for the job.' },
          { edges: { edge: 'job->controller', back: true, data: 'COMPLETED' }, say: 'PENDING, RUNNING, COMPLETED or FAILED comes back.' },
          { edges: 'manager->crds', say: 'The phase, and the score on success, go into the CR status.' },
        ],
      },
      {
        label: 'Node',
        caption: 'A VmafxNode turns unhealthy when the probe fails or its heartbeat is old.',
        flow: [
          { edges: 'node->controller', say: 'Every 30 s the reconciler probes the controller.' },
          { edges: 'manager->crds', say: 'A heartbeat older than 60 s marks the node unhealthy.' },
        ],
      },
      {
        label: 'Training',
        caption: 'A VmafxModelTraining reports the trainer sidecar.',
        flow: [
          { edges: 'training->trainer', say: 'Every 60 s the reconciler reads the trainer /status.' },
          { edges: 'manager->crds', say: 'A new checkpoint emits a CheckpointWritten event.' },
        ],
      },
    ],
  },
} satisfies PraetorFigure;
