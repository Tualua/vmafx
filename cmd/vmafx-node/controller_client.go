// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/controller_client.go — the node's controller client: the
// RegisterNode / Heartbeat / PullWork / ReportResult loop of ADR-0713.
//
// Shape:
//
//   - One session keeper goroutine registers the node (retrying with jittered
//     exponential backoff until the controller answers), then sends a
//     Heartbeat every heartbeat interval. A heartbeat answered ok=false, a
//     call refused with PermissionDenied, or 60 s of failed heartbeats (the
//     controller's eviction window) drops the session and the keeper
//     registers again.
//   - node.slots pull goroutines each wait for a session, call PullWork, run
//     the job through the Executor and report the result. An empty PullWork
//     waits one jittered poll interval; a failed one backs off.
//   - Every RPC carries its own deadline (controller.rpc_timeout, HISS-02).
//
// Shutdown (fx OnStop, after the gRPC server drained): the slots stop pulling,
// a running job may finish until the stop deadline, a job still running then
// is cancelled and reported as failed ("node shutting down"), and the keeper
// stops last so reports during the drain still have a session.

package main

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"math"
	"sync"
	"sync/atomic"
	"time"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"

	controllerv1 "github.com/VMAFx/vmafx/gen/go/controller"
)

const (
	registerRetryBase = 500 * time.Millisecond
	registerRetryMax  = 30 * time.Second
	pullErrorRetryMax = 30 * time.Second
	reportRetryBase   = 500 * time.Millisecond
	reportRetryMax    = 15 * time.Second
	maxReportAttempts = 8
	// controllerEvictionWindow mirrors nodes.HeartbeatTimeout on the
	// controller: after this long without an accepted heartbeat the
	// controller has dropped the session, so the node registers again.
	controllerEvictionWindow = 60 * time.Second
)

// jobExecutor is the part of *Executor the controller client drives.
type jobExecutor interface {
	Execute(ctx context.Context, job *controllerv1.Job) ExecuteResult
}

// controllerClient runs the node side of the controller's Node API.
type controllerClient struct {
	cfg        controllerConfig
	rpc        controllerv1.VmafxControllerClient
	exec       jobExecutor
	capability *controllerv1.NodeCapability
	log        *slog.Logger
	sessions   *sessionHolder
	running    atomic.Int32

	pullCtx, execCtx, keepCtx          context.Context
	cancelPull, cancelExec, cancelKeep context.CancelFunc
	slots, keeper                      sync.WaitGroup
	startOnce, stopOnce                sync.Once
}

// newControllerClient builds a client; nothing runs until start.
func newControllerClient(cfg controllerConfig, rpc controllerv1.VmafxControllerClient,
	exec jobExecutor, capability *controllerv1.NodeCapability, log *slog.Logger,
) *controllerClient {
	c := &controllerClient{cfg: cfg, rpc: rpc, exec: exec, capability: capability, log: log, sessions: newSessionHolder()}
	c.pullCtx, c.cancelPull = context.WithCancel(context.Background())
	c.execCtx, c.cancelExec = context.WithCancel(context.Background())
	c.keepCtx, c.cancelKeep = context.WithCancel(context.Background())
	return c
}

// start launches the session keeper and the pull slots. Idempotent.
func (c *controllerClient) start() {
	c.startOnce.Do(func() {
		c.keeper.Add(1)
		go c.keepSession()
		for slot := range c.cfg.Slots {
			c.slots.Add(1)
			go c.runSlot(slot)
		}
		c.log.Info("controller client started",
			"controller", c.cfg.Addr, "node", c.cfg.NodeName, "slots", c.cfg.Slots,
			"backends", c.capability.GetBackends(), "tls", c.cfg.Creds.TLS,
			"bearer_token", c.cfg.Creds.HasToken())
	})
}

// stop drains the client: no new work, running jobs until ctx ends, then a
// bounded report window, then the session keeper. Idempotent; safe without
// start.
func (c *controllerClient) stop(ctx context.Context) {
	c.stopOnce.Do(func() {
		c.cancelPull()
		if !waitGroupDone(ctx, &c.slots) {
			c.log.Warn("stop deadline reached with jobs running; cancelling them", "running", c.running.Load())
			c.cancelExec()
			grace, cancel := context.WithTimeout(context.Background(), c.cfg.RPCTimeout)
			waitGroupDone(grace, &c.slots)
			cancel()
		}
		c.cancelKeep()
		c.slots.Wait()
		c.keeper.Wait()
		c.cancelExec()
		c.log.Info("controller client stopped")
	})
}

// waitGroupDone waits for wg or ctx and reports whether wg finished.
func waitGroupDone(ctx context.Context, wg *sync.WaitGroup) bool {
	done := make(chan struct{})
	go func() { wg.Wait(); close(done) }()
	select {
	case <-done:
		return true
	case <-ctx.Done():
		return false
	}
}

