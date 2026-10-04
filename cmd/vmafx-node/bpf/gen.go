// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

// Package bpf contains the eBPF descriptor tracker for rclone FUSE mounts and
// its Go-side loader.
//
// rclone_bypass.bpf.c is compiled with clang through bpf2go into
// rclonebypass_bpfel.o and its Go binding, which are committed and embedded in
// the node binary; no BPF toolchain is needed to build the node. Only the
// little-endian object is generated (amd64, arm64 and the other bpf2go
// little-endian targets): the package does not build on a big-endian
// architecture, and the node refuses VMAFX_EBPF_BYPASS there. vmlinux.h is a minimal header with only the types the
// program uses (tracepoint contexts follow the stable tracepoint ABI).
//
// Regenerate after changing the C source (needs clang and libbpf headers;
// the committed objects were built with clang 23.1.1):
//
//	go generate ./cmd/vmafx-node/bpf/
//
// and commit the .c, the two generated files and any header change together.
// The second directive (add_spdx_header.sh) prepends the licence header the
// repository requires on every Go file (ADR-1250).
// TestEmbeddedObjectMatchesMirrors checks the generated object against the
// layouts the loader relies on.
//
// ADR-0779, ADR-1539.
package bpf

//go:generate go run github.com/cilium/ebpf/cmd/bpf2go@v0.22.0 -cc clang -cflags "-O2 -g -Wall -Wextra" -target bpfel -type event_t rcloneBypass rclone_bypass.bpf.c -- -I/usr/include/bpf -I.
//go:generate sh ./add_spdx_header.sh rclonebypass_bpfel.go
