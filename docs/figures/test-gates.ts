// The test and golden-gate flow, drawn from the code at the evidence anchors: the unit tests run
// through scripts/ci/run_meson_test.py, the Netflix golden gate (make test-netflix-golden) runs the
// Python assertions against the isolated gcc or clang build of scripts/ci/setup-golden-build.sh
// (ADR-1317), and the cross-backend parity gate compares every GPU cell with the CPU, with
// tolerance 0 for the twins in EXACT_TWINS and the derived bound of LIBM_TWINS.
import type { PraetorFigure } from '../../tools/figures/types.ts';

export default {
  title: 'Test gates: unit tests, the Netflix golden gate, cross-backend parity',
  alt: 'A change passes the unit tests, the Netflix golden gate on its isolated CPU build, and the parity gate against the CPU.',
  evidence: [
    'scripts/ci/run_meson_test.py:sanitize_process_environment',
    'Makefile:GOLDEN_BUILD_DIR',
    'scripts/ci/setup-golden-build.sh:GOLDEN_CC',
    'python/test/quality_runner_test.py:assertAlmostEqual',
    'scripts/ci/cross_backend_parity_gate.py:FEATURE_METRICS',
    'scripts/ci/cross_backend_calibration.py:EXACT_TWINS',
    'scripts/ci/cross_backend_calibration.py:LIBM_TWINS',
  ],
  describe: [
    'The golden assertions are Netflix ground truth and are never edited; a drifting score is fixed in the code.',
    'An exact twin is compared at --precision max with tolerance 0.',
  ],
  props: {
    layout: {
      gap: 40,
      children: [
        { id: 'change', label: 'Change', sub: 'source, tests, docs' },
        {
          id: 'gates',
          label: 'Gates',
          direction: 'column',
          gap: 22,
          children: [
            { id: 'unit', label: 'Unit tests', sub: 'run_meson_test.py -- -C build', width: 270 },
            { id: 'golden', label: 'Netflix golden gate', sub: 'make test-netflix-golden', width: 270 },
            { id: 'parity', label: 'Cross-backend parity gate', sub: 'every GPU cell against the CPU', width: 270 },
          ],
        },
        {
          direction: 'column',
          gap: 22,
          children: [
            { id: 'goldenbuild', label: 'Isolated golden build', sub: 'gcc or clang, no FP contraction', width: 250 },
            { id: 'assertions', label: 'Netflix assertions', sub: 'python/test/, never edited', shape: 'store', width: 250 },
            { id: 'twins', label: 'EXACT_TWINS, LIBM_TWINS', sub: 'tolerance 0 or a derived bound', shape: 'store', width: 250 },
          ],
        },
      ],
    },
    edges: [
      { from: 'change', to: 'unit' },
      { from: 'unit', to: 'golden' },
      { from: 'golden', to: 'parity' },
      { from: 'golden', to: 'goldenbuild', label: 'VMAF_BUILD_DIR' },
      { from: 'golden', to: 'assertions', label: 'assertAlmostEqual' },
      { from: 'parity', to: 'twins', label: 'tolerance' },
    ],
    steps: [
      {
        label: 'Gates in order',
        caption: 'Each gate must pass before the change lands.',
        flow: [
          { edges: 'change->unit', say: 'The unit tests run under the credential-safe Meson wrapper.' },
          { edges: 'unit->golden', say: 'The golden gate builds the CPU library on its own build directory.' },
          { edges: ['golden->goldenbuild', 'golden->assertions'], say: "Netflix's assertions run against that build." },
          { edges: 'golden->parity', say: 'The parity gate runs each GPU backend against the CPU.' },
          { edges: 'parity->twins', say: 'An exact twin must match to the last bit; a libm twin within its bound.' },
        ],
      },
    ],
  },
} satisfies PraetorFigure;
