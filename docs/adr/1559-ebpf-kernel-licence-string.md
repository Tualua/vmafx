<!-- markdownlint-disable MD013 MD060 -->
# ADR-1559: the node's eBPF program stays EUPL-1.2 and declares "GPL" to the kernel under EUPL-1.2's compatibility clause

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: go, node, ebpf, license, supply-chain, phase4b, fork-local

## Context

An eBPF object carries a licence string in its `license` section, and the
kernel reads it when the program is loaded. `cmd/vmafx-node/bpf/rclone_bypass.bpf.c`
(ADR-1539) is fork-authored, so its SPDX line is `EUPL-1.2`
([ADR-1250](1250-eupl-fork-relicense.md)), but its licence string said
`"Dual BSD/GPL"`: it told the kernel, and anyone reading the object, that the
program is available under a BSD licence or the GPL, neither of which the
project granted.

The string cannot simply become `"EUPL-1.2"`. The kernel lets a program call
some helpers only when its licence string is one of six GPL-compatible values
(`license_is_gpl_compatible()` in `include/linux/license.h`: `"GPL"`,
`"GPL v2"`, `"GPL and additional rights"`, `"Dual BSD/GPL"`, `"Dual MIT/GPL"`,
`"Dual MPL/GPL"`; torvalds/linux master `6addb4f385570ebc11c4eb499a4f1c149f313e84`,
fetched 2026-10-04). The program calls two such helpers,
`bpf_probe_read_user_str` and `bpf_probe_read_kernel` (`.gpl_only = true` in
`kernel/trace/bpf_trace.c` at the same commit). Built with `"EUPL-1.2"`, the
object is refused on kernel 7.2.8 with `load program: invalid argument: cannot
call GPL-restricted function from non-GPL compatible program`.

The official English text of EUPL-1.2 (Interoperable Europe / Joinup,
<https://interoperable-europe.ec.europa.eu/sites/default/files/custom-page/attachment/2020-03/EUPL-1.2%20EN.txt>,
fetched 2026-10-04, SHA-256
`6fc9e709ccbfe0d77fbffa2427a983282be2eb88e47b1cdb49f21a83b4d1e665`) says in
Article 5:

> Compatibility clause: If the Licensee Distributes or Communicates Derivative
> Works or copies thereof based upon both the Work and another work licensed under
> a Compatible Licence, this Distribution or Communication can be done under the
> terms of this Compatible Licence. For the sake of this clause, ‘Compatible
> Licence’ refers to the licences listed in the appendix attached to this Licence.
> Should the Licensee's obligations under the Compatible Licence conflict with
> his/her obligations under this Licence, the obligations of the Compatible
> Licence shall prevail.

and its Appendix begins:

> ‘Compatible Licences’ according to Article 5 EUPL are:
>
> - GNU General Public License (GPL) v. 2, v. 3

## Decision

The source stays EUPL-1.2 and the program's licence string becomes `"GPL"`.
Loaded into the kernel, the program runs as part of a GPL-2.0 work and calls
its GPL-only interfaces; the compatibility clause lets that combination be
distributed and communicated under the GPL, which is what the string declares.
The string names the licence of the combination the kernel builds, not a
relicensing of the source file, which keeps its `EUPL-1.2` SPDX line.

`TestEmbeddedObjectLicence` (`cmd/vmafx-node/bpf/preflight_test.go`) fails
when any program of the embedded object declares another string, and
`TestEmbeddedObjectLoadsIntoKernel` loads and attaches the object on a host
that passes `Preflight` (passed on kernel 7.2.8 in a privileged container).

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| `"GPL"`, source EUPL-1.2 (chosen) | One licence for the fork's own code; the string names a licence the compatibility clause allows for the combination; the kernel accepts it | Relies on the reading that the loaded program and the kernel form a combined work under the clause | Chosen by the maintainer (popup of 2026-10-04) |
| Keep `"Dual BSD/GPL"` | No change; loads | Declares a BSD grant the project never made | A false licence statement in a published object |
| `"EUPL-1.2"` | Matches the SPDX line | The kernel refuses the program (GPL-only helpers), measured above | The tracker would never load |
| Relicense `rclone_bypass.bpf.c` to `GPL-2.0-only` or `Dual BSD/GPL` | String and SPDX line agree without the clause | A second licence for fork-authored code, against ADR-1250's one-licence rule; a BSD grant is a new permission the maintainer did not choose | Not chosen by the maintainer |
| Drop the two GPL-only helpers | Any string would load | `bpf_probe_read_user_str` is how the program reads the `openat` path; there is no non-GPL helper for user memory | Removes the tracker's function |

## Consequences

- **Positive**: the object no longer claims a BSD grant; the licence the
  kernel sees is the one the compatibility clause provides; a test holds the
  string and a privileged test proves the object loads.
- **Negative**: anyone redistributing the compiled object combined with a
  kernel receives it under the GPL for that combination; the source remains
  EUPL-1.2.
- **Neutral / follow-ups**: the regenerated `rclonebypass_bpfel.o` differs
  only in the licence section (`go generate ./cmd/vmafx-node/bpf/` with clang
  23.1.1 reproduces it byte for byte). The node image's licence notices
  (ADR-1514) already ship the EUPL-1.2 text; no notice change.

## References

- [ADR-1250](1250-eupl-fork-relicense.md), [ADR-1539](1539-node-ebpf-tracker-wiring.md),
  [ADR-1514](1514-go-and-node-image-licensing.md).
- EUPL-1.2 official English text, Article 5 and Appendix (URL and SHA-256 above, fetched 2026-10-04).
- Linux `include/linux/license.h` and `kernel/trace/bpf_trace.c` at `6addb4f385570ebc11c4eb499a4f1c149f313e84` (fetched 2026-10-04).
- Popup answer of 2026-10-04, "eBPF program licence": "Keep EUPL-1.2, string \"GPL\"".
