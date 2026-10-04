// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/ebpf_test.go — the eBPF tracker's configuration and its
// fail-closed wiring: a tracker that would watch nothing, or that the host
// cannot run, stops the node with the reason.

//go:build cgo && linux && (386 || amd64 || arm || arm64 || loong64 || mips64le || mipsle || ppc64le || riscv64)

package main

import (
	"context"
	"errors"
	"log/slog"
	"path/filepath"
	"strings"
	"testing"

	"go.uber.org/fx"

	"github.com/VMAFx/vmafx/cmd/vmafx-node/bpf"
	"github.com/VMAFx/vmafx/pkg/storage"
)

// TestLoadEBPFConfig covers off, on, prefix normalisation and a malformed
// flag (negative).
func TestLoadEBPFConfig(t *testing.T) {
	t.Parallel()
	for raw, want := range map[string]bool{"": false, "0": false, "false": false, "1": true, "true": true} {
		got, err := loadEBPFConfig(mapConfig{"ebpf.bypass": raw})
		if err != nil || got.Enabled != want || got.MountPrefix != defaultEBPFMountPrefix {
			t.Errorf("ebpf.bypass=%q: %+v, %v", raw, got, err)
		}
	}
	got, err := loadEBPFConfig(mapConfig{"ebpf.bypass": "1", "ebpf.mount_prefix": "/mnt/vmafx"})
	if err != nil || got.MountPrefix != "/mnt/vmafx/" {
		t.Errorf("prefix normalisation: %+v, %v", got, err)
	}
	if _, err := loadEBPFConfig(mapConfig{"ebpf.bypass": "maybe"}); err == nil {
		t.Error("ebpf.bypass=maybe accepted")
	}
}

// TestEBPFMountAgreement: the tracker needs a mounting storage layer whose
// root lies under the prefix (positive, two negatives, empty-root boundary).
func TestEBPFMountAgreement(t *testing.T) {
	t.Parallel()
	ec := ebpfConfig{Enabled: true, MountPrefix: "/rclone-mount/"}
	mount := storage.New(storage.Config{Mode: storage.ModeMount, Log: slog.New(slog.DiscardHandler)})
	serve := storage.New(storage.Config{Mode: storage.ModeHTTPServe, Log: slog.New(slog.DiscardHandler)})
	if err := ec.checkMountAgreement(mount, "/rclone-mount/jobs"); err != nil {
		t.Errorf("root under prefix refused: %v", err)
	}
	if err := ec.checkMountAgreement(serve, "/rclone-mount"); err == nil || !strings.Contains(err.Error(), "needs the storage layer to mount") {
		t.Errorf("http-serve accepted: %v", err)
	}
	if err := ec.checkMountAgreement(mount, "/tmp/elsewhere"); err == nil || !strings.Contains(err.Error(), "does not contain the mount root") {
		t.Errorf("root outside prefix accepted: %v", err)
	}
	if err := ec.checkMountAgreement(mount, ""); err == nil {
		t.Error("empty root (the temp dir) accepted under /rclone-mount/")
	}
}

// TestEBPFRefusedWithHTTPServe: the graph refuses a tracker over http-serve
// storage before any BPF call (negative).
func TestEBPFRefusedWithHTTPServe(t *testing.T) {
	writeNodeEnv(t)
	t.Setenv("VMAFX_EBPF_BYPASS", "1")
	t.Setenv("VMAFX_STORAGE_MODE", "http-serve")
	err := fx.New(productionGraph(), fx.NopLogger).Err()
	if err == nil || !strings.Contains(err.Error(), "needs the storage layer to mount") {
		t.Fatalf("graph error = %v, want the storage refusal", err)
	}
}

// TestEBPFStartFailsClosed: with storage mounting under the prefix and a host
// check that fails, the node does not start, and the error says why. Needs
// FUSE for the mount mode (a declared test dependency).
func TestEBPFStartFailsClosed(t *testing.T) {
	writeNodeEnv(t)
	root := t.TempDir()
	t.Setenv("VMAFX_EBPF_BYPASS", "1")
	t.Setenv("VMAFX_EBPF_MOUNT_PREFIX", filepath.Dir(root))
	t.Setenv("VMAFX_STORAGE_MODE", "mount")
	t.Setenv("VMAFX_STORAGE_MOUNT_ROOT", root)
	restore := bpf.SetPreflightForTest(func() error {
		return errors.New("ebpf: process lacks CAP_BPF and CAP_PERFMON (or CAP_SYS_ADMIN)")
	})
	t.Cleanup(restore)
	app := fx.New(productionGraph(), fx.NopLogger)
	if err := app.Err(); err != nil {
		t.Fatalf("graph construction: %v", err)
	}
	err := app.Start(context.Background())
	if err == nil {
		_ = app.Stop(context.Background())
		t.Fatal("node started although the eBPF tracker could not")
	}
	if !strings.Contains(err.Error(), "eBPF tracker cannot start") || !strings.Contains(err.Error(), "CAP_BPF") {
		t.Fatalf("start error = %v, want the tracker refusal naming the capability", err)
	}
}
