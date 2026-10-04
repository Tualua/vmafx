// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-operator/internal/controller/vmafxjob_auth_test.go — the VmafxJob
// reconciler's GetJob carries the controller credentials (ADR-1569).
//
// The fake controller refuses a GetJob without the expected bearer token, as
// the real one does with auth on. Positive: the token file is sent and a
// rotated file applies on the next poll. Negative: without credentials the
// poll is refused; an expired JWT never leaves the operator.

package controller

import (
	"context"
	"encoding/base64"
	"net"
	"os"
	"path/filepath"
	"strings"
	"sync"
	"testing"

	googlegrpc "google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/metadata"
	grpcstatus "google.golang.org/grpc/status"

	controllerv1 "github.com/VMAFx/vmafx/gen/go/controller"
	"github.com/VMAFx/vmafx/pkg/controllerclient"
)

// authController answers GetJob only for an accepted bearer token.
type authController struct {
	controllerv1.UnimplementedVmafxControllerServer
	mu      sync.Mutex
	accept  map[string]bool
	headers []string
}

func (a *authController) GetJob(ctx context.Context, req *controllerv1.GetJobRequest) (*controllerv1.Job, error) {
	md, _ := metadata.FromIncomingContext(ctx)
	got := strings.Join(md.Get("authorization"), ",")
	a.mu.Lock()
	a.headers = append(a.headers, got)
	ok := a.accept[got]
	a.mu.Unlock()
	if !ok {
		return nil, grpcstatus.Error(codes.Unauthenticated, "authentication required")
	}
	return &controllerv1.Job{Id: req.GetJobId(), Status: controllerv1.JobStatus_RUNNING}, nil
}

func (a *authController) seen() []string {
	a.mu.Lock()
	defer a.mu.Unlock()
	return append([]string(nil), a.headers...)
}

// serveAuthController serves a on a loopback port and returns its address.
func serveAuthController(t *testing.T, a *authController) string {
	t.Helper()
	lis, err := net.Listen("tcp", "127.0.0.1:0")
	if err != nil {
		t.Fatalf("listen: %v", err)
	}
	srv := googlegrpc.NewServer()
	controllerv1.RegisterVmafxControllerServer(srv, a)
	go func() { _ = srv.Serve(lis) }()
	t.Cleanup(srv.Stop)
	return lis.Addr().String()
}

func writeToken(t *testing.T, path, token string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(token+"\n"), 0o600); err != nil {
		t.Fatal(err)
	}
}

func TestGetRemoteJobPresentsTheTokenAndFollowsRotation(t *testing.T) {
	fake := &authController{accept: map[string]bool{"Bearer first": true, "Bearer rotated": true}}
	file := filepath.Join(t.TempDir(), "token")
	writeToken(t, file, "first")
	r := &VmafxJobReconciler{
		ControllerAddr:        serveAuthController(t, fake),
		ControllerCredentials: controllerclient.Credentials{TokenFile: file},
	}
	if job, err := r.getRemoteJob(t.Context(), "vmafx", "job-1"); err != nil || job.GetId() != "job-1" {
		t.Fatalf("first poll: job %v, err %v", job, err)
	}
	writeToken(t, file, "rotated")
	if _, err := r.getRemoteJob(t.Context(), "vmafx", "job-1"); err != nil {
		t.Fatalf("poll after rotation: %v", err)
	}
	if got := fake.seen(); len(got) != 2 || got[0] != "Bearer first" || got[1] != "Bearer rotated" {
		t.Fatalf("authorization headers = %q, want [Bearer first, Bearer rotated]", got)
	}
}

func TestGetRemoteJobWithoutCredentialsIsRefused(t *testing.T) {
	fake := &authController{accept: map[string]bool{"Bearer first": true}}
	r := &VmafxJobReconciler{ControllerAddr: serveAuthController(t, fake)}
	_, err := r.getRemoteJob(t.Context(), "vmafx", "job-1")
	if grpcstatus.Code(err) != codes.Unauthenticated {
		t.Fatalf("token-less poll: err %v, want Unauthenticated from the controller", err)
	}
}

func TestGetRemoteJobRefusesAnExpiredTokenBeforeTheCall(t *testing.T) {
	fake := &authController{accept: map[string]bool{}}
	file := filepath.Join(t.TempDir(), "token")
	enc := base64.RawURLEncoding.EncodeToString
	writeToken(t, file, enc([]byte(`{"alg":"RS256"}`))+"."+enc([]byte(`{"exp":1000}`))+".c2ln")
	r := &VmafxJobReconciler{
		ControllerAddr:        serveAuthController(t, fake),
		ControllerCredentials: controllerclient.Credentials{TokenFile: file},
	}
	_, err := r.getRemoteJob(t.Context(), "vmafx", "job-1")
	if err == nil || !strings.Contains(err.Error(), "expired") {
		t.Fatalf("expired token: err %v, want the expiry refusal", err)
	}
	if got := fake.seen(); len(got) != 0 {
		t.Fatalf("the controller saw %q; an expired token must not be sent", got)
	}
}
