// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/controller_session.go — the node's controller session.
//
// RegisterNode hands the node a node id and a session token that every later
// Node API call carries. The controller forgets a session when the node misses
// heartbeats for 60 s or when the controller restarts; the next call with the
// old token is refused. sessionHolder keeps the current session, lets the
// pull slots wait for one, and lets any caller that sees the session refused
// drop it so the session keeper registers again.

package main

import (
	"context"
	"sync"
)

// nodeSession is one controller registration.
type nodeSession struct {
	nodeID string
	token  string
	gen    uint64
}

// sessionHolder holds the current session. The zero value is not usable;
// construct it with newSessionHolder.
type sessionHolder struct {
	mu    sync.Mutex
	cur   *nodeSession
	gen   uint64
	ready chan struct{} // closed while cur != nil
	lost  chan struct{} // wakes the session keeper after invalidate
}

func newSessionHolder() *sessionHolder {
	return &sessionHolder{ready: make(chan struct{}), lost: make(chan struct{}, 1)}
}

// set installs a new session and wakes every waiter.
func (h *sessionHolder) set(nodeID, token string) nodeSession {
	h.mu.Lock()
	defer h.mu.Unlock()
	h.gen++
	s := &nodeSession{nodeID: nodeID, token: token, gen: h.gen}
	h.cur = s
	close(h.ready)
	// A loss signalled for the previous session is answered by this one.
	select {
	case <-h.lost:
	default:
	}
	return *s
}

// invalidate drops the session of generation gen and wakes the session keeper.
// A stale generation (a newer session already exists) is ignored, so two slots
// that see the same refusal cause one registration, not two.
func (h *sessionHolder) invalidate(gen uint64) {
	h.mu.Lock()
	defer h.mu.Unlock()
	if h.cur == nil || h.cur.gen != gen {
		return
	}
	h.cur = nil
	h.ready = make(chan struct{})
	select {
	case h.lost <- struct{}{}:
	default:
	}
}

// wait returns the current session, blocking until one exists or ctx ends.
// The loop runs once per session change and ends with ctx.
func (h *sessionHolder) wait(ctx context.Context) (nodeSession, error) {
	for ctx.Err() == nil {
		h.mu.Lock()
		cur, ready := h.cur, h.ready
		h.mu.Unlock()
		if cur != nil {
			return *cur, nil
		}
		select {
		case <-ctx.Done():
		case <-ready:
		}
	}
	return nodeSession{}, ctx.Err()
}