// keepSession registers and heartbeats until the client stops.
func (c *controllerClient) keepSession() {
	defer c.keeper.Done()
	for c.keepCtx.Err() == nil {
		s, err := c.register(c.keepCtx)
		if err != nil {
			return
		}
		c.heartbeatUntilLost(c.keepCtx, s)
	}
}

// register calls RegisterNode until it succeeds or ctx ends. The attempt count
// is unbounded on purpose (a controller may be down for an hour); ctx bounds
// the loop.
func (c *controllerClient) register(ctx context.Context) (nodeSession, error) {
	b := newBackoff(registerRetryBase, registerRetryMax)
	for attempt := 1; ctx.Err() == nil; attempt++ {
		resp, err := c.registerOnce(ctx)
		if err == nil {
			s := c.sessions.set(resp.GetNodeId(), resp.GetSessionToken())
			c.log.Info("registered with controller", "node_id", s.nodeID, "attempt", attempt)
			return s, nil
		}
		delay := b.next()
		c.log.Warn("RegisterNode failed; retrying", "attempt", attempt, "retry_in", delay,
			"code", status.Code(err).String(), "error", err)
		if sleepCtx(ctx, delay) != nil {
			break
		}
	}
	return nodeSession{}, ctx.Err()
}

func (c *controllerClient) registerOnce(ctx context.Context) (*controllerv1.RegisterNodeResponse, error) {
	rpcCtx, cancel := context.WithTimeout(ctx, c.cfg.RPCTimeout)
	defer cancel()
	resp, err := c.rpc.RegisterNode(rpcCtx, &controllerv1.RegisterNodeRequest{
		Name: c.cfg.NodeName, Capability: c.capability,
	})
	if err != nil {
		return nil, err
	}
	if resp.GetNodeId() == "" || resp.GetSessionToken() == "" {
		return nil, errors.New("controller returned an empty node id or session token")
	}
	return resp, nil
}

// heartbeatUntilLost heartbeats session s until it is lost or ctx ends.
func (c *controllerClient) heartbeatUntilLost(ctx context.Context, s nodeSession) {
	ticker := time.NewTicker(c.cfg.HeartbeatInterval)
	defer ticker.Stop()
	var failingSince time.Time
	for ctx.Err() == nil {
		select {
		case <-ctx.Done():
			return
		case <-c.sessions.lost:
			return
		case <-ticker.C:
		}
		if c.heartbeatLost(ctx, s, &failingSince) {
			c.sessions.invalidate(s.gen)
			return
		}
	}
}

// heartbeatLost sends one heartbeat and reports whether the session is gone.
func (c *controllerClient) heartbeatLost(ctx context.Context, s nodeSession, failingSince *time.Time) bool {
	rpcCtx, cancel := context.WithTimeout(ctx, c.cfg.RPCTimeout)
	defer cancel()
	resp, err := c.rpc.Heartbeat(rpcCtx, &controllerv1.HeartbeatRequest{
		NodeId: s.nodeID, SessionToken: s.token, JobsRunning: c.running.Load(),
	})
	switch {
	case err == nil && resp.GetOk():
		*failingSince = time.Time{}
		return false
	case err == nil:
		c.log.Warn("controller no longer knows this session; registering again", "node_id", s.nodeID)
		return true
	case sessionRefused(err):
		c.log.Warn("heartbeat refused; registering again", "node_id", s.nodeID, "error", err)
		return true
	}
	if failingSince.IsZero() {
		*failingSince = time.Now()
	}
	c.log.Warn("heartbeat failed", "node_id", s.nodeID, "failing_for", time.Since(*failingSince), "error", err)
	return time.Since(*failingSince) >= controllerEvictionWindow
}

// runSlot pulls and runs jobs until the client stops pulling.
func (c *controllerClient) runSlot(slot int) {
	defer c.slots.Done()
	idle := newBackoff(c.cfg.PollInterval, c.cfg.PollInterval)
	failing := newBackoff(c.cfg.PollInterval, pullErrorRetryMax)
	for c.pullCtx.Err() == nil {
		job, err := c.pullOnce(c.pullCtx)
		var delay time.Duration
		switch {
		case err != nil && c.pullCtx.Err() == nil:
			delay = failing.next()
			c.log.Warn("PullWork failed", "slot", slot, "retry_in", delay, "code", status.Code(err).String(), "error", err)
		case err != nil:
			return
		case job == nil:
			failing.reset()
			delay = idle.next()
		default:
			failing.reset()
			c.runJob(slot, job)
			continue
		}
		if sleepCtx(c.pullCtx, delay) != nil {
			return
		}
	}
}

