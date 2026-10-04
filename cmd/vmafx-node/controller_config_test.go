// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/controller_config_test.go — validation of the controller
// client configuration, the jittered backoff, the session holder and the
// advertised capability.

package main

import (
	"context"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

// mapConfig is a configGetter over a map.
type mapConfig map[string]string

func (m mapConfig) Get(path string) string { return m[path] }

// TestLoadControllerConfig_Defaults: an address alone yields the documented
// defaults (positive).
func TestLoadControllerConfig_Defaults(t *testing.T) {
	t.Parallel()
	cfg, err := loadControllerConfig(mapConfig{"controller.addr": "ctrl:9090", "node.id": "n1"})
	if err != nil {
		t.Fatalf("loadControllerConfig: %v", err)
	}
	if !cfg.Enabled() || cfg.Slots != 1 || cfg.RPCTimeout != 10*time.Second ||
		cfg.HeartbeatInterval != 10*time.Second || cfg.PollInterval != 2*time.Second || cfg.TLS {
		t.Fatalf("defaults = %+v", cfg)
	}
}

// TestLoadControllerConfig_DisabledWithoutAddr: no address disables the client.
func TestLoadControllerConfig_DisabledWithoutAddr(t *testing.T) {
	t.Parallel()
	cfg, err := loadControllerConfig(mapConfig{"node.id": "n1"})
	if err != nil {
		t.Fatalf("loadControllerConfig: %v", err)
	}
	if cfg.Enabled() {
		t.Fatal("client enabled without controller.addr")
	}
}

// TestLoadControllerConfig_HostnameDefault: node.id falls back to the host name.
func TestLoadControllerConfig_HostnameDefault(t *testing.T) {
	t.Parallel()
	host, hostErr := os.Hostname()
	cfg, err := loadControllerConfig(mapConfig{"controller.addr": "c:1"})
	if hostErr != nil || host == "" {
		if err == nil {
			t.Fatal("no node.id and no host name, yet the config was accepted")
		}
		return
	}
	if err != nil {
		t.Fatalf("loadControllerConfig: %v", err)
	}
	if cfg.NodeName != host {
		t.Fatalf("NodeName = %q, want host name %q", cfg.NodeName, host)
	}
}

// TestLoadControllerConfig_SlotBounds: 1 and 64 are accepted, 0 and 65 are not
// (boundary).
func TestLoadControllerConfig_SlotBounds(t *testing.T) {
	t.Parallel()
	for slots, ok := range map[string]bool{"1": true, "64": true, "0": false, "65": false, "two": false} {
		_, err := loadControllerConfig(mapConfig{"controller.addr": "c:1", "node.id": "n", "node.slots": slots})
		if (err == nil) != ok {
			t.Errorf("node.slots=%s: err=%v, want ok=%v", slots, err, ok)
		}
	}
}

// TestLoadControllerConfig_RefusesInvalid: values the client cannot honour
// fail at startup with a message naming the key (negative).
func TestLoadControllerConfig_RefusesInvalid(t *testing.T) {
	t.Parallel()
	cases := map[string]mapConfig{
		"controller.rpc_timeout":        {"controller.rpc_timeout": "ten"},
		"controller.heartbeat_interval": {"controller.heartbeat_interval": "-1s"},
		"controller.poll_interval":      {"controller.poll_interval": "0s"},
		"controller.tls":                {"controller.tls": "maybe"},
		"not both":                      {"controller.token": "a", "controller.token_file": "/t"},
		"need controller.tls=true":      {"controller.ca_file": "/ca.pem"},
	}
	for want, cfg := range cases {
		cfg["controller.addr"], cfg["node.id"] = "c:1", "n"
		_, err := loadControllerConfig(cfg)
		if err == nil || !strings.Contains(err.Error(), want) {
			t.Errorf("config %v: err=%v, want it to mention %q", cfg, err, want)
		}
	}
}

// TestTransportCredentials covers plaintext, a valid CA and broken CA files.
func TestTransportCredentials(t *testing.T) {
	t.Parallel()
	if creds, err := (controllerConfig{}).transportCredentials(); creds != nil || err != nil {
		t.Fatalf("plaintext: creds=%v err=%v, want nil, nil", creds, err)
	}
	if creds, err := (controllerConfig{TLS: true}).transportCredentials(); creds == nil || err != nil {
		t.Fatalf("system roots: creds=%v err=%v", creds, err)
	}
	missing := controllerConfig{TLS: true, CAFile: filepath.Join(t.TempDir(), "absent.pem")}
	if _, err := missing.transportCredentials(); err == nil {
		t.Fatal("a missing CA file was accepted")
	}
	garbage := filepath.Join(t.TempDir(), "garbage.pem")
	if err := os.WriteFile(garbage, []byte("not a certificate"), 0o600); err != nil {
		t.Fatal(err)
	}
	if _, err := (controllerConfig{TLS: true, CAFile: garbage}).transportCredentials(); err == nil {
		t.Fatal("a CA file without a PEM certificate was accepted")
	}
}

// TestNodeCapability: concrete backends map to their vendor; auto and unknown
// names are refused because the node must advertise what it runs.
func TestNodeCapability(t *testing.T) {
	t.Parallel()
	for backend, vendor := range map[string]string{"cpu": "cpu", "cuda": "nvidia", "hip": "amd", "sycl": "intel", "metal": "apple"} {
		c, err := nodeCapability(backend, 3)
		if err != nil || c.GetGpuVendor() != vendor || c.GetConcurrency() != 3 || len(c.GetBackends()) != 1 || c.GetBackends()[0] != backend {
			t.Errorf("nodeCapability(%q) = %+v, %v", backend, c, err)
		}
	}
	for _, bad := range []string{"auto", "vulkan", ""} {
		if _, err := nodeCapability(bad, 1); err == nil {
			t.Errorf("nodeCapability(%q) accepted", bad)
		}
	}
}

// TestBackoff: delays stay in [window/2, window], the window doubles to the
// cap, and reset returns to the base (boundary at the cap).
func TestBackoff(t *testing.T) {
	t.Parallel()
	b := newBackoff(100*time.Millisecond, 400*time.Millisecond)
	for i, window := range []time.Duration{100, 200, 400, 400, 400} {
		window *= time.Millisecond
		d := b.next()
		if d < window/2 || d > window {
			t.Fatalf("step %d: delay %v outside [%v, %v]", i, d, window/2, window)
		}
	}
	b.reset()
	if d := b.next(); d > 100*time.Millisecond {
		t.Fatalf("after reset: delay %v above the base", d)
	}
	if got := newBackoff(time.Second, time.Millisecond); got.max != time.Second {
		t.Fatalf("max below base not raised: %v", got.max)
	}
}

// TestSleepCtx returns early with the context error when cancelled.
func TestSleepCtx(t *testing.T) {
	t.Parallel()
	ctx, cancel := context.WithCancel(context.Background())
	cancel()
	if err := sleepCtx(ctx, time.Hour); err == nil {
		t.Fatal("sleepCtx ignored a cancelled context")
	}
	if err := sleepCtx(context.Background(), time.Millisecond); err != nil {
		t.Fatalf("sleepCtx: %v", err)
	}
}

// TestSessionHolder: waiters block until a session exists, a stale
// invalidation is ignored, and a loss wakes the keeper once.
func TestSessionHolder(t *testing.T) {
	t.Parallel()
	h := newSessionHolder()
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Millisecond)
	defer cancel()
	if _, err := h.wait(ctx); err == nil {
		t.Fatal("wait returned without a session")
	}
	s1 := h.set("n1", "t1")
	h.invalidate(s1.gen)
	select {
	case <-h.lost:
	default:
		t.Fatal("invalidate did not signal the keeper")
	}
	s2 := h.set("n2", "t2")
	h.invalidate(s1.gen) // stale: must not drop s2
	got, err := h.wait(context.Background())
	if err != nil || got.token != "t2" || got.gen != s2.gen {
		t.Fatalf("wait = %+v, %v; want the second session", got, err)
	}
}
