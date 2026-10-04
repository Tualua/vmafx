// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/nodes/reaper_test.go — the reaper evicts silent nodes,
// tells the eviction hook, and keeps running when started detached.

package nodes

import (
	"context"
	"log/slog"
	"testing"
	"time"
)

// TestEvictStale: a node silent past the timeout is evicted and returned; a
// node exactly at the deadline is kept (boundary).
func TestEvictStale(t *testing.T) {
	t.Parallel()
	r := NewRegistry(slog.New(slog.DiscardHandler))
	t.Cleanup(r.Close)
	stale, _, _ := r.Register("stale", "t", Capability{})
	edge, _, _ := r.Register("edge", "t", Capability{})
	now := time.Now()
	r.mu.Lock()
	r.nodes[stale].LastHeartbeat = now.Add(-HeartbeatTimeout - time.Second)
	r.nodes[edge].LastHeartbeat = now.Add(-HeartbeatTimeout)
	r.mu.Unlock()
	evicted := r.evictStale(now)
	if len(evicted) != 1 || evicted[0] != stale {
		t.Fatalf("evicted %v, want only %s", evicted, stale)
	}
	if _, ok := r.Get(edge); !ok {
		t.Fatal("node at the deadline was evicted")
	}
}

// TestReaperCallsEvictionHook: the running reaper evicts a silent node and
// hands its ID to the hook (positive).
func TestReaperCallsEvictionHook(t *testing.T) {
	t.Parallel()
	r := NewRegistry(slog.New(slog.DiscardHandler))
	r.reapInterval, r.timeout = 5*time.Millisecond, 20*time.Millisecond
	got := make(chan string, 1)
	r.SetEvictionHook(func(id string) { got <- id })
	id, _, _ := r.Register("silent", "t", Capability{})
	r.StartDetached()
	t.Cleanup(r.Close)
	select {
	case evicted := <-got:
		if evicted != id {
			t.Fatalf("hook got %s, want %s", evicted, id)
		}
	case <-time.After(2 * time.Second):
		t.Fatal("the reaper never evicted the silent node")
	}
}

// TestStartWithEndedContextStopsReaper documents why the controller starts
// the reaper detached: Start(ctx) stops it when ctx ends, which the fx start
// context does after the start timeout (negative).
func TestStartWithEndedContextStopsReaper(t *testing.T) {
	t.Parallel()
	r := NewRegistry(slog.New(slog.DiscardHandler))
	t.Cleanup(r.Close)
	ctx, cancel := context.WithCancel(context.Background())
	r.Start(ctx)
	cancel()
	deadline := time.Now().Add(2 * time.Second)
	for r.ReaperRunning() && time.Now().Before(deadline) {
		time.Sleep(5 * time.Millisecond)
	}
	if r.ReaperRunning() {
		t.Fatal("reaper still running after its start context ended")
	}
}
