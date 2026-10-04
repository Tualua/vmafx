// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/queue/queue_listall_test.go — table-driven tests for
// SQLiteQueue.ListByTenant, Depth, and the repeatCommaQ helper (exercised via
// ListByTenant filtered queries). The jobs here carry the empty tenant; the
// tenant filter itself is tested in queue_tenant_test.go.
//
// ADR-0711: vmafx-controller Phase 4b.1 scope expansion.
// ADR-0962: StreamJobs snapshot uses ListByTenant (was ListAll).

package queue_test

import (
	"context"
	"log/slog"
	"path/filepath"
	"testing"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/queue"
)

// newListTestQueue opens a fresh SQLiteQueue in a temp dir with a discarding logger.
func newListTestQueue(t *testing.T) *queue.SQLiteQueue {
	t.Helper()
	q, err := queue.New(filepath.Join(t.TempDir(), "list.db"),
		slog.New(slog.NewTextHandler(nil, &slog.HandlerOptions{Level: slog.LevelError})))
	if err != nil {
		t.Fatalf("queue.New: %v", err)
	}
	t.Cleanup(func() { _ = q.Close() })
	return q
}

// submitN submits n identical jobs and returns their IDs.
func submitN(t *testing.T, q *queue.SQLiteQueue, n int) []string {
	t.Helper()
	ids := make([]string, n)
	for i := range n {
		id, err := q.Submit(context.Background(), &queue.Job{
			Scoring: queue.ScoringParams{
				Reference: "/r.yuv",
				Distorted: "/d.yuv",
				Backend:   "cpu",
			},
		})
		if err != nil {
			t.Fatalf("Submit[%d]: %v", i, err)
		}
		ids[i] = id
	}
	return ids
}

// TestListByTenant_EmptyQueue returns an empty slice (not nil) without error.
func TestListByTenant_EmptyQueue(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)
	jobs, err := q.ListByTenant(context.Background(), "", nil)
	if err != nil {
		t.Fatalf("ListByTenant: %v", err)
	}
	if len(jobs) != 0 {
		t.Errorf("empty queue: got %d jobs, want 0", len(jobs))
	}
}

// TestListByTenant_NoFilter returns all jobs.
func TestListByTenant_NoFilter(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)
	ids := submitN(t, q, 3)

	jobs, err := q.ListByTenant(context.Background(), "", nil)
	if err != nil {
		t.Fatalf("ListByTenant: %v", err)
	}
	if len(jobs) != 3 {
		t.Errorf("got %d jobs, want 3", len(jobs))
	}

	// Verify IDs match.
	idSet := make(map[string]bool, len(ids))
	for _, id := range ids {
		idSet[id] = true
	}
	for _, j := range jobs {
		if !idSet[j.ID] {
			t.Errorf("unexpected job ID %q in ListByTenant result", j.ID)
		}
	}
}

// TestListByTenant_FilterPending returns only PENDING jobs.
func TestListByTenant_FilterPending(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)
	submitN(t, q, 2)

	jobs, err := q.ListByTenant(context.Background(), "", []string{queue.StatusPending})
	if err != nil {
		t.Fatalf("ListByTenant(pending): %v", err)
	}
	if len(jobs) != 2 {
		t.Errorf("pending filter: got %d, want 2", len(jobs))
	}
	for _, j := range jobs {
		if j.Status != queue.StatusPending {
			t.Errorf("job %q has status %q, want %q", j.ID, j.Status, queue.StatusPending)
		}
	}
}

// TestListByTenant_FilterRunning returns no RUNNING jobs when queue is freshly populated.
func TestListByTenant_FilterRunning(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)
	submitN(t, q, 2)

	jobs, err := q.ListByTenant(context.Background(), "", []string{queue.StatusRunning})
	if err != nil {
		t.Fatalf("ListByTenant(running): %v", err)
	}
	if len(jobs) != 0 {
		t.Errorf("running filter on fresh queue: got %d, want 0", len(jobs))
	}
}

// TestListByTenant_MultipleStatusFilters exercises the IN-clause path (repeatCommaQ)
// with two status values.
func TestListByTenant_MultipleStatusFilters(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)
	submitN(t, q, 3)

	// Filter by both pending + running — should return all 3 since they're pending.
	jobs, err := q.ListByTenant(context.Background(), "", []string{queue.StatusPending, queue.StatusRunning})
	if err != nil {
		t.Fatalf("ListByTenant(pending+running): %v", err)
	}
	if len(jobs) != 3 {
		t.Errorf("pending+running filter: got %d, want 3", len(jobs))
	}
}

