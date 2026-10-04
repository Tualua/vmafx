// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/controller_cancel_test.go — a job the controller names in a
// heartbeat answer is stopped and reported as cancelled (ADR-1567).
//
// Positive: the heartbeat lists the running job, the controller names it,
// the job's context ends with errCancelledByController and the report says
// "cancelled by the controller". Negative: a name the node does not run
// changes nothing; the job finishes and reports its score.

package main

import (
	"context"
	"errors"
	"slices"
	"strings"
	"testing"
	"time"

	controllerv1 "github.com/VMAFx/vmafx/gen/go/controller"
)

// waitingExecutor scores after hold unless the job's context ends first; it
// sends the context's cause on causes.
func waitingExecutor(hold time.Duration, causes chan<- error) fakeExecutor {
	return fakeExecutor{fn: func(ctx context.Context, _ *controllerv1.Job) ExecuteResult {
		select {
		case <-ctx.Done():
			causes <- context.Cause(ctx)
			return ExecuteResult{Error: ctx.Err()}
		case <-time.After(hold):
			causes <- nil
			return ExecuteResult{Score: 91.5, Features: map[string]float64{"vif": 1}}
		}
	}}
}

func TestControllerClient_HeartbeatCancelStopsTheJob(t *testing.T) {
	f := newFakeController()
	f.onPull = oneJob(&controllerv1.Job{Id: "job-1"}, "")
	f.cancelRunning = func(running []string) []string { return running }
	causes := make(chan error, 1)
	startTestClient(t, testControllerConfig(serveFake(t, f)), waitingExecutor(30*time.Second, causes))

	r := awaitReport(t, f, 10*time.Second)
	if r.GetJobId() != "job-1" || !strings.HasPrefix(r.GetError(), "cancelled by the controller") {
		t.Fatalf("report = %q error %q, want job-1 failed with \"cancelled by the controller: ...\"", r.GetJobId(), r.GetError())
	}
	if cause := <-causes; !errors.Is(cause, errCancelledByController) {
		t.Fatalf("job context cause = %v, want errCancelledByController", cause)
	}
	named := false
	for _, ids := range f.runningSeen() {
		named = named || slices.Equal(ids, []string{"job-1"})
	}
	if !named {
		t.Fatalf("no heartbeat named the running job: %v", f.runningSeen())
	}
}

func TestControllerClient_HeartbeatCancelOfAnotherJobIsIgnored(t *testing.T) {
	f := newFakeController()
	f.onPull = oneJob(&controllerv1.Job{Id: "job-1"}, "")
	f.cancelRunning = func([]string) []string { return []string{"job-elsewhere"} }
	causes := make(chan error, 1)
	startTestClient(t, testControllerConfig(serveFake(t, f)), waitingExecutor(300*time.Millisecond, causes))

	r := awaitReport(t, f, 10*time.Second)
	if r.GetJobId() != "job-1" || r.GetError() != "" || r.GetScore() != 91.5 {
		t.Fatalf("report = %q score %v error %q, want job-1 scored 91.5", r.GetJobId(), r.GetScore(), r.GetError())
	}
	if cause := <-causes; cause != nil {
		t.Fatalf("job context ended with %v, want it to run to the end", cause)
	}
}
