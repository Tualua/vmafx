// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/env_test.go — VMAFX_ env-contract regression guards.
//
// golusoris' env transform splits EVERY underscore on the delimiter, so the
// grpc.Module's underscore-bearing leaf keys (grpc.cert_file, grpc.key_file,
// grpc.max_recv_size, grpc.max_send_size) only bind if nodeEnvOptions declares
// them as CompoundKeys. Without that, VMAFX_GRPC_MAX_RECV_SIZE silently maps to
// grpc.max.recv.size and the operator override is a no-op. These tests pin both
// the declared contract and the end-to-end env→config binding.

//go:build cgo

package main

import (
	"os"
	"path/filepath"
	"testing"
	"time"

	"github.com/golusoris/golusoris/core/config"
	grpcmod "github.com/golusoris/golusoris/grpc"
)

// TestNodeEnvOptionsContract pins the VMAFX_ prefix, delimiter, and the exact
// CompoundKey set (the four underscore-bearing grpc leaves).
func TestNodeEnvOptionsContract(t *testing.T) {
	t.Parallel()
	opts := nodeEnvOptions(true)
	if opts.EnvPrefix != "VMAFX_" {
		t.Errorf("EnvPrefix = %q, want VMAFX_", opts.EnvPrefix)
	}
	if opts.Delimiter != "." {
		t.Errorf("Delimiter = %q, want .", opts.Delimiter)
	}
	if !opts.Watch {
		t.Error("nodeEnvOptions(true).Watch = false, want true")
	}
	if nodeEnvOptions(false).Watch {
		t.Error("nodeEnvOptions(false).Watch = true, want false")
	}
	want := map[string]bool{
		"grpc.cert_file":     true,
		"grpc.key_file":      true,
		"grpc.max_recv_size": true,
		"grpc.max_send_size": true,
		// Controller client keys (controller_config.go).
		"controller.ca_file":            true,
		"controller.server_name":        true,
		"controller.token_file":         true,
		"controller.rpc_timeout":        true,
		"controller.heartbeat_interval": true,
		"controller.poll_interval":      true,
	}
	got := make(map[string]bool, len(opts.CompoundKeys))
	for _, k := range opts.CompoundKeys {
		got[k] = true
	}
	for k := range want {
		if !got[k] {
			t.Errorf("missing CompoundKey %q (its VMAFX_GRPC_* env var would not bind)", k)
		}
	}
	for k := range got {
		if !want[k] {
			t.Errorf("unexpected CompoundKey %q", k)
		}
	}
}

// TestNodeEnvOptionsBindGrpcKeys is the end-to-end regression guard: the
// VMAFX_GRPC_* env vars must resolve onto the dotted grpc.* leaf keys through a
// real config.Config built with nodeEnvOptions().
func TestNodeEnvOptionsBindGrpcKeys(t *testing.T) {
	t.Setenv("VMAFX_GRPC_MAX_RECV_SIZE", "8388608")
	t.Setenv("VMAFX_GRPC_KEY_FILE", "/etc/tls/tls.key")

	cfg, err := config.New(nodeEnvOptions(false))
	if err != nil {
		t.Fatalf("config.New: %v", err)
	}
	if got := cfg.Int("grpc.max_recv_size"); got != 8388608 {
		t.Errorf("grpc.max_recv_size = %d, want 8388608 (compound key not binding)", got)
	}
	if got := cfg.String("grpc.key_file"); got != "/etc/tls/tls.key" {
		t.Errorf("grpc.key_file = %q, want /etc/tls/tls.key", got)
	}
}

