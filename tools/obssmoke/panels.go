// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris

package main

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"os"
	"path/filepath"
	"regexp"
	"slices"
	"strings"

	"github.com/VMAFx/vmafx/pkg/observability/obsgen"
)

// The Grafana variables a query may hold. The time-range ones become a
// window the traffic of this run falls in; every other variable selects all
// values, as the dashboards' "All" does.
var (
	rangeVariable = regexp.MustCompile(`\$(?:\{(?:__rate_interval|__interval|__range)\}|(?:__rate_interval|__interval|__range)\b)`)
	otherVariable = regexp.MustCompile(`\$\{?[A-Za-z_][A-Za-z0-9_]*\}?`)
)

// queryWindow is what the time-range variables become.
const queryWindow = "10m"

// instantiate turns a dashboard query into the query Grafana sends with
// every variable set to "All".
func instantiate(expr string) string {
	return otherVariable.ReplaceAllString(rangeVariable.ReplaceAllString(expr, queryWindow), ".*")
}

// panelResult is one query's outcome.
type panelResult struct {
	key  string
	data bool
	err  error
}

// checkPanels queries every Prometheus query of every VMAFx dashboard and
// holds the outcome against the exemptions.
func checkPanels(ctx context.Context, cfg config) error {
	files, err := filepath.Glob(filepath.Join(cfg.dashboards, "*.json"))
	if err != nil || len(files) == 0 {
		return fmt.Errorf("no dashboards under %s: %v", cfg.dashboards, err)
	}
	var results []panelResult
	for _, f := range files {
		r, err := dashboardResults(ctx, cfg, f)
		if err != nil {
			return err
		}
		results = append(results, r...)
	}
	return judgePanels(results)
}

// dashboardResults runs the Prometheus queries of one dashboard. A vendor
// exporter dashboard is skipped: no GPU exporter runs in the example.
func dashboardResults(ctx context.Context, cfg config, path string) ([]panelResult, error) {
	raw, err := os.ReadFile(path) // #nosec G304 -- a dashboard of the mounted repository
	if err != nil {
		return nil, err
	}
	d, err := obsgen.DashboardQueries(raw)
	if err != nil {
		return nil, fmt.Errorf("%s: %w", path, err)
	}
	if len(d.Exporters) > 0 {
		slog.Info("skip", "dashboard", d.Title, "reason", "a vendor GPU exporter's dashboard; the example runs no GPU exporter")
		return nil, nil
	}
	var out []panelResult
	for _, q := range d.Queries {
		if q.Datasource == "loki" {
			continue
		}
		n, err := promSeries(ctx, cfg.prometheus, instantiate(q.Expr))
		out = append(out, panelResult{key: d.Title + ": " + q.Where, data: n > 0, err: err})
	}
	return out, nil
}

// judgePanels fails a query that errs, a query without data that is not
// exempted, an exempted query that has data, and an exemption no dashboard
// has.
func judgePanels(results []panelResult) error {
	var problems []string
	for _, r := range results {
		if p := judge(r); p != "" {
			problems = append(problems, p)
		}
	}
	for key := range exemptions {
		if !slices.ContainsFunc(results, func(r panelResult) bool { return r.key == key }) {
			problems = append(problems, key+": exempted but no dashboard has this query")
		}
	}
	slog.Info("dashboard queries", "checked", len(results), "exempt", len(exemptions), "problems", len(problems))
	if len(problems) > 0 {
		slices.Sort(problems)
		return errors.New(strings.Join(problems, "\n"))
	}
	return nil
}

// judge returns the problem of one query's outcome, or "".
func judge(r panelResult) string {
	reason, exempt := exemptions[r.key]
	switch {
	case r.err != nil:
		return fmt.Sprintf("%s: %v", r.key, r.err)
	case !r.data && !exempt:
		return r.key + ": no data"
	case r.data && exempt:
		return fmt.Sprintf("%s: has data, remove its exemption (%s)", r.key, reason)
	case exempt:
		slog.Info("exempt", "query", r.key, "reason", reason)
	}
	return ""
}
