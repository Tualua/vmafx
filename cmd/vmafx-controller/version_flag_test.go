// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/version_flag_test.go — `vmafx-controller --version`
// prints the release and exits without configuration or listeners, as the
// image smoke test and the docs expect (ADR-1589).

//go:build cgo

package main

import (
	"context"
	"os/exec"
	"path/filepath"
	"strings"
	"testing"
	"time"

	"github.com/VMAFx/vmafx/pkg/libvmaf"
)

func TestIsVersionRequest(t *testing.T) {
	if !isVersionRequest([]string{"vmafx-controller", "--version"}) {
		t.Fatal("--version must select the version path")
	}
	for _, args := range [][]string{{"vmafx-controller"}, {"vmafx-controller", "--version", "extra"}, {"vmafx-controller", "-version"}} {
		if isVersionRequest(args) {
			t.Errorf("%q selected the version path", args)
		}
	}
}

func TestVersionFlagPrintsTheReleaseAndExits(t *testing.T) {
	bin := filepath.Join(t.TempDir(), "vmafx-controller")
	build := exec.Command("go", "build", "-o", bin,
		"-ldflags", "-X github.com/VMAFx/vmafx/pkg/version.version=v9.8.7", "./cmd/vmafx-controller")
	build.Dir = libvmaf.RepoRoot()
	if out, err := build.CombinedOutput(); err != nil {
		t.Fatalf("build: %v\n%s", err, out)
	}
	ctx, cancel := context.WithTimeout(context.Background(), 20*time.Second)
	defer cancel()
	// No auth settings in the environment: started for real, the controller
	// would refuse to start; --version must not get that far.
	cmd := exec.CommandContext(ctx, bin, "--version")
	cmd.Env = []string{"PATH=/usr/bin:/bin"}
	out, err := cmd.Output()
	if err != nil || strings.TrimSpace(string(out)) != "v9.8.7" {
		t.Fatalf("--version = %q, %v; want v9.8.7 and exit 0", out, err)
	}
}
