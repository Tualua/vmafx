// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/ebpf_other.go — the eBPF tracker where the embedded BPF
// object cannot run (not Linux, or a big-endian architecture): requesting it
// stops the node, it is never silently skipped (ADR-1539).

//go:build !(linux && (386 || amd64 || arm || arm64 || loong64 || mips64le || mipsle || ppc64le || riscv64))

package main

import (
	"errors"

	"github.com/golusoris/golusoris/core/config"
)

// ebpfBypass never exists off Linux.
type ebpfBypass struct{}

// provideEBPFBypass refuses VMAFX_EBPF_BYPASS off Linux.
func provideEBPFBypass(cfg *config.Config) (*ebpfBypass, error) {
	ec, err := loadEBPFConfig(cfg)
	if err != nil {
		return nil, err
	}
	if ec.Enabled {
		return nil, errors.New("VMAFX_EBPF_BYPASS needs little-endian Linux; this build cannot run the eBPF tracker")
	}
	return nil, nil
}
