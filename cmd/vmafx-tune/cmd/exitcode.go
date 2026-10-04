// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package cmd

import (
	"errors"

	"github.com/spf13/cobra"
)

// exitCodeError carries a specific process exit status out of a subcommand.
//
// Most vmafx-tune subcommands only distinguish success from failure, and
// cobra's plain error return (exit 1) says everything they need to. One does
// not: `encode-profile` runs FFmpeg and its own exit status IS FFmpeg's, the
// way the Python CLI returns int(result.exit_status) from main(). A caller
// scripting around it distinguishes "ffmpeg reported 234" from "the CLI itself
// failed", so collapsing every failure onto 1 would lose information the
// Python command carried.
//
// Execute unwraps this and calls os.Exit with the carried status.
type exitCodeError struct {
	code int
	err  error
}

// Error renders the wrapped message.
func (e exitCodeError) Error() string { return e.err.Error() }

// Unwrap exposes the underlying error to errors.Is / errors.As.
func (e exitCodeError) Unwrap() error { return e.err }

// ExitCode reports the process exit status the CLI should use, satisfying the
// exitCoder interface in root.go. Declared on the value receiver so that both
// construction styles in the tree — exitCodeError{...} and &exitCodeError{...}
// — satisfy it.
func (e exitCodeError) ExitCode() int { return e.code }

// exitCodeOf returns the process exit status an error asks for, defaulting to
// 1 for any error that does not carry one. A nil error means success.
func exitCodeOf(err error) int {
	if err == nil {
		return 0
	}
	var ece exitCodeError
	if errors.As(err, &ece) && ece.code != 0 {
		return ece.code
	}
	return 1
}

// usageExitCode is what the Python CLI returns for a validation or usage
// failure: every `vmaf-tune` subcommand writes a diagnostic to stderr and
// `return 2`, and argparse itself exits 2 on a bad flag.
const usageExitCode = 2

// useUsageExitCode makes cmd, and every subcommand that does not set its own,
// report flag-layer failures — an unknown flag, a value that will not parse —
// with the usage status 2. Cobra raises those inside ParseFlags, before RunE
// ever runs, so wrapping the domain function is not enough on its own. newRoot
// installs it on the root command, so it covers the whole CLI (cobra's
// FlagErrorFunc is inherited from the parent).
//
// A missing required flag takes the same status: markCommandFlagsRequired
// checks the annotations from PreRunE, ahead of Cobra's own check, and the
// commands that never call MarkFlagRequired re-check their flags at the top
// of their run function (benchmark, encode-profile, the sidecar group).
func useUsageExitCode(cmd *cobra.Command) {
	cmd.SetFlagErrorFunc(func(_ *cobra.Command, err error) error {
		return asUsageError(err)
	})
}

// asUsageError tags a validation failure with the exit status the Python CLI
// would have used, leaving an error that already carries one alone.
//
// Every command reports flag-layer failures and missing required flags with
// it (see useUsageExitCode). Validation inside a run function uses it where
// the command was ported with that contract (benchmark, encode-profile, the
// sidecar group); other commands still report their own validation failures
// as exit 1.
func asUsageError(err error) error {
	if err == nil {
		return nil
	}
	if _, ok := errors.AsType[exitCodeError](err); ok {
		return err
	}
	return exitCodeError{code: usageExitCode, err: err}
}
