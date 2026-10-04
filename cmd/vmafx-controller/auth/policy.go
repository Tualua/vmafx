// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/auth/policy.go — the per-RPC role policy the gRPC
// interceptors enforce.
//
// Design
// ======
// The interceptors authenticate a call and then authorise it against
// MethodRoles in the same function, so no wiring can authenticate a call
// without also checking its role. A method without an entry is refused for
// every caller, the synthetic admin of the disabled mode included: an RPC
// added to a service without a policy entry fails closed instead of being
// open to any token (ADR-1518).
//
// ADR-0794: multi-tenant auth gateway (the three user roles). ADR-1563: the
// node role.
// ADR-1518: controller gRPC authorisation.

package auth

import (
	"context"
	"fmt"
	"slices"
	"strings"

	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/status"
)

// MethodRoles maps a full gRPC method name ("/package.Service/Method") to the
// roles that may call it. A caller needs at least one of the listed roles.
type MethodRoles map[string][]string

// IsKnownRole reports whether r is one of the roles of ADR-0794 or the node
// role of ADR-1563.
func IsKnownRole(r string) bool {
	return r == RoleReader || r == RoleWriter || r == RoleAdmin || r == RoleNode
}

// cloneMethodRoles validates policy and returns a private copy of it, so a
// caller that keeps mutating its map cannot change what the interceptors
// enforce. Every entry must name a well-formed method and grant at least one
// known role; a misspelt role would otherwise lock a method for everyone
// without any visible error.
func cloneMethodRoles(policy MethodRoles) (MethodRoles, error) {
	out := make(MethodRoles, len(policy))
	for method, roles := range policy {
		if err := validateMethodEntry(method, roles); err != nil {
			return nil, err
		}
		out[method] = slices.Clone(roles)
	}
	return out, nil
}

// validateMethodEntry checks one policy entry.
func validateMethodEntry(method string, roles []string) error {
	parts := strings.Split(method, "/")
	if len(parts) != 3 || parts[0] != "" || parts[1] == "" || parts[2] == "" {
		return fmt.Errorf("auth: policy method %q is not of the form /package.Service/Method", method)
	}
	if len(roles) == 0 {
		return fmt.Errorf("auth: policy for %s grants no role", method)
	}
	for _, r := range roles {
		if !IsKnownRole(r) {
			return fmt.Errorf("auth: policy for %s names unknown role %q", method, r)
		}
	}
	return nil
}

// authorizeGRPC checks the authenticated caller in ctx against the policy
// entry of method. A method without an entry is refused.
func (m *Middleware) authorizeGRPC(ctx context.Context, method string) error {
	roles, ok := m.methodRoles[method]
	if !ok {
		m.log.Warn("grpc auth: no role policy for method; refused", "method", method)
		return status.Errorf(codes.PermissionDenied, "no role may call %s", method)
	}
	if err := requireRoles(ctx, roles); err != nil {
		m.log.Info("grpc auth: role refused", "method", method,
			"tenant_id", TenantIDFromCtx(ctx), "error", err)
		return err
	}
	return nil
}

// requireRoles returns nil when the caller in ctx holds at least one of roles,
// Unauthenticated when ctx carries no claims and PermissionDenied otherwise.
func requireRoles(ctx context.Context, roles []string) error {
	c, ok := ClaimsFromCtx(ctx)
	if !ok {
		return status.Error(codes.Unauthenticated, "unauthenticated")
	}
	if !c.HasRole(roles...) {
		return status.Errorf(codes.PermissionDenied, "role required: %s", strings.Join(roles, " | "))
	}
	return nil
}
