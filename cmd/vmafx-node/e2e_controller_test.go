// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/e2e_controller_test.go — end-to-end: a real vmafx-controller
// process, the node's production fx graph in this process, and the real vmaf
// CLI. A job submitted to the controller is pulled by the node, scored and
// reported; the controller then holds the score the CLI computes for the same
// pair. A job for a backend the node does not advertise stays PENDING.
//
// Requirements are the ones every cgo test of this package already has: the
// in-tree CPU build at core/build-cpu (libvmaf for the link, tools/vmaf for
// scoring; go-ci.yml builds both) or VMAF_BIN pointing at a vmaf binary.

//go:build cgo

package main

import (
	"bytes"
	"context"
	"fmt"
	"os"
	"os/exec"
	"path/filepath"
	"testing"
	"time"

	"go.uber.org/fx/fxtest"
	googlegrpc "google.golang.org/grpc"
	"google.golang.org/grpc/credentials/insecure"

	vmafxv1 "github.com/VMAFx/vmafx/gen/go"
	controllerv1 "github.com/VMAFx/vmafx/gen/go/controller"
	"github.com/VMAFx/vmafx/internal/vmaftest"
	"github.com/VMAFx/vmafx/pkg/libvmaf"
)

const (
	e2eWidth  = 320
	e2eHeight = 240
	e2eFrames = 5
)

// writeY4M writes a synthetic 4:2:0 8-bit clip. noise > 0 perturbs the luma
// deterministically so the distorted clip differs from the reference.
func writeY4M(t *testing.T, path string, noise int) {
	t.Helper()
	var buf bytes.Buffer
	fmt.Fprintf(&buf, "YUV4MPEG2 W%d H%d F25:1 Ip A1:1 C420jpeg\n", e2eWidth, e2eHeight)
	for f := range e2eFrames {
		buf.WriteString("FRAME\n")
		for y := range e2eHeight {
			for x := range e2eWidth {
				v := (x*3 + y*5 + f*7 + (x*y)%23) % 256
				if noise > 0 && (x+y+f)%3 == 0 {
					v = (v + noise) % 256
				}
				buf.WriteByte(byte(v))
			}
		}
		chroma := bytes.Repeat([]byte{128}, 2*(e2eWidth/2)*(e2eHeight/2))
		buf.Write(chroma)
	}
	if err := os.WriteFile(path, buf.Bytes(), 0o600); err != nil {
		t.Fatalf("write %s: %v", path, err)
	}
}

// startController builds and runs vmafx-controller with auth disabled and
// returns its gRPC address.
func startController(t *testing.T, root, vmafBin string) string {
	t.Helper()
	dir := t.TempDir()
	bin := filepath.Join(dir, "vmafx-controller")
	build := exec.Command("go", "build", "-o", bin, "./cmd/vmafx-controller")
	build.Dir = root
	if out, err := build.CombinedOutput(); err != nil {
		t.Fatalf("build vmafx-controller: %v\n%s", err, out)
	}
	grpcAddr := freeLoopbackAddr(t)
	var logs bytes.Buffer
	cmd := exec.Command(bin)
	cmd.Env = append(os.Environ(),
		"VMAFX_GRPC_LISTEN="+grpcAddr, "VMAFX_HTTP_ADDR="+freeLoopbackAddr(t),
		"VMAFX_DB_PATH="+filepath.Join(dir, "jobs.db"), "VMAFX_AUTH_DISABLED=true",
		"VMAFX_VMAF_BINARY="+vmafBin, "VMAFX_MODEL_DIR="+filepath.Join(root, "model"),
		"VMAFX_LOG_LEVEL=warn")
	cmd.Stdout, cmd.Stderr = &logs, &logs
	if err := cmd.Start(); err != nil {
		t.Fatalf("start vmafx-controller: %v", err)
	}
	t.Cleanup(func() {
		_ = cmd.Process.Kill()
		_ = cmd.Wait()
		if t.Failed() {
			t.Logf("controller output:\n%s", logs.String())
		}
	})
	awaitControllerHealth(t, grpcAddr)
	return grpcAddr
}

