// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

package cmd

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"time"

	"github.com/golusoris/golusoris/core/config"
	"github.com/golusoris/golusoris/otel"
	"github.com/spf13/cobra"
	"go.uber.org/fx"

	"github.com/VMAFx/vmafx/internal/app/bootstrap"
	"github.com/VMAFx/vmafx/pkg/observability"
)

// deps is the set of golusoris framework values injected into a vmafx-tune
// subcommand. Domain commands receive a *slog.Logger for structured
// diagnostics and a *config.Config for VMAFX_-prefixed runtime configuration.
type deps struct {
	Log *slog.Logger
	Cfg *config.Config
	// OTel is the golusoris OpenTelemetry provider set bootstrap.Base wired
	// for this invocation (ADR-0782 / ADR-1119). All three providers are nil
	// when no OTLP endpoint is configured — the silent no-op default — and the
	// fx OnStop hook flushes them at app.Stop. Commands do not need it (spans
	// go through the global tracer); it is populated so tests can prove the
	// wiring without rebuilding the graph.
	OTel *otel.Providers
}

// configOptions overrides the golusoris config defaults so vmafx-tune reads the
// VMAFX_ environment prefix (ADR-1119). golusoris splits every underscore in an
// env var into the config delimiter, so VMAFX_LOG_LEVEL maps to the koanf path
// log.level — which the golusoris log module reads natively (no env bridge).
func configOptions() config.Options {
	return config.Options{
		EnvPrefix: "VMAFX_",
		Delimiter: ".",
		// File-watch is pointless for a short-lived one-shot CLI; disabling it
		// avoids spawning a watcher goroutine + SIGHUP handler per invocation.
		Watch: false,
	}
}

// withGolusoris wraps a domain function so it runs inside a golusoris fx graph
// with config + slog injected, then adapts it to a clikit/cobra RunE handler.
//
// clikit.WithFx is designed for long-running services: its generated RunE calls
// app.Run(), which blocks until a signal and never surfaces an fx.Invoke error
// as the command's exit code. A one-shot tuning subcommand instead needs to (a)
// receive the injected dependencies, (b) run to completion, and (c) propagate a
// non-zero exit code on failure. We therefore build the fx app ourselves via
// bootstrap.Base, populate the dependencies, run the function, and return its
// error through clikit.WithRunE so cobra sets the process exit status.
func newTuneApp(d *deps) (*fx.App, error) {
	app := fx.New(
		bootstrap.Base,
		fx.Replace(configOptions()),
		fx.NopLogger,
		fx.Populate(&d.Log, &d.Cfg, &d.OTel),
	)
	if err := app.Err(); err != nil {
		return nil, fmt.Errorf("vmafx-tune: build dependency graph: %w", err)
	}
	return app, nil
}

func withGolusoris(run func(ctx context.Context, d deps, args []string) error) func(*cobra.Command, []string) error {
	return func(cmd *cobra.Command, args []string) error {
		ctx := cmd.Context()
		if ctx == nil {
			ctx = context.Background()
		}

		var d deps
		app, err := newTuneApp(&d)
		if err != nil {
			return err
		}

		startCtx, cancelStart := context.WithTimeout(ctx, app.StartTimeout())
		defer cancelStart()
		if err := app.Start(startCtx); err != nil {
			return fmt.Errorf("vmafx-tune: start dependency graph: %w", err)
		}

		runCtx := ctx
		var cancelRun context.CancelFunc
		if _, hasDeadline := ctx.Deadline(); !hasDeadline {
			runCtx, cancelRun = context.WithTimeout(ctx, 30*time.Minute)
			defer cancelRun()
		}

		// One SpanTuneCommand span per invocation is the CLI's top-level job
		// span (ADR-0782); the cobra command path (e.g. "vmafx-tune-go sidecar
		// status") is its bounded-cardinality attribute. It ends before
		// app.Stop so the OTel OnStop flush exports it.
		spanCtx, span := observability.StartSpan(runCtx, observability.SpanTuneCommand,
			observability.AttrTuneCommand.String(cmd.CommandPath()))
		runErr := run(spanCtx, d, args)
		observability.EndSpan(span, &runErr)

		stopCtx, cancelStop := context.WithTimeout(context.WithoutCancel(ctx), app.StopTimeout())
		defer cancelStop()
		stopErr := app.Stop(stopCtx)
		if stopErr != nil {
			stopErr = fmt.Errorf("vmafx-tune: stop dependency graph: %w", stopErr)
		}

		// Surface both the domain error and any shutdown error; errors.Join
		// drops nils, so the common (clean) path returns just runErr or nil
		// (AGENTS.md #7: errors.Join for multi-step cleanup).
		return errors.Join(runErr, stopErr)
	}
}
