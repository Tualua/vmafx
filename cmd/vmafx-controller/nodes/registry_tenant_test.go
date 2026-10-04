// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/nodes/registry_tenant_test.go — a node session belongs
// to the tenant that registered it (ADR-1522).

package nodes_test

import (
	"testing"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/nodes"
)

// testTenant is the tenant the registry tests register their nodes for.
const testTenant = "acme"

func TestSessionRefusedForAnotherTenant(t *testing.T) {
	r := nodes.NewRegistry(nil)
	defer r.Close()
	id, tok, err := r.Register("w", testTenant, nodes.Capability{Backends: []string{"cpu"}, Concurrency: 1})
	if err != nil {
		t.Fatalf("Register: %v", err)
	}
	for _, other := range []string{"rival", "", "acme ", "ACME"} {
		if r.ValidateSession(id, tok, other) {
			t.Errorf("ValidateSession with tenant %q accepted a session of %q", other, testTenant)
		}
		if r.Heartbeat(id, tok, other, 0) {
			t.Errorf("Heartbeat with tenant %q accepted a session of %q", other, testTenant)
		}
	}
	if !r.ValidateSession(id, tok, testTenant) || !r.Heartbeat(id, tok, testTenant, 1) {
		t.Fatal("the registering tenant's own session was refused")
	}
	n, ok := r.Get(id)
	if !ok || n.TenantID != testTenant {
		t.Errorf("Get: TenantID = %q (found %v), want %q", n.TenantID, ok, testTenant)
	}
}