// TestListByTenant_OrderedByCreation verifies that ListByTenant returns jobs in creation
// order (oldest first).
func TestListByTenant_OrderedByCreation(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)
	ids := submitN(t, q, 3)

	jobs, err := q.ListByTenant(context.Background(), "", nil)
	if err != nil {
		t.Fatalf("ListByTenant: %v", err)
	}
	if len(jobs) != 3 {
		t.Fatalf("got %d jobs, want 3", len(jobs))
	}
	// Jobs submitted sequentially should come back in the same order.
	for i, j := range jobs {
		if j.ID != ids[i] {
			t.Errorf("position %d: got %q, want %q", i, j.ID, ids[i])
		}
	}
}

// TestDepth reflects PendingCount.
func TestDepth(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)

	if d := q.Depth(); d != 0 {
		t.Errorf("empty: Depth() = %d, want 0", d)
	}

	submitN(t, q, 5)

	if d := q.Depth(); d != 5 {
		t.Errorf("5 submitted: Depth() = %d, want 5", d)
	}
}

// TestListByTenant_ScoringFieldsRoundTrip verifies that Reference, Distorted, Model,
// and Backend are preserved through a ListByTenant round-trip.
func TestListByTenant_ScoringFieldsRoundTrip(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)
	id, err := q.Submit(context.Background(), &queue.Job{
		Scoring: queue.ScoringParams{
			Reference: "/ref.yuv",
			Distorted: "/dis.yuv",
			Model:     "vmaf_v0.6.1",
			Backend:   "cuda",
		},
	})
	if err != nil {
		t.Fatalf("Submit: %v", err)
	}

	jobs, err := q.ListByTenant(context.Background(), "", nil)
	if err != nil {
		t.Fatalf("ListByTenant: %v", err)
	}
	if len(jobs) != 1 {
		t.Fatalf("got %d jobs, want 1", len(jobs))
	}
	j := jobs[0]
	if j.ID != id {
		t.Errorf("ID: got %q, want %q", j.ID, id)
	}
	if j.Scoring.Reference != "/ref.yuv" {
		t.Errorf("Reference: got %q", j.Scoring.Reference)
	}
	if j.Scoring.Backend != "cuda" {
		t.Errorf("Backend: got %q", j.Scoring.Backend)
	}
	if j.Scoring.Model != "vmaf_v0.6.1" {
		t.Errorf("Model: got %q", j.Scoring.Model)
	}
}

// TestListByTenant_TenantIDRoundTrip verifies that a job submitted with a
// non-empty TenantID has that field preserved through the ListByTenant SELECT.
//
// Regression guard for the bug where ListAll (now ListByTenant) omitted
// tenant_id from its SELECT and Scan, causing Job.TenantID to be "" in every
// result — which broke the StreamJobs gRPC handler's ability to surface the
// submitter's tenant.
func TestListByTenant_TenantIDRoundTrip(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)

	id, err := q.Submit(context.Background(), &queue.Job{
		TenantID: "acme-corp",
		Scoring:  queue.ScoringParams{Reference: "/r.yuv", Distorted: "/d.yuv", Backend: "cpu"},
	})
	if err != nil {
		t.Fatalf("Submit: %v", err)
	}

	jobs, err := q.ListByTenant(context.Background(), "acme-corp", nil)
	if err != nil {
		t.Fatalf("ListByTenant: %v", err)
	}
	if len(jobs) != 1 {
		t.Fatalf("got %d jobs, want 1", len(jobs))
	}
	j := jobs[0]
	if j.ID != id {
		t.Errorf("ID: got %q, want %q", j.ID, id)
	}
	if j.TenantID != "acme-corp" {
		t.Errorf("TenantID: got %q, want %q", j.TenantID, "acme-corp")
	}
}

// TestSubmit_TenantIDRoundTrip verifies that a job submitted with TenantID
// has that field preserved through Get as well (covering the getUnlocked path).
func TestSubmit_TenantIDRoundTrip(t *testing.T) {
	t.Parallel()
	q := newListTestQueue(t)

	id, err := q.Submit(context.Background(), &queue.Job{
		TenantID: "tenant-x",
		Scoring:  queue.ScoringParams{Reference: "/r.yuv", Distorted: "/d.yuv"},
	})
	if err != nil {
		t.Fatalf("Submit: %v", err)
	}

	got, err := q.Get(context.Background(), id)
	if err != nil {
		t.Fatalf("Get: %v", err)
	}
	if got.TenantID != "tenant-x" {
		t.Errorf("TenantID after Get: got %q, want %q", got.TenantID, "tenant-x")
	}
}