// TestNodeEnvOptionsBindControllerKeys: the VMAFX_CONTROLLER_* and VMAFX_NODE_*
// env vars reach the keys loadControllerConfig reads.
func TestNodeEnvOptionsBindControllerKeys(t *testing.T) {
	env := map[string]string{
		"VMAFX_CONTROLLER_ADDR":               "ctrl:9090",
		"VMAFX_CONTROLLER_TLS":                "true",
		"VMAFX_CONTROLLER_CA_FILE":            "/etc/vmafx/ca.pem",
		"VMAFX_CONTROLLER_SERVER_NAME":        "ctrl.example",
		"VMAFX_CONTROLLER_TOKEN_FILE":         "/var/run/token",
		"VMAFX_CONTROLLER_RPC_TIMEOUT":        "3s",
		"VMAFX_CONTROLLER_HEARTBEAT_INTERVAL": "4s",
		"VMAFX_CONTROLLER_POLL_INTERVAL":      "5s",
		"VMAFX_NODE_ID":                       "pod-7",
		"VMAFX_NODE_SLOTS":                    "2",
	}
	for k, v := range env {
		t.Setenv(k, v)
	}
	raw, err := config.New(nodeEnvOptions(false))
	if err != nil {
		t.Fatalf("config.New: %v", err)
	}
	got, err := loadControllerConfig(raw)
	if err != nil {
		t.Fatalf("loadControllerConfig: %v", err)
	}
	want := controllerConfig{
		Addr: "ctrl:9090", TLS: true, CAFile: "/etc/vmafx/ca.pem", ServerName: "ctrl.example",
		TokenFile: "/var/run/token", RPCTimeout: 3 * time.Second, HeartbeatInterval: 4 * time.Second,
		PollInterval: 5 * time.Second, NodeName: "pod-7", Slots: 2,
	}
	if got != want {
		t.Fatalf("controller config = %+v, want %+v", got, want)
	}
}

// TestWithNodeGRPCDefault pins the historical standalone-node port while also
// proving that an operator-supplied address is never replaced by the decorator.
func TestWithNodeGRPCDefault(t *testing.T) {
	t.Run("missing uses node default", func(t *testing.T) {
		t.Setenv("VMAFX_GRPC_LISTEN", "")
		raw, err := config.New(nodeEnvOptions(false))
		if err != nil {
			t.Fatalf("config.New: %v", err)
		}

		got := withNodeGRPCDefault(grpcmod.DefaultConfig(), raw)
		if got.Listen != defaultNodeGRPCListen {
			t.Errorf("Listen = %q, want %q", got.Listen, defaultNodeGRPCListen)
		}
	})

	t.Run("explicit override is preserved", func(t *testing.T) {
		const override = ":9090"
		t.Setenv("VMAFX_GRPC_LISTEN", override)
		raw, err := config.New(nodeEnvOptions(false))
		if err != nil {
			t.Fatalf("config.New: %v", err)
		}

		got := withNodeGRPCDefault(grpcmod.Config{
			Listen:      override,
			MaxRecvSize: 8 << 20,
			MaxSendSize: 16 << 20,
		}, raw)
		if got.Listen != override {
			t.Errorf("Listen = %q, want explicit override %q", got.Listen, override)
		}
		if got.MaxRecvSize != 8<<20 || got.MaxSendSize != 16<<20 {
			t.Errorf("decorator changed message-size limits: %+v", got)
		}
	})

	t.Run("file override is preserved", func(t *testing.T) {
		const override = ":9090"
		old, existed := os.LookupEnv("VMAFX_GRPC_LISTEN")
		if err := os.Unsetenv("VMAFX_GRPC_LISTEN"); err != nil {
			t.Fatalf("unset VMAFX_GRPC_LISTEN: %v", err)
		}
		t.Cleanup(func() {
			if existed {
				_ = os.Setenv("VMAFX_GRPC_LISTEN", old)
			} else {
				_ = os.Unsetenv("VMAFX_GRPC_LISTEN")
			}
		})

		path := filepath.Join(t.TempDir(), "node.yaml")
		if err := os.WriteFile(path, []byte("grpc:\n  listen: \":9090\"\n"), 0o600); err != nil {
			t.Fatalf("write config fixture: %v", err)
		}
		opts := nodeEnvOptions(false)
		opts.Files = []string{path}
		raw, err := config.New(opts)
		if err != nil {
			t.Fatalf("config.New: %v", err)
		}
		framework := grpcmod.DefaultConfig()
		if err := raw.Unmarshal("grpc", &framework); err != nil {
			t.Fatalf("unmarshal grpc config: %v", err)
		}

		got := withNodeGRPCDefault(framework, raw)
		if got.Listen != override {
			t.Errorf("Listen = %q, want file override %q", got.Listen, override)
		}
	})
}
