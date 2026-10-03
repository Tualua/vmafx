<!-- markdownlint-disable MD013 -->
# Intel QSV for vmaf-tune

The `h264_qsv`, `hevc_qsv` and `av1_qsv` codec adapters of
[`vmaf-tune`](../../usage/vmaf-tune-codec-adapters.md) encode on Intel Quick
Sync Video. They need an FFmpeg built against Intel's oneVPL dispatcher
(`libvpl`) and a GPU generation that supports the codec. Scoring with `vmaf`
needs none of this.

## Install the oneVPL packages

| Platform | Command | Guide |
| --- | --- | --- |
| Ubuntu | `sudo apt-get install -y libvpl2 libvpl-dev` | [Ubuntu](ubuntu.md#intel-qsv-optional-for-vmaf-tune) |
| Fedora | `sudo dnf install -y libvpl libvpl-tools` | [Fedora](fedora.md#intel-qsv-optional-for-vmaf-tune) |
| Arch | `sudo pacman -S --needed libvpl vpl-gpu-rt` | [Arch](arch.md#intel-qsv-optional-for-vmaf-tune) |
| Windows | the runtime ships with Intel's graphics driver | [Windows](windows.md#intel-qsv-optional-for-vmaf-tune) |
| macOS | not supported: Intel ships oneVPL for Linux and Windows only | [macOS](macos.md#caveats) |

Package sources:
[Ubuntu `libvpl-dev`](https://packages.ubuntu.com/search?keywords=libvpl&searchon=names)
(noble: [2023.3.0](https://packages.ubuntu.com/noble/libvpl-dev)),
[Fedora `libvpl`](https://packages.fedoraproject.org/pkgs/oneVPL/) (Fedora 42,
43, Rawhide, EPEL 9 and 10),
[Arch `libvpl`](https://archlinux.org/packages/extra/x86_64/libvpl/) and
[Arch `vpl-gpu-rt`](https://archlinux.org/packages/extra/x86_64/vpl-gpu-rt/).

Intel [archived Media SDK (`libmfx`) in May
2023](https://github.com/Intel-Media-SDK/MediaSDK);
oneVPL is its successor. GPUs older than Tiger Lake need the legacy media
driver as well (`intel-media-va-driver` or `intel-media-va-driver-non-free` on
Ubuntu, `intel-mediasdk` on Fedora,
`intel-media-sdk` on Arch).

## FFmpeg requirement

FFmpeg supports oneVPL from n6.0 onward (`--enable-libvpl`; the
[FFmpeg Changelog](https://github.com/FFmpeg/FFmpeg/blob/master/Changelog) lists
"oneVPL support for QSV" under 6.0). Older FFmpeg builds use the legacy
`--enable-libmfx`.

| Source | oneVPL |
| --- | --- |
| Ubuntu 24.04 / 26.04 `ffmpeg` | built with `--enable-libvpl` |
| Ubuntu 22.04 `ffmpeg` (4.4) | legacy `libmfx` only: build FFmpeg yourself or use a backport |
| Fedora `ffmpeg` from RPM Fusion | built with `--enable-libvpl` |
| Arch `extra/ffmpeg` | built with `--enable-libvpl` |
| [BtbN FFmpeg builds](https://github.com/BtbN/FFmpeg-Builds) for Windows | built with `--enable-libvpl` |
| your own build | pass `--enable-libvpl` and install the oneVPL headers |

## Hardware capability matrix

| CPU / GPU generation | H.264 enc/dec | HEVC 8-bit enc/dec | HEVC 10-bit enc/dec | AV1 decode | AV1 encode |
| --- | --- | --- | --- | --- | --- |
| Skylake / Kaby Lake / Coffee Lake (Gen 9) | yes | yes | decode only | no | no |
| Ice Lake (Gen 11) | yes | yes | yes | no | no |
| Tiger Lake / Alder Lake / Raptor Lake (Xe LP) | yes | yes | yes | yes | no |
| Arc Alchemist (Xe HPG, A-series, 2022) | yes | yes | yes | yes | yes |
| Arc Battlemage (Xe2, B-series) | yes | yes | yes | yes | yes |

`av1_qsv` therefore needs Arc Alchemist or newer, and 10-bit `hevc_qsv` needs
Ice Lake or newer. Source:
[Intel Quick Sync Video, hardware decoding and encoding](https://en.wikipedia.org/wiki/Intel_Quick_Sync_Video#Hardware_decoding_and_encoding),
checked 2026-05-08.
