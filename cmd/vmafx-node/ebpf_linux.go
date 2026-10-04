// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/ebpf_linux.go — the eBPF descriptor tracker in the node's fx
// graph (ADR-0779, ADR-1539), on little-endian Linux (the architectures the
// embedded BPF object is built for). Off by default. When VMAFX_EBPF_BYPASS is set,
// the tracker starts in OnStart, after the storage layer and before the
// controller client pulls jobs, and stops in OnStop after the client drained.
// A host that cannot run it (kernel, BTF, capabilities, a prefix the BPF
// program cannot match) stops the node at startup with the reason: fail
// closed, never a node that claims a tracker it does not run.

//go:build linux && (386 || amd64 || arm || arm64 || loong64 || mips64le || mipsle || ppc64le || riscv64)

package main

import (
	"context"
	"fmt"
	"log/slog"

	"go.uber.org/fx"

	"github.com/golusoris/golusoris/core/config"

	"github.com/VMAFx/vmafx/cmd/vmafx-node/bpf"
)

// ebpfBypass is the running tracker; nil when it is off. runCtx bounds the
// loader's ring-buffer drain for the tracker's whole life (the fx start
// context ends right after startup), and stop cancels it.
type ebpfBypass struct {
	loader *bpf.Loader
	runCtx context.Context
	cancel context.CancelFunc
}

func newEBPFBypass(prefix string, log *slog.Logger) *ebpfBypass {
	b := &ebpfBypass{loader: bpf.New(prefix, log)}
	b.runCtx, b.cancel = context.WithCancel(context.Background())
	return b
}

// start loads and attaches the program; a failure cancels the run context.
func (b *ebpfBypass) start() error {
	if err := b.loader.Start(b.runCtx); err != nil {
		b.cancel()
		return fmt.Errorf("VMAFX_EBPF_BYPASS is set but the eBPF tracker cannot start: %w", err)
	}
	return nil
}

// stop detaches the program and ends the drain.
func (b *ebpfBypass) stop() {
	b.cancel()
	b.loader.Stop()
}

// provideEBPFBypass builds the tracker when VMAFX_EBPF_BYPASS is set.
func provideEBPFBypass(lc fx.Lifecycle, cfg *config.Config, exec *Executor, log *slog.Logger) (*ebpfBypass, error) {
	ec, err := loadEBPFConfig(cfg)
	if err != nil {
		return nil, err
	}
	if !ec.Enabled {
		return nil, nil
	}
	if err := ec.checkMountAgreement(exec.store, cfg.Get("storage.mount_root")); err != nil {
		return nil, err
	}
	b := newEBPFBypass(ec.MountPrefix, log)
	lc.Append(fx.Hook{
		OnStart: func(_ context.Context) error {
			if err := b.start(); err != nil {
				return err
			}
			log.Info("ebpf descriptor tracker running", "mount_prefix", b.loader.MountPrefix())
			return nil
		},
		OnStop: func(_ context.Context) error {
			log.Info("stopping ebpf descriptor tracker", "tracked_fds", b.loader.TrackedFDs())
			b.stop()
			return nil
		},
	})
	return b, nil
}
