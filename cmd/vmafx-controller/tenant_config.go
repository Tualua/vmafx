// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/tenant_config.go — reads the tenant source settings
// and builds the tenant registry the auth middleware verifies tokens against.
//
//	VMAFX_AUTH_TENANTS_SOURCE    -> auth.tenants.source    "" (none), "kubernetes" or "file".
//	VMAFX_AUTH_TENANTS_FILE      -> auth.tenants.file      Tenant file (source "file").
//	VMAFX_AUTH_TENANTS_NAMESPACE -> auth.tenants.namespace VmafxTenant namespace (source
//	                                                       "kubernetes"; default: the pod's).
//	VMAFX_AUTH_TENANTS_REFRESH   -> auth.tenants.refresh   Reload interval (default 30s,
//	                                                       1s to 1h).
//
// A setting the chosen source does not use is refused, as is an unknown
// source: the controller never starts with a tenant setting it ignores. The
// initial load runs while the fx graph is built, so an unreadable source or an
// invalid tenant stops the controller before it serves a request.
//
// ADR-1519: tenant registry.

//go:build cgo

package main

import (
	"context"
	"errors"
	"fmt"
	"log/slog"
	"time"

	"go.uber.org/fx"

	"github.com/golusoris/golusoris/core/config"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth"
	"github.com/VMAFx/vmafx/cmd/vmafx-controller/tenants"
)

const (
	tenantSourceFile       = "file"
	tenantSourceKubernetes = "kubernetes"

	defaultTenantRefresh = 30 * time.Second
	minTenantRefresh     = time.Second
	maxTenantRefresh     = time.Hour
	initialTenantLoad    = 30 * time.Second
)

// tenantSettings are the auth.tenants.* settings.
type tenantSettings struct {
	source    string
	file      string
	namespace string
	refresh   time.Duration
}

// readTenantSettings reads and checks the auth.tenants.* settings.
func readTenantSettings(cfg *config.Config) (tenantSettings, error) {
	s := tenantSettings{
		source:    cfg.Get("auth.tenants.source"),
		file:      cfg.Get("auth.tenants.file"),
		namespace: cfg.Get("auth.tenants.namespace"),
		refresh:   defaultTenantRefresh,
	}
	rawRefresh := cfg.Get("auth.tenants.refresh")
	if err := s.check(rawRefresh); err != nil {
		return tenantSettings{}, fmt.Errorf("tenant settings: %w", err)
	}
	if rawRefresh != "" {
		d, err := time.ParseDuration(rawRefresh)
		if err != nil || d < minTenantRefresh || d > maxTenantRefresh {
			return tenantSettings{}, fmt.Errorf("tenant settings: VMAFX_AUTH_TENANTS_REFRESH=%q is not a duration from %s to %s",
				rawRefresh, minTenantRefresh, maxTenantRefresh)
		}
		s.refresh = d
	}
	return s, nil
}

// check refuses settings the chosen source would ignore.
func (s tenantSettings) check(rawRefresh string) error {
	switch s.source {
	case "":
		if s.file != "" || s.namespace != "" || rawRefresh != "" {
			return errors.New("VMAFX_AUTH_TENANTS_FILE, _NAMESPACE and _REFRESH need VMAFX_AUTH_TENANTS_SOURCE")
		}
	case tenantSourceFile:
		if s.file == "" {
			return errors.New("VMAFX_AUTH_TENANTS_SOURCE=file needs VMAFX_AUTH_TENANTS_FILE")
		}
		if s.namespace != "" {
			return errors.New("VMAFX_AUTH_TENANTS_NAMESPACE is not used with VMAFX_AUTH_TENANTS_SOURCE=file")
		}
	case tenantSourceKubernetes:
		if s.file != "" {
			return errors.New("VMAFX_AUTH_TENANTS_FILE is not used with VMAFX_AUTH_TENANTS_SOURCE=kubernetes")
		}
	default:
		return fmt.Errorf("VMAFX_AUTH_TENANTS_SOURCE=%q is not %q or %q", s.source, tenantSourceKubernetes, tenantSourceFile)
	}
	return nil
}

// newSource returns the configured tenant source.
func (s tenantSettings) newSource() (tenants.Source, error) {
	if s.source == tenantSourceFile {
		return tenants.FileSource{Path: s.file}, nil
	}
	return tenants.NewInClusterSource(s.namespace)
}

// provideTenantRegistry builds the tenant registry, or returns nil when no
// tenant source is configured (the global identity provider then applies).
// The first load is strict and synchronous; the refresh loop runs between
// fx's OnStart and OnStop.
func provideTenantRegistry(lc fx.Lifecycle, cfg *config.Config, log *slog.Logger) (*auth.TenantRegistry, error) {
	settings, err := readTenantSettings(cfg)
	if err != nil || settings.source == "" {
		return nil, err
	}
	src, err := settings.newSource()
	if err != nil {
		return nil, err
	}
	reg, err := auth.NewTenantRegistry(tenants.StaleFactor*settings.refresh, log)
	if err != nil {
		return nil, err
	}
	refresher := tenants.NewRefresher(src, reg, settings.refresh, log)
	ctx, cancel := context.WithTimeout(context.Background(), initialTenantLoad)
	defer cancel()
	if err := refresher.LoadInitial(ctx); err != nil {
		return nil, err
	}
	lc.Append(fx.Hook{
		OnStart: func(context.Context) error { refresher.Start(); return nil },
		OnStop:  func(context.Context) error { refresher.Close(); return nil },
	})
	return reg, nil
}
