// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/executor_inputs_test.go — the executor scores through the
// storage layer: an http(s) input is streamed with the file score, a failed
// GET fails the job, a remote URI without an rclone mode fails the job, and
// an unknown storage mode stops the node.

//go:build cgo

package main

import (
	"context"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"net/url"
	"path/filepath"
	"strings"
	"testing"

	"go.uber.org/fx"

	controllerv1 "github.com/VMAFx/vmafx/gen/go/controller"
	"github.com/VMAFx/vmafx/internal/vmaftest"
	"github.com/VMAFx/vmafx/pkg/libvmaf"
	"github.com/VMAFx/vmafx/pkg/storage"
)

func realExecutor(t *testing.T, store storage.Storage) (*Executor, string) {
	t.Helper()
	root := libvmaf.RepoRoot()
	scorer, err := libvmaf.New(vmaftest.Binary(t), filepath.Join(root, "model"))
	if err != nil {
		t.Fatalf("libvmaf.New: %v", err)
	}
	return NewExecutorWithStorage(scorer, nil, store, "cpu", slog.New(slog.DiscardHandler)), root
}

// scoringJobFor builds a job for ref and dis with scoring roots that admit
// both (ADR-1577): the input's directory, the URL's server or the remote.
func scoringJobFor(ref, dis string) *controllerv1.Job {
	return &controllerv1.Job{
		Id: "in", Scoring: &controllerv1.ScoringParams{Reference: ref, Distorted: dis, Model: "vmaf_v0.6.1"},
		ScoringRoots: []string{rootOf(ref), rootOf(dis)},
	}
}

// rootOf returns a scoring root that holds input.
func rootOf(input string) string {
	if u, err := url.Parse(input); err == nil && u.Host != "" {
		return u.Scheme + "://" + u.Host + "/"
	}
	return filepath.Dir(input)
}

// TestExecuteScoring_StreamsHTTPInput: a reference served over HTTP scores
// exactly like the same file on disk (positive).
func TestExecuteScoring_StreamsHTTPInput(t *testing.T) {
	_, _, ref, dis, want := e2eMedia(t)
	srv := httptest.NewServer(http.FileServer(http.Dir(filepath.Dir(ref))))
	t.Cleanup(srv.Close)
	exec, _ := realExecutor(t, &storage.HTTPServeStorage{})
	res := exec.Execute(context.Background(), scoringJobFor(srv.URL+"/ref.y4m", dis))
	if res.Error != nil || res.Score != want {
		t.Fatalf("streamed score %v (err %v), want the file score %v", res.Score, res.Error, want)
	}
}

// TestExecuteScoring_HTTPStatusFails: a 404 input fails the job with the
// status, it does not score an empty clip (negative).
func TestExecuteScoring_HTTPStatusFails(t *testing.T) {
	_, _, _, dis, _ := e2eMedia(t)
	srv := httptest.NewServer(http.NotFoundHandler())
	t.Cleanup(srv.Close)
	exec, _ := realExecutor(t, &storage.HTTPServeStorage{})
	res := exec.Execute(context.Background(), scoringJobFor(srv.URL+"/missing.y4m", dis))
	if res.Error == nil || !strings.Contains(res.Error.Error(), "404") {
		t.Fatalf("job error = %v, want the 404", res.Error)
	}
}

// TestExecuteScoring_RemoteWithoutRcloneModeFails: the default executor
// (local storage) refuses an rclone remote instead of handing it to the CLI
// (negative).
func TestExecuteScoring_RemoteWithoutRcloneModeFails(t *testing.T) {
	exec, _ := realExecutor(t, nil)
	_, _, _, dis, _ := e2eMedia(t)
	res := exec.Execute(context.Background(), scoringJobFor("s3://bucket/ref.y4m", dis))
	if res.Error == nil || !strings.Contains(res.Error.Error(), "prepare reference") {
		t.Fatalf("job error = %v, want the reference preparation failure", res.Error)
	}
}

// TestStorageModeRefusedAtStartup: VMAFX_STORAGE_MODE=rclone (the chart's old
// value, never implemented) stops the node (negative).
func TestStorageModeRefusedAtStartup(t *testing.T) {
	writeNodeEnv(t)
	t.Setenv("VMAFX_STORAGE_MODE", "rclone")
	err := fx.New(productionGraph(), fx.NopLogger).Err()
	if err == nil || !strings.Contains(err.Error(), "unknown mode") {
		t.Fatalf("graph error = %v, want the unknown storage mode", err)
	}
}
