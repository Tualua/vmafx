// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/registry_lifecycle_test.go — the node registry's reaper
// outlives the fx start context, and its eviction hook requeues the evicted
// node's running jobs.

//go:build cgo

package main

import (
	"context"
	"log/slog"
	"path/filepath"
	"testing"
	"time"

	"go.uber.org/fx/fxtest"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/queue"
)

func newLifecycleQueue(t *testing.T) *queue.SQLiteQueue {
	t.Helper()
	q, err := queue.New(filepath.Join(t.TempDir(), "jobs.db"), slog.New(slog.DiscardHandler))
	if err != nil {
		t.Fatalf("queue.New: %v", err)
	}
	t.Cleanup(func() { _ = q.Close() })
	return q
}

// TestNodeRegistryReaperOutlivesStartContext: fx ends the OnStart context
// after the start timeout; the reaper keeps running (it stopped with that
// context before, so silent nodes were never evicted after startup).
func TestNodeRegistryReaperOutlivesStartContext(t *testing.T) {
	lc := fxtest.NewLifecycle(t)
	r := provideNodeRegistry(lc, newLifecycleQueue(t), slog.New(slog.DiscardHandler))
	startCtx, cancel := context.WithCancel(context.Background())
	if err := lc.Start(startCtx); err != nil {
		t.Fatalf("start: %v", err)
	}
	cancel()
	time.Sleep(50 * time.Millisecond)
	if !r.ReaperRunning() {
		t.Fatal("the reaper stopped when the start context ended")
	}
	if err := lc.Stop(context.Background()); err != nil {
		t.Fatalf("stop: %v", err)
	}
	if r.ReaperRunning() {
		t.Fatal("the reaper is still running after OnStop")
	}
}

// TestRequeueEvictedNodeHook: the hook moves the evicted node's running job
// back to PENDING (positive), and a node without jobs is a no-op (negative).
func TestRequeueEvictedNodeHook(t *testing.T) {
	q := newLifecycleQueue(t)
	ctx := context.Background()
	id, err := q.Submit(ctx, &queue.Job{TenantID: "t", Scoring: queue.ScoringParams{Reference: "/r", Distorted: "/d"}})
	if err != nil {
		t.Fatalf("Submit: %v", err)
	}
	if _, err := q.PullWork(ctx, "gone", "t", queue.NodeCapacity{Slots: 1}); err != nil {
		t.Fatalf("PullWork: %v", err)
	}
	hook := requeueEvictedNode(q, slog.New(slog.DiscardHandler))
	hook("other")
	if j, _ := q.Get(ctx, id); j.Status != queue.StatusRunning {
		t.Fatalf("job of another node changed to %s", j.Status)
	}
	hook("gone")
	if j, _ := q.Get(ctx, id); j.Status != queue.StatusPending || j.AssignedNode != "" {
		t.Fatalf("job after eviction = %+v, want PENDING with no node", j)
	}
}
