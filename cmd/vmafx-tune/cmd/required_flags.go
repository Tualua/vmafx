// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package cmd

import (
	"errors"
	"fmt"

	"github.com/spf13/cobra"
)

// markCommandFlagsRequired applies Cobra's required-flag annotations and
// turns any command-construction defect into an exit-2 validation error.
//
// A missing required flag exits 2, the Python CLI's usage status: Cobra checks
// the annotations itself after PreRunE and returns a plain error (exit 1), so
// the check is run first from PreRunE and tagged with asUsageError. The
// message is Cobra's own. A PreRunE or PreRun the command already had still
// runs after the check.
func markCommandFlagsRequired(cmd *cobra.Command, names ...string) {
	var configErr error
	for _, name := range names {
		if err := cmd.MarkFlagRequired(name); err != nil {
			configErr = errors.Join(configErr, fmt.Errorf("mark %q required: %w", name, err))
		}
	}
	if configErr == nil {
		checkRequiredFlagsFirst(cmd)
		return
	}
	cmd.Args = func(current *cobra.Command, _ []string) error {
		return &exitCodeError{code: 2, err: fmt.Errorf(
			"configure required flags for %s: %w", current.Name(), configErr)}
	}
}

// checkRequiredFlagsFirst installs the usage-status required-flag check ahead
// of cmd's own pre-run hook.
func checkRequiredFlagsFirst(cmd *cobra.Command) {
	prevE, prev := cmd.PreRunE, cmd.PreRun
	cmd.PreRunE = func(current *cobra.Command, args []string) error {
		if err := current.ValidateRequiredFlags(); err != nil {
			return asUsageError(err)
		}
		if prevE != nil {
			return prevE(current, args)
		}
		if prev != nil {
			prev(current, args)
		}
		return nil
	}
}
