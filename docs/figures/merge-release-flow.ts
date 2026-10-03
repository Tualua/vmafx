// The merge and release flow, drawn from the code at the evidence anchors: the PR body gates
// (deliverables-check.sh, state-md-touch-check.sh), the local merge train's validation
// (merge_train_guard.py validate: make lint, make test, a validated-<head>.json receipt), release-please on
// master, the draft release an operator publishes, and the publication workflows that check out the
// published tag and sign their outputs (docs/development/release.md, "Automation flow").
import type { PraetorFigure } from '../../tools/figures/types.ts';

export default {
  title: 'From a pull request to a published release',
  alt: 'A PR passes its gates and the merge train, lands on master, and release-please and an operator turn master into a release.',
  evidence: [
    'scripts/ci/deliverables-check.sh:diff_base',
    'scripts/ci/state-md-touch-check.sh:trigger_reason',
    'scripts/dev/merge_train_guard.py:validate',
    'scripts/dev/merge_train_guard.py:RELEASE_PR',
    '.github/workflows/release-please.yml:googleapis',
    '.github/workflows/docker-publish-tester.yml:published_at',
    '.github/workflows/docker-publish-production.yml:release',
  ],
  describe: [
    'Release candidates follow the phases in AGENTS.md section 11: RC1 to RC8, then v1.0.0.',
    'Publishing the draft is the one irreversible step and is taken by a person.',
  ],
  props: {
    layout: {
      direction: 'column',
      gap: 34,
      children: [
        {
          id: 'land',
          label: 'Landing',
          direction: 'row',
          gap: 46,
          children: [
            { id: 'pr', label: 'Pull request', sub: 'Conventional Commit title' },
            { id: 'checks', label: 'PR gates', sub: 'deliverables, state.md, audit' },
            { id: 'train', label: 'Merge train', sub: 'make lint, make test' },
            { id: 'master', label: 'master', sub: 'squashed commit', shape: 'store' },
          ],
        },
        {
          id: 'release',
          label: 'Release',
          direction: 'row',
          gap: 46,
          children: [
            { id: 'rp', label: 'release-please', sub: 'release PR, version bump' },
            { id: 'draft', label: 'Draft release', sub: 'merged release PR' },
            { id: 'publish', label: 'Operator publishes', sub: 'creates the vX.Y.Z tag', shape: 'decision', width: 200 },
            { id: 'artifacts', label: 'Publication workflows', sub: 'build, sign, attest, publish' },
          ],
        },
      ],
    },
    edges: [
      { from: 'pr', to: 'checks' },
      { from: 'checks', to: 'train' },
      { from: 'train', to: 'master', label: 'fast-forward' },
      { from: 'master', to: 'rp', label: 'push' },
      { from: 'rp', to: 'draft', label: 'merge' },
      { from: 'draft', to: 'publish' },
      { from: 'publish', to: 'artifacts', label: 'release.published' },
    ],
    steps: [
      {
        label: 'Land',
        caption: 'A pull request becomes one commit on master.',
        flow: [
          { edges: 'pr->checks', say: 'The PR body carries the deliverables; the gates check it and the diff.' },
          { edges: 'checks->train', say: 'The merge train restacks the PR, builds and runs the tests.' },
          { edges: 'train->master', say: 'It lands as one squashed commit, fast-forward.' },
        ],
      },
      {
        label: 'Release',
        caption: 'master becomes a published release.',
        flow: [
          { edges: 'master->rp', say: 'release-please reads the commit titles and opens the release PR.' },
          { edges: 'rp->draft', say: 'Merging the release PR creates a draft release, no tag yet.' },
          { edges: 'draft->publish', say: 'An operator publishes the draft; that creates the tag.' },
          { edges: 'publish->artifacts', say: 'The workflows check out the tag, sign and publish.' },
        ],
      },
    ],
  },
} satisfies PraetorFigure;
