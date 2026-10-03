// The outside-tester flow of docs/usage/tester-image.md, drawn from the code at the evidence
// anchors: the tester image or macOS bundle runs the GPU rows (run_gpu_section, run_twins), writes a
// report with its digest (report_digest), the report is sent back, scripts/ci/check-hardware-reports.py
// validates it (verdict_errors, gpu_errors), and scripts/docs/generate-hardware-reports.py renders
// the hardware-reports page.
import type { PraetorFigure } from '../../tools/figures/types.ts';

export default {
  title: 'Testing on hardware the project does not own',
  alt: 'A tester runs the image, sends the report, and CI checks it before it appears on the hardware-reports page.',
  evidence: [
    '.github/workflows/docker-publish-tester.yml:release',
    'tools/rc1-tester/src/vmaf_rc1_tester/hw_gpu.py:run_gpu_section',
    'tools/rc1-tester/src/vmaf_rc1_tester/hw_gpu.py:run_twins',
    'tools/rc1-tester/src/vmaf_rc1_tester/hw_report.py:report_digest',
    'scripts/ci/check-hardware-reports.py:verdict_errors',
    'scripts/ci/check-hardware-reports.py:gpu_errors',
    'scripts/docs/generate-hardware-reports.py:render',
  ],
  describe: ['The report carries a digest of its own content, so an edited report fails the check.'],
  props: {
    layout: {
      gap: 40,
      children: [
        {
          id: 'tester',
          label: "Tester's machine",
          direction: 'column',
          gap: 24,
          children: [
            { id: 'image', label: 'Tester image or macOS bundle', sub: 'published per release', width: 260 },
            { id: 'rows', label: 'GPU rows and twins', sub: 'every twin against the CPU', width: 260 },
            { id: 'report', label: 'Report JSON', sub: 'facts, verdicts, digest', shape: 'store', width: 260 },
          ],
        },
        {
          id: 'project',
          label: 'Project',
          direction: 'column',
          gap: 24,
          children: [
            { id: 'check', label: 'check-hardware-reports.py', sub: 'schema, verdicts, digest', width: 270 },
            { id: 'page', label: 'Hardware reports page', sub: 'generate-hardware-reports.py', width: 270 },
          ],
        },
      ],
    },
    edges: [
      { from: 'image', to: 'rows', label: 'run' },
      { from: 'rows', to: 'report', label: 'write' },
      { from: 'report', to: 'check', label: 'sent back' },
      { from: 'check', to: 'page', label: 'make docs-fragments-write' },
    ],
    steps: [
      {
        label: 'One report',
        caption: "A tester's run becomes a row on the hardware-reports page.",
        flow: [
          { edges: 'image->rows', say: 'The image probes the GPU and runs every row it can.' },
          { edges: 'rows->report', say: 'Each twin is compared with the CPU; the report records the verdicts.' },
          { edges: 'report->check', say: 'The tester sends the report; CI checks its schema and digest.' },
          { edges: 'check->page', say: 'The page lists the report with its backend and verdict.' },
        ],
      },
    ],
  },
} satisfies PraetorFigure;
