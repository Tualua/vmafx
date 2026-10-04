// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/executor_inputs.go — turn a scoring job's source URIs into
// scorer inputs through pkg/storage (ADR-0719, ADR-1526).
//
// The storage layer returns, per source, either a local path (a local file or
// a FUSE mount of an rclone remote) or an http(s) URL (an rclone serve http
// instance, or the source itself when it is already a URL). Two paths go to
// the vmaf CLI as files. When either input is a URL, both are streamed into
// the CLI through pipes (libvmaf.Scorer.ScoreReaders): nothing is written to
// the node's disk, and a stream that breaks fails the job.

package main

import (
	"context"
	"fmt"
	"io"
	"log/slog"
	"net/http"
	"os"
	"time"

	controllerv1 "github.com/VMAFx/vmafx/gen/go/controller"
	"github.com/VMAFx/vmafx/pkg/storage"
)

// inputHeaderTimeout bounds the wait for the response headers of a streamed
// input; the body is bounded by the scoring deadline, which closes it.
const inputHeaderTimeout = 30 * time.Second

// inputClient fetches streamed inputs.
var inputClient = &http.Client{Transport: &http.Transport{
	Proxy:                 http.ProxyFromEnvironment,
	ResponseHeaderTimeout: inputHeaderTimeout,
	TLSHandshakeTimeout:   inputHeaderTimeout,
}}

// scoreJob prepares both sources, scores them on the job's backend and
// releases the storage resources (rclone processes, mounts) afterwards.
func (e *Executor) scoreJob(ctx context.Context, sp *controllerv1.ScoringParams) (float64, map[string]float64, error) {
	ref, refDone, err := e.store.Prepare(ctx, sp.GetReference())
	if err != nil {
		return 0, nil, fmt.Errorf("prepare reference %q: %w", sp.GetReference(), err)
	}
	defer refDone()
	dis, disDone, err := e.store.Prepare(ctx, sp.GetDistorted())
	if err != nil {
		return 0, nil, fmt.Errorf("prepare distorted %q: %w", sp.GetDistorted(), err)
	}
	defer disDone()
	backend := jobBackend(sp, e.backend)
	if !storage.IsHTTP(ref) && !storage.IsHTTP(dis) {
		return e.scorer.ScoreOnBackend(ctx, ref, dis, sp.GetModel(), backend)
	}
	refIn, err := openInput(ctx, ref)
	if err != nil {
		return 0, nil, fmt.Errorf("open reference: %w", err)
	}
	disIn, err := openInput(ctx, dis)
	if err != nil {
		closeInput(e.log, refIn)
		return 0, nil, fmt.Errorf("open distorted: %w", err)
	}
	return e.scorer.ScoreReaders(ctx, refIn, disIn, sp.GetModel(), backend)
}

// openInput opens a prepared input as a stream: an http(s) URL with a GET,
// anything else as a local file.
func openInput(ctx context.Context, input string) (io.ReadCloser, error) {
	if !storage.IsHTTP(input) {
		f, err := os.Open(input) // #nosec G304 -- the path comes from the job's storage preparation
		if err != nil {
			return nil, fmt.Errorf("open %s: %w", input, err)
		}
		return f, nil
	}
	req, err := http.NewRequestWithContext(ctx, http.MethodGet, input, nil)
	if err != nil {
		return nil, fmt.Errorf("request %s: %w", input, err)
	}
	resp, err := inputClient.Do(req) // #nosec G107 -- URL from the job's storage preparation
	if err != nil {
		return nil, fmt.Errorf("GET %s: %w", input, err)
	}
	if resp.StatusCode != http.StatusOK {
		closeInput(slog.Default(), resp.Body)
		return nil, fmt.Errorf("GET %s: status %s", input, resp.Status)
	}
	return resp.Body, nil
}

// closeInput closes an input that will not be scored; the job already fails
// for another reason, so a close failure is only logged.
func closeInput(log *slog.Logger, in io.Closer) {
	if err := in.Close(); err != nil {
		log.Warn("close unused scoring input", "error", err)
	}
}