// awaitControllerHealth polls the controller's VmafxScoring/Health.
func awaitControllerHealth(t *testing.T, addr string) {
	t.Helper()
	conn := dialPlain(t, addr)
	client := vmafxv1.NewVmafxScoringClient(conn)
	deadline := time.Now().Add(30 * time.Second)
	for time.Now().Before(deadline) {
		ctx, cancel := context.WithTimeout(context.Background(), time.Second)
		_, err := client.Health(ctx, &vmafxv1.HealthRequest{})
		cancel()
		if err == nil {
			return
		}
		time.Sleep(100 * time.Millisecond)
	}
	t.Fatalf("controller at %s never became healthy", addr)
}

func dialPlain(t *testing.T, addr string) *googlegrpc.ClientConn {
	t.Helper()
	conn, err := googlegrpc.NewClient(addr, googlegrpc.WithTransportCredentials(insecure.NewCredentials()))
	if err != nil {
		t.Fatalf("dial %s: %v", addr, err)
	}
	t.Cleanup(func() { _ = conn.Close() })
	return conn
}

// awaitTerminal polls GetJob until the job leaves PENDING/RUNNING.
func awaitTerminal(t *testing.T, client controllerv1.VmafxControllerClient, id string) *controllerv1.Job {
	t.Helper()
	deadline := time.Now().Add(90 * time.Second)
	for time.Now().Before(deadline) {
		ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
		job, err := client.GetJob(ctx, &controllerv1.GetJobRequest{JobId: id})
		cancel()
		if err != nil {
			t.Fatalf("GetJob %s: %v", id, err)
		}
		if s := job.GetStatus(); s != controllerv1.JobStatus_PENDING && s != controllerv1.JobStatus_RUNNING {
			return job
		}
		time.Sleep(100 * time.Millisecond)
	}
	t.Fatalf("job %s did not finish within 90s", id)
	return nil
}

func submit(t *testing.T, client controllerv1.VmafxControllerClient, ref, dis, backend string) string {
	t.Helper()
	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	resp, err := client.SubmitJob(ctx, &controllerv1.SubmitJobRequest{Scoring: &controllerv1.ScoringParams{
		Reference: ref, Distorted: dis, Model: "vmaf_v0.6.1", Backend: backend,
	}})
	if err != nil {
		t.Fatalf("SubmitJob: %v", err)
	}
	return resp.GetJobId()
}

// e2eMedia writes the synthetic pair and returns the repository root, the
// vmaf CLI, both paths and the CLI's own score for them.
func e2eMedia(t *testing.T) (root, vmafBin, ref, dis string, want float64) {
	t.Helper()
	root = libvmaf.RepoRoot()
	vmafBin = vmaftest.Binary(t)
	media := t.TempDir()
	ref, dis = filepath.Join(media, "ref.y4m"), filepath.Join(media, "dis.y4m")
	writeY4M(t, ref, 0)
	writeY4M(t, dis, 40)
	scorer, err := libvmaf.New(vmafBin, filepath.Join(root, "model"))
	if err != nil {
		t.Fatalf("libvmaf.New: %v", err)
	}
	want, _, err = scorer.ScoreOnBackend(t.Context(), ref, dis, "vmaf_v0.6.1", "cpu")
	if err != nil {
		t.Fatalf("direct score: %v", err)
	}
	return root, vmafBin, ref, dis, want
}

