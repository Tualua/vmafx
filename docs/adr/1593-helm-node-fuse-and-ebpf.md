<!-- markdownlint-disable MD013 MD060 -->
# ADR-1593: the node image carries FUSE mount tools, and the Helm chart grants FUSE and the eBPF tracker per value

- **Status**: Accepted
- **Date**: 2026-10-04
- **Deciders**: maintainer, agent
- **Tags**: helm, node, docker, storage, ebpf, licensing, security, phase4b, fork-local

## Context

`storage.mode: mount` ([ADR-1526](1526-node-storage-streamed-inputs.md))
runs `rclone mount`, which mounts through the setuid helper `fusermount3`.
The published node image is distroless and had no `fusermount3`, so a node
in mount mode refused to start ("neither fusermount3 nor fusermount is on
PATH"), and `docs/server/node.md` told users to use `http-serve` there. The
chart could not give a pod FUSE either: `storage.mode: mount` rendered a node
that stopped at startup. The eBPF descriptor tracker
([ADR-1539](1539-node-ebpf-tracker-wiring.md)) watches those mounts and had
no chart value at all. The follow-up list of 2026-10-04 asks for a Helm
value for the tracker and its capabilities, and for the FUSE helper in the
node image under the licence gate ([ADR-1513](1513-production-artifact-licensing.md),
[ADR-1514](1514-go-and-node-image-licensing.md)).

Measured in Docker on kernel 7.2.9 with the node image's distroless base,
rclone v1.75.1, Debian 13 `fuse3` 3.17.2 and `mount` 2.41.5, with
`pkg/storage`'s real mount path (`TestFUSEMountStorage_RealRclone` and a probe
with the node's defaults):

| Image content and container settings | Mount |
|---|---|
| no `fusermount3` | refused: not on PATH |
| setuid `fusermount3` only, UID 65532 or 0, `SYS_ADMIN` | fails: `fusermount3: failed to execute /bin/mount` |
| `fusermount3` + util-linux `mount`/`umount`, UID 65532, `SYS_ADMIN` | fails: `mount failed: Permission denied` |
| the same with `--security-opt no-new-privileges` or without `SYS_ADMIN` | fails |
| `fusermount3` + `mount`/`umount`, UID 65532, `SYS_ADMIN` + `DAC_READ_SEARCH` | works: reads back byte for byte, unmounts, removes the mount point |
| `fusermount3` + `mount`/`umount`, UID 0, `SYS_ADMIN` | works |

libfuse 3.17.2's `mtab_needs_update()` (`lib/mount_util.c`) only skips the
`/bin/mount` call when `/etc/mtab` is missing or on a read-only file system;
Docker creates `/etc/mtab -> /proc/mounts` in every container, so the helper
needs util-linux `mount` and `umount`. Without `DAC_READ_SEARCH` the
setuid-root helper, holding only `SYS_ADMIN`, cannot reach the node's
per-job mount points (mode 0700, owned by the node's UID).

For the tracker, the embedded BPF object loads and attaches in a
non-privileged container as UID 0 with only `BPF` and `PERFMON` and the host's
`/sys/kernel/tracing` bound read-only, under Docker's default seccomp profile;
kernel BTF is visible through the container's own `/sys`. As UID 65532 the
same container has no effective capability and the preflight refuses.

End to end, a controller container and a node container from the built
`node-cpu` image (which passed `node-licence-check`) scored a job whose two
inputs were an rclone remote (`:local:/media/...`) read through per-job FUSE
mounts: as UID 65532 with `node.fuse`'s settings, and as UID 0 with
`node.ebpf`'s, where the tracker ran and held the two descriptors the scorer
opened under the mount prefix. Both returned the CLI's score (3.572957).

## Decision

- **Node image.** A `fuse-tools` stage installs Debian's `fuse3` and `mount`
  and copies `fusermount3` (setuid root), `mount` and `umount` (setuid bit
  removed: only `fusermount3` runs them, already as root) and their library
  closure into `runtime-base`, at their Debian paths, because `fusermount3`
  runs them with an empty environment. `record-copied-debian-libs.sh` records
  every copied file with its package and copyright in
  `/usr/local/share/vmafx/fuse-tools/`; the script gains an optional
  per-line destination directory for this. `licensing.json` lists the record
  as the `fuse-tools` `dpkg-copied` component of `production-node-image`, so
  the licence check claims the files and the `-source` image fetches the
  Debian sources of `fuse3`, `util-linux`, `libselinux` and `pcre2`. The
  publish workflow's node smoke test mounts an rclone remote as UID 65532 with
  only the capabilities below, reads the file back and unmounts.
- **`node.fuse`.** `enabled`, `resourceName` (required: the extended
  resource of a FUSE device plugin) and `appArmorProfile`. The container keeps
  UID 65532 and its read-only root file system; its capability bounding set
  becomes `SYS_ADMIN` and `DAC_READ_SEARCH` and `allowPrivilegeEscalation`
  becomes `true`. The node process has no effective capability; only the
  setuid helper uses them. A `storage.mountRoot` outside `/tmp` gets an
  `emptyDir`.
- **`node.ebpf`.** `enabled` and `mountPrefix`. It sets `VMAFX_EBPF_BYPASS=1`
  and `VMAFX_EBPF_MOUNT_PREFIX`, defaults `VMAFX_STORAGE_MOUNT_ROOT` to the
  prefix, runs the container as UID 0 with capabilities `BPF`, `PERFMON` and
  `SYS_ADMIN`, and mounts the host's `/sys/kernel/tracing` read-only.
- **Refusals** (`templates/node-validate.yaml`): `storage.mode: mount`
  without `node.fuse`; `node.fuse` without `resourceName`; `node.ebpf`
  without `storage.mode: mount` and `node.fuse`, with a relative prefix or
  one over 255 bytes, or with a mount root outside the prefix;
  `env.VMAFX_EBPF_*`, and with `node.fuse` `env.VMAFX_STORAGE_MODE` /
  `VMAFX_STORAGE_MOUNT_ROOT`; `node.fuse` / `node.ebpf` without
  `node.enabled`.

## Alternatives considered

| Option | Pros | Cons | Why not chosen |
|---|---|---|---|
| Debian `fusermount3` + util-linux `mount`/`umount`, non-root with `SYS_ADMIN` + `DAC_READ_SEARCH` in the bounding set (chosen) | Unmodified distribution binaries; the node process keeps no effective capability; works with Docker's `/etc/mtab` | Five more copied packages in the licence record; two capabilities for the helper | Measured working; least privilege for the node process |
| `fusermount3` alone | Smallest change | Fails whenever `/etc/mtab` exists (Docker always creates it) | Does not work |
| Build `fusermount3` from libfuse with `IGNORE_MTAB` | No `mount` binary | Drops the helper's mount-ownership check on unmount; a source build with its own corresponding-source handling | Weakens a setuid binary to save two files |
| A small `/bin/mount` stand-in that exits 0 | Tiny | A fake system tool in the image; hides a real failure if `/etc/mtab` were a regular file | Not honest about what runs |
| BusyBox as `/bin/mount` | One static binary | Puts a full shell into the distroless image (any `argv[0]`); refuses non-root callers without `CAP_SETUID` | Undoes the no-shell posture |
| Run every FUSE node as root | No `DAC_READ_SEARCH` | Root for a need only the helper has | The tracker needs root; plain mount mode does not |
| `privileged: true` instead of a device plugin | No plugin to install | Every capability and every device | The chart never renders a privileged node |
| eBPF tracker as non-root with file capabilities on `vmafx-node` | No root | `allowPrivilegeEscalation` must stay on for file caps and the image would carry `cap_bpf` on the main binary | Root with three capabilities is smaller and measured |

## Consequences

- **Positive**: mount mode works in the published image and in Kubernetes
  through one value; the tracker can be enabled from the chart; every copied
  file is in the licence record and its Debian source in the `-source` image.
- **Negative**: pods with `node.fuse` or `node.ebpf` leave the Pod Security
  `baseline` and `restricted` profiles; their namespace must allow
  `privileged`. A cluster needs a FUSE device plugin. On AppArmor hosts the
  runtime's default profile denies mount (`deny mount,` in containerd's
  template) and `appArmorProfile` must be set; this was not reproduced here
  (the test host has AppArmor disabled).
- **Breaking**: `storage.mode: mount` without `node.fuse` no longer renders.
  It rendered a node that could not start with the published image.
- **Neutral / follow-ups**: a node pod with `node.fuse` or the tracker has
  not been run on a Kubernetes cluster; the Docker runs above stand in for it.

## References

- [ADR-1526](1526-node-storage-streamed-inputs.md), [ADR-1539](1539-node-ebpf-tracker-wiring.md),
  [ADR-1513](1513-production-artifact-licensing.md), [ADR-1514](1514-go-and-node-image-licensing.md),
  [ADR-0930](0930-helm-networkpolicy-pss.md).
- libfuse `fuse-3.17.2`: `util/fusermount.c` and `lib/mount_util.c`
  (`mtab_needs_update()`, the `/bin/mount` call in `add_mount()`), fetched
  2026-10-04 from <https://raw.githubusercontent.com/libfuse/libfuse/fuse-3.17.2/>.
- containerd `contrib/apparmor/template.go` at main `81af89b1d4b1`
  (`deny mount,`); squat/generic-device-plugin README at main `2cc50b05d4d2`
  (resources named `devic.es/<name>` by default, `/dev/fuse` example); both
  fetched 2026-10-04.
- Follow-up list of 2026-10-04, Lane PLAT item 7: "a Helm value for the eBPF tracker (`VMAFX_EBPF_BYPASS` and the capabilities it needs), and the FUSE helper in the node image so `mount` mode works there (licence gate covers it)".