// pullOnce asks the controller for one job under the current session.
func (c *controllerClient) pullOnce(ctx context.Context) (*controllerv1.Job, error) {
	s, err := c.sessions.wait(ctx)
	if err != nil {
		return nil, err
	}
	rpcCtx, cancel := context.WithTimeout(ctx, c.cfg.RPCTimeout)
	defer cancel()
	resp, err := c.rpc.PullWork(rpcCtx, &controllerv1.PullWorkRequest{
		NodeId: s.nodeID, SessionToken: s.token, Capability: c.capability,
	})
	if err != nil {
		if sessionRefused(err) {
			c.sessions.invalidate(s.gen)
		}
		return nil, err
	}
	return resp.GetJob(), nil
}

// runJob executes one job and reports its result.
func (c *controllerClient) runJob(slot int, job *controllerv1.Job) {
	c.running.Add(1)
	c.log.Info("job pulled", "slot", slot, "job_id", job.GetId())
	res := c.exec.Execute(c.execCtx, job)
	c.running.Add(-1)
	if res.Error != nil && c.execCtx.Err() != nil {
		res.Error = fmt.Errorf("node shutting down, job interrupted: %w", res.Error)
	}
	c.report(job.GetId(), res)
}

// report delivers a final result, retrying transient failures with backoff.
func (c *controllerClient) report(jobID string, res ExecuteResult) {
	req := finalResultRequest(jobID, res)
	b := newBackoff(reportRetryBase, reportRetryMax)
	for attempt := 1; attempt <= maxReportAttempts; attempt++ {
		err := c.reportOnce(c.keepCtx, req)
		if err == nil {
			c.log.Info("job result reported", "job_id", jobID, "score", req.GetScore(), "error", req.GetError())
			return
		}
		if !retryableReport(err) || attempt == maxReportAttempts {
			c.log.Error("job result not delivered", "job_id", jobID, "attempts", attempt, "error", err)
			return
		}
		delay := b.next()
		c.log.Warn("ReportResult failed; retrying", "job_id", jobID, "attempt", attempt, "retry_in", delay, "error", err)
		if sleepCtx(c.keepCtx, delay) != nil {
			c.log.Error("job result not delivered: node stopped", "job_id", jobID, "error", err)
			return
		}
	}
}

// reportOnce sends req under the current session.
func (c *controllerClient) reportOnce(ctx context.Context, req *controllerv1.ReportResultRequest) error {
	s, err := c.sessions.wait(ctx)
	if err != nil {
		return err
	}
	req.NodeId, req.SessionToken = s.nodeID, s.token
	rpcCtx, cancel := context.WithTimeout(ctx, c.cfg.RPCTimeout)
	defer cancel()
	resp, err := c.rpc.ReportResult(rpcCtx, req)
	if err != nil {
		if sessionRefused(err) {
			c.sessions.invalidate(s.gen)
		}
		return err
	}
	if !resp.GetOk() {
		return status.Error(codes.FailedPrecondition, "controller answered ReportResult with ok=false")
	}
	return nil
}

// finalResultRequest maps an ExecuteResult onto the terminal ReportResult
// message. A non-finite score or feature cannot be stored by the controller,
// so it turns the job into a failure that names the value instead of being
// dropped.
func finalResultRequest(jobID string, res ExecuteResult) *controllerv1.ReportResultRequest {
	req := &controllerv1.ReportResultRequest{JobId: jobID, Final: true}
	if res.Error != nil {
		req.Error = res.Error.Error()
		return req
	}
	if bad := nonFiniteResult(res); bad != "" {
		req.Error = "executor returned a non-finite value for " + bad
		return req
	}
	req.Score, req.Features = res.Score, res.Features
	return req
}

// nonFiniteResult returns the name of the first NaN or infinite value.
func nonFiniteResult(res ExecuteResult) string {
	if math.IsNaN(res.Score) || math.IsInf(res.Score, 0) {
		return "score"
	}
	for name, v := range res.Features {
		if math.IsNaN(v) || math.IsInf(v, 0) {
			return "feature " + name
		}
	}
	return ""
}

// sessionRefused reports whether the controller refused the node session.
// The controller answers an unknown or expired session with PermissionDenied
// (PullWork, ReportResult).
func sessionRefused(err error) bool {
	return status.Code(err) == codes.PermissionDenied
}

// retryableReport reports whether a failed ReportResult may succeed later.
// Malformed-request codes are final; everything else (unavailable controller,
// deadline, refused session that a new registration answers, a storage error
// on the controller) is retried up to maxReportAttempts.
func retryableReport(err error) bool {
	switch status.Code(err) {
	case codes.InvalidArgument, codes.NotFound, codes.Unimplemented,
		codes.FailedPrecondition, codes.OutOfRange, codes.AlreadyExists:
		return false
	case codes.Canceled:
		return false
	default:
		return true
	}
}
