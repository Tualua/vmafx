// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package cmd

import (
	"errors"
	"io"
	"strings"
	"testing"

	"github.com/spf13/cobra"
)

func TestMarkCommandFlagsRequiredRejectsUnknownFlagBeforePreRun(t *testing.T) {
	t.Parallel()

	preRunCalled := false
	cmd := &cobra.Command{
		Use: "test",
		PreRun: func(*cobra.Command, []string) {
			preRunCalled = true
		},
		Run: func(*cobra.Command, []string) {},
	}
	cmd.SetOut(io.Discard)
	cmd.SetErr(io.Discard)
	markCommandFlagsRequired(cmd, "missing")
	err := cmd.Execute()
	if err == nil || !strings.Contains(err.Error(), `mark "missing" required`) {
		t.Fatalf("execute error = %v, want the missing flag name", err)
	}
	var coder exitCoder
	if !errors.As(err, &coder) || coder.ExitCode() != 2 {
		t.Fatalf("execute error = %v, want exit code 2", err)
	}
	if preRunCalled {
		t.Fatal("PreRun ran before the command-construction error")
	}
}

func TestMarkCommandFlagsRequiredPreservesPreRun(t *testing.T) {
	t.Parallel()

	called := false
	cmd := &cobra.Command{Use: "test", PreRun: func(*cobra.Command, []string) {
		called = true
	}}
	cmd.Flags().String("present", "", "test flag")
	markCommandFlagsRequired(cmd, "present")
	if cmd.PreRun == nil {
		t.Fatal("mustMarkCommandFlagsRequired cleared PreRun")
	}
	cmd.PreRun(cmd, nil)
	if !called {
		t.Fatal("existing PreRun hook was not preserved")
	}
}

// runRootForExit runs the full command tree with args and returns the error and
// the process exit status Execute would use.
func runRootForExit(t *testing.T, args ...string) (int, error) {
	t.Helper()
	root := newRoot("dev")
	root.Cobra().SetArgs(args)
	root.Cobra().SetOut(io.Discard)
	root.Cobra().SetErr(io.Discard)
	err := root.Execute()
	return resolveExitCode(err), err
}

// TestEveryCommandRejectsUnknownFlagWithUsageStatus: an unknown flag exits 2 on
// every subcommand, as argparse does in the Python CLI. Before the root carried
// the flag-error function only benchmark, encode-profile and sidecar did.
func TestEveryCommandRejectsUnknownFlagWithUsageStatus(t *testing.T) {
	t.Parallel()
	for _, sub := range newRoot("dev").Cobra().Commands() {
		name := sub.Name()
		if name == "help" || name == "completion" {
			continue
		}
		t.Run(name, func(t *testing.T) {
			t.Parallel()
			code, err := runRootForExit(t, name, "--no-such-flag-anywhere")
			if err == nil || code != 2 {
				t.Errorf("%s --no-such-flag-anywhere: err = %v, exit %d, want exit 2", name, err, code)
			}
		})
	}
}

// TestMissingRequiredFlagExitsWithUsageStatus: a missing required flag exits 2
// on every command that declares one (Cobra's own check exited 1).
func TestMissingRequiredFlagExitsWithUsageStatus(t *testing.T) {
	t.Parallel()
	cases := map[string]string{
		"compare":            "reference",
		"corpus":             "source",
		"predict":            "source",
		"fast":               "target-vmaf",
		"tune-per-shot":      "src",
		"ladder":             "reference",
		"recommend-saliency": "src",
		"auto":               "src",
		"prefilter":          "target-vmaf",
		"recommend":          "source",
		"report":             "input JSON",
	}
	for name, flag := range cases {
		t.Run(name, func(t *testing.T) {
			t.Parallel()
			code, err := runRootForExit(t, name)
			if err == nil || !strings.Contains(err.Error(), flag) {
				t.Fatalf("%s without flags: err = %v, want it to name %q", name, err, flag)
			}
			if code != 2 {
				t.Errorf("%s without flags: exit %d, want 2", name, code)
			}
		})
	}
}

func TestMarkCommandFlagsRequiredRunsPreRunEAfterCheck(t *testing.T) {
	t.Parallel()
	calls := 0
	cmd := &cobra.Command{Use: "test",
		PreRunE: func(*cobra.Command, []string) error { calls++; return nil },
		Run:     func(*cobra.Command, []string) {}}
	cmd.SetOut(io.Discard)
	cmd.SetErr(io.Discard)
	cmd.Flags().String("need", "", "")
	markCommandFlagsRequired(cmd, "need")
	cmd.SetArgs([]string{})
	if err := cmd.Execute(); err == nil || resolveExitCode(err) != 2 {
		t.Fatalf("missing flag: err = %v, want exit 2", err)
	}
	if calls != 0 {
		t.Fatal("the command's PreRunE ran although a required flag was missing")
	}
	cmd.SetArgs([]string{"--need", "x"})
	if err := cmd.Execute(); err != nil {
		t.Fatalf("flag present: %v", err)
	}
	if calls != 1 {
		t.Fatalf("the command's PreRunE ran %d times, want 1", calls)
	}
}