// startE2ENode runs the node's production graph against the controller.
func startE2ENode(t *testing.T, root, vmafBin, ctrlAddr string, env map[string]string) *fxtest.App {
	t.Helper()
	writeNodeEnv(t)
	t.Setenv("VMAFX_VMAF_BINARY", vmafBin)
	t.Setenv("VMAFX_MODEL_DIR", filepath.Join(root, "model"))
	t.Setenv("VMAFX_CONTROLLER_ADDR", ctrlAddr)
	t.Setenv("VMAFX_CONTROLLER_POLL_INTERVAL", "100ms")
	t.Setenv("VMAFX_NODE_ID", "e2e-node")
	for k, v := range env {
		t.Setenv(k, v)
	}
	app := fxtest.New(t, productionGraph())
	app.RequireStart()
	return app
}

// requireScored asserts the job completed with the CLI's score.
func requireScored(t *testing.T, job *controllerv1.Job, want float64) {
	t.Helper()
	if job.GetStatus() != controllerv1.JobStatus_COMPLETED {
		t.Fatalf("job finished %v with error %q, want COMPLETED", job.GetStatus(), job.GetError())
	}
	if job.GetAssignedNode() == "" {
		t.Fatal("completed job has no assigned node")
	}
	if got := job.GetFinalScore(); got != want || got <= 0 || got >= 100 {
		t.Fatalf("controller score %v, want the CLI's %v (strictly between 0 and 100)", got, want)
	}
	t.Logf("job %s scored %.6f on node %s", job.GetId(), job.GetFinalScore(), job.GetAssignedNode())
}

// TestEndToEndControllerNodeJob: submit -> pull -> score -> report against a
// real controller, and the backend match keeps a foreign job PENDING.
func TestEndToEndControllerNodeJob(t *testing.T) {
	root, vmafBin, ref, dis, want := e2eMedia(t)
	// The controller admits only inputs under the tenant's scoring roots
	// (ADR-1577); the node checks them again with the roots of the job.
	t.Setenv("VMAFX_SCORING_ROOTS", filepath.Dir(ref))
	ctrlAddr := startController(t, root, vmafBin)
	client := controllerv1.NewVmafxControllerClient(dialPlain(t, ctrlAddr))
	foreign := submit(t, client, ref, dis, "cuda")

	app := startE2ENode(t, root, vmafBin, ctrlAddr, nil)
	defer app.RequireStop()

	requireScored(t, awaitTerminal(t, client, submit(t, client, ref, dis, "cpu")), want)

	ctx, cancel := context.WithTimeout(context.Background(), 5*time.Second)
	defer cancel()
	pending, err := client.GetJob(ctx, &controllerv1.GetJobRequest{JobId: foreign})
	if err != nil {
		t.Fatalf("GetJob foreign: %v", err)
	}
	if pending.GetStatus() != controllerv1.JobStatus_PENDING {
		t.Fatalf("cuda job is %v on a cpu-only node, want PENDING", pending.GetStatus())
	}
}

// TestEndToEndControllerNodeRcloneSources: a job whose sources are rclone
// remotes is scored through each storage mode with the CLI's file score:
// http-serve streams both inputs into the CLI, mount reads them from FUSE
// mounts. Needs rclone, and FUSE for the mount mode (declared dependencies).
func TestEndToEndControllerNodeRcloneSources(t *testing.T) {
	root, vmafBin, ref, dis, want := e2eMedia(t)
	t.Setenv("VMAFX_SCORING_ROOTS", ":local:"+filepath.Dir(ref))
	ctrlAddr := startController(t, root, vmafBin)
	client := controllerv1.NewVmafxControllerClient(dialPlain(t, ctrlAddr))
	for _, mode := range []string{"http-serve", "mount"} {
		t.Run(mode, func(t *testing.T) {
			app := startE2ENode(t, root, vmafBin, ctrlAddr, map[string]string{
				"VMAFX_STORAGE_MODE": mode, "VMAFX_STORAGE_MOUNT_ROOT": t.TempDir(),
			})
			defer app.RequireStop()
			id := submit(t, client, ":local:"+ref, ":local:"+dis, "cpu")
			requireScored(t, awaitTerminal(t, client, id), want)
		})
	}
}
