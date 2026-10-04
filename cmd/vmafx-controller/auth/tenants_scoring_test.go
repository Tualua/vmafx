// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/auth/tenants_scoring_test.go — each VmafxTenant's
// spec.scoring.roots, validated at load (ADR-1577).

package auth_test

import (
	"slices"
	"strings"
	"testing"
	"time"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth"
	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth/authtest"
)

func TestScoringRootsComeFromTheTenant(t *testing.T) {
	iss := authtest.NewIssuer(t)
	acme := tenantSpec("acme", iss)
	acme.Spec.Scoring = &auth.TenantScoring{Roots: []string{"/data/acme", "s3:media/acme"}}
	reg := newRegistry(t, acme, tenantSpec("rival", iss))

	roots, ok := reg.ScoringRoots("acme")
	if !ok || !slices.Equal(roots.Strings(), []string{"/data/acme", "s3:media/acme"}) {
		t.Fatalf("acme roots = %v, %v", roots.Strings(), ok)
	}
	if err := roots.Check("/data/rival/x.y4m"); err == nil {
		t.Fatal("acme's roots admit rival's directory")
	}
	if roots, ok := reg.ScoringRoots("rival"); !ok || !roots.Empty() {
		t.Fatalf("rival without spec.scoring = %v, %v; want no roots (deny by default)", roots.Strings(), ok)
	}
	if _, ok := reg.ScoringRoots("nobody"); ok {
		t.Fatal("an unknown tenant has roots")
	}
}

func TestInvalidScoringRootRefusesTheTenant(t *testing.T) {
	iss := authtest.NewIssuer(t)
	for _, root := range []string{"relative/media", "/data/../etc"} {
		spec := tenantSpec("acme", iss)
		spec.Spec.Scoring = &auth.TenantScoring{Roots: []string{root}}
		reg, err := auth.NewTenantRegistry(time.Minute, nil)
		if err != nil {
			t.Fatal(err)
		}
		if err := reg.Load([]auth.NamedTenantSpec{spec}); err == nil || !strings.Contains(err.Error(), "scoring") {
			t.Errorf("root %q: Load error %v, want a scoring refusal", root, err)
		}
	}
}
