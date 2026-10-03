<!-- markdownlint-disable MD013 MD060 -->
# ADR-1507: The bundled BRISQUE model is used and redistributed under the LIVE release notice as written, not as a research-only exception

- **Status**: Accepted; supersedes the statement of the model's licence terms in [ADR-1115](1115-brisque-nr-metric.md)
- **Date**: 2026-10-03
- **Deciders**: maintainer, agent
- **Tags**: license, compliance, model, metrics, docs, fork-local

## Context

ADR-1115 bundled the LIVE laboratory's trained BRISQUE model
(`model/other_models/brisque_live.model`, also built into libvmaf) and described
its terms as "released by the LIVE lab for research and education, conditioned on
citing the TIP 2012 paper", a "documented research-use redistribution exception".
`NOTICE-brisque`, the model card, a header comment, the metric page and the ADR
index repeated that description.

ADR-1503 put the release's own notice into the tree
(`LICENSES/LicenseRef-LIVE-BRISQUE.txt`, reproduced verbatim from the BRISQUE
release `readme.txt`) because the notice must travel with every copy. Read as
written, it grants more than the fork had claimed and asks for something the
fork had not done. Its grant is:

> Permission is hereby granted, without written agreement and without license or
> royalty fees, to use, copy, modify, and distribute this code (the source files)
> and its documentation for any purpose, provided that the copyright notice in its
> entirety appear in all copies of this code, and the original source of this code,
> Laboratory for Image and Video Engineering (LIVE, <http://live.ece.utexas.edu>) and
> Center for Perceptual Systems (CPS, <http://www.cps.utexas.edu>) at the University
> of Texas at Austin (UT Austin, <http://www.utexas.edu>), is acknowledged in any
> publication that reports research using this code.

So the use is not limited to research or education; the conditions are the
complete copyright notice in all copies and, in a publication that reports
research using the model, an acknowledgement of LIVE and CPS at UT Austin and the
two citations the notice names. The fork's own description invented a
restriction and replaced the actual conditions with a looser "cite the paper on
any use".

## Decision

The fork uses and redistributes the BRISQUE model under the LIVE release notice as
written, identified as `LicenseRef-LIVE-BRISQUE` (text in `LICENSES/`). Every
statement of the model's terms in the tree quotes or points to that text instead of
paraphrasing it: the use and redistribution are for any purpose; every copy
carries the copyright notice in its entirety (the tester packages ship it in their
licence directory, ADR-1503); a publication that reports research using the model
acknowledges LIVE and CPS at UT Austin and cites the two works the notice names,
the BRISQUE software release (2011) and the paper, published as Mittal, Moorthy
and Bovik, IEEE TIP 21(12), 2012. "Research-use exception" and "research and
education only" are withdrawn wherever the fork wrote them. ADR-1115's metric
decisions are unchanged.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Follow the verbatim LIVE text (chosen) | The fork states what the licensor granted and what it asks; one source of truth in `LICENSES/` | Every place that paraphrased the terms changes | The maintainer chose it (References) |
| Keep the research-use wording as a self-imposed restriction | No doc change | Tells users of libvmaf and the tester packages that a use is forbidden which the licensor permits, and still omits the acknowledgement condition | A licence statement has to match the licence |
| Remove the model and require `model_path` | No third-party model in the tree | The metric stops working out of the box; ADR-1115 rejected it | The terms are permissive enough to keep it |

## Consequences

- **Positive**: the model's terms are stated once, verbatim, and every summary
  points to them; downstream users are not told a restriction exists that does
  not.
- **Negative**: none in code; the documents that paraphrased the terms change in
  the same PR.
- **Neutral / follow-ups**: historical records keep their wording (ADR-1115's
  body, the 1.0.0-rc.1 changelog archive, the ADR-1115 entries of the rebase
  notes, research digest 1101 and the `T-METRIC-BRISQUE-NR-2026-06-14` ledger
  row); the digest gains a pointer to this ADR.

## References

- `Q` (popup 2026-10-03): "Follow the verbatim LIVE text (Recommended)".
- [ADR-1115](1115-brisque-nr-metric.md), [ADR-1503](1503-tester-artifact-licensing.md).
- `LICENSES/LicenseRef-LIVE-BRISQUE.txt`, reproduced from
  <https://github.com/gregfreeman/image_quality_toolbox/blob/5491aa55b9006242e7317dd5a82f3af9d5c92510/+brisque/readme.txt>
  (read 2026-10-03), the mirror of the BRISQUE release that trained the model.
