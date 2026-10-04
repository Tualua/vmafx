// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/tenants/refresher.go — loads the tenant set at startup
// and reloads it on an interval, so a suspension or a role change takes
// effect without a restart.
//
// Startup is strict (auth.TenantRegistry.Load): an unreadable source or any
// invalid tenant fails it, and the controller does not start. A refresh is
// lenient (Reload): invalid tenants are dropped and the others reloaded. A
// failed refresh keeps the last set, which the registry refuses once it is
// older than its staleness bound.
//
// Lifecycle mirrors nodes.Registry (ADR-1119): Start launches the loop, Close
// stops it and waits for it.
//
// ADR-1519: tenant registry.

package tenants

import (
	"context"
	"fmt"
	"log/slog"
	"sync"
	"sync/atomic"
	"time"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth"
)

// StaleFactor is how many refresh intervals the tenant set may age before the
// registry refuses every token.
const StaleFactor = 10

// Refresher keeps a TenantRegistry in step with a Source.
type Refresher struct {
	src      Source
	reg      *auth.TenantRegistry
	interval time.Duration
	log      *slog.Logger

	ctx       context.Context
	cancel    context.CancelFunc
	startOnce sync.Once
	closeOnce sync.Once
	started   atomic.Bool
	done      chan struct{}
}

// NewRefresher returns a Refresher reloading reg from src every interval.
func NewRefresher(src Source, reg *auth.TenantRegistry, interval time.Duration, log *slog.Logger) *Refresher {
	if log == nil {
		log = slog.Default()
	}
	ctx, cancel := context.WithCancel(context.Background())
	return &Refresher{
		src: src, reg: reg, interval: interval, log: log,
		ctx: ctx, cancel: cancel, done: make(chan struct{}),
	}
}

// LoadInitial loads the source into the registry strictly; any error means
// the controller must not start.
func (r *Refresher) LoadInitial(ctx context.Context) error {
	specs, err := r.src.Load(ctx)
	if err != nil {
		return fmt.Errorf("tenants: initial load from %s: %w", r.src, err)
	}
	if err := r.reg.Load(specs); err != nil {
		return fmt.Errorf("tenants: initial load from %s: %w", r.src, err)
	}
	r.log.Info("tenant configuration loaded", "source", r.src.String(), "tenants", r.reg.Count())
	return nil
}

// RefreshOnce reloads the source leniently and returns the per-tenant errors,
// or the load error (the set is kept).
func (r *Refresher) RefreshOnce(ctx context.Context) ([]error, error) {
	specs, err := r.src.Load(ctx)
	if err != nil {
		r.reg.MarkRefreshFailed(err)
		return nil, err
	}
	return r.reg.Reload(specs), nil
}

// Start launches the refresh loop. It is idempotent.
func (r *Refresher) Start() {
	r.startOnce.Do(func() {
		if r.ctx.Err() != nil {
			close(r.done)
			return
		}
		r.started.Store(true)
		go func() {
			defer close(r.done)
			r.loop()
		}()
	})
}

// Close stops the refresh loop and waits for it. It is safe without Start and
// when called more than once.
func (r *Refresher) Close() {
	r.cancel()
	r.closeOnce.Do(func() {
		if r.started.Load() {
			<-r.done
		}
	})
}

// loop refreshes every interval until Close. Cancellation is the exit
// condition, checked between ticks and while waiting for one.
func (r *Refresher) loop() {
	ticker := time.NewTicker(r.interval)
	defer ticker.Stop()
	for r.ctx.Err() == nil {
		select {
		case <-r.ctx.Done():
			return
		case <-ticker.C:
			if _, err := r.RefreshOnce(r.ctx); err != nil {
				r.log.Warn("tenant refresh failed", "source", r.src.String(), "error", err)
			}
		}
	}
}
