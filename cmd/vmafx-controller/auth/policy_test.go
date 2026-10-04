// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/auth/policy_test.go — the per-RPC role policy of the
// gRPC interceptors (ADR-1518).
//
// Covers:
//   - a caller without a granted role is refused, on unary and stream calls;
//   - a caller with a granted role reaches the handler;
//   - a token without a roles claim, with an empty one and with an unknown
//     role is refused;
//   - a method without a policy entry is refused for every caller, the
//     disabled mode's synthetic admin included, and a call without method
//     information is refused;
//   - New rejects a malformed policy and keeps its own copy of a valid one.

package auth_test

import (
	"context"
	"strings"
	"testing"

	"google.golang.org/grpc"
	"google.golang.org/grpc/codes"
	"google.golang.org/grpc/metadata"
	"google.golang.org/grpc/status"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth"
	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth/authtest"
)

const (
	writeMethod    = "/test.Service/Write"
	unlistedMethod = "/test.Service/Unlisted"
)

// writePolicy lets writers and admins call writeMethod and lists nothing else.
func writePolicy() auth.MethodRoles {
	return auth.MethodRoles{writeMethod: {auth.RoleWriter, auth.RoleAdmin}}
}

// policyMiddleware returns a Middleware trusting iss with writePolicy.
func policyMiddleware(t *testing.T, iss *authtest.Issuer) *auth.Middleware {
	t.Helper()
	mw, err := auth.New(auth.Config{
		JWKSEndpoint: iss.URL(),
		Issuer:       iss.URL(),
		MethodRoles:  writePolicy(),
	})
	if err != nil {
		t.Fatalf("auth.New: %v", err)
	}
	return mw
}

// bearerCtx returns an incoming-call context carrying token.
func bearerCtx(token string) context.Context {
	return metadata.NewIncomingContext(context.Background(),
		metadata.Pairs("authorization", "Bearer "+token))
}

// callUnary runs the unary interceptor for method and reports whether the
// handler ran.
func callUnary(mw *auth.Middleware, ctx context.Context, method string) (bool, error) {
	ran := false
	_, err := mw.GRPCUnaryInterceptor()(ctx, nil, &grpc.UnaryServerInfo{FullMethod: method},
		func(context.Context, any) (any, error) {
			ran = true
			return nil, nil
		})
	return ran, err
}

// callStream runs the stream interceptor for method and reports whether the
// handler ran.
func callStream(mw *auth.Middleware, ctx context.Context, method string) (bool, error) {
	ran := false
	err := mw.GRPCStreamInterceptor()(nil, &fakeServerStream{ctx: ctx},
		&grpc.StreamServerInfo{FullMethod: method},
		func(any, grpc.ServerStream) error {
			ran = true
			return nil
		})
	return ran, err
}

// wantRoleRefusal fails unless err is the policy's role refusal and the
// handler did not run.
func wantRoleRefusal(t *testing.T, what string, ran bool, err error) {
	t.Helper()
	if ran {
		t.Errorf("%s: handler ran, want the call refused", what)
	}
	if status.Code(err) != codes.PermissionDenied || !strings.Contains(err.Error(), "role required") {
		t.Errorf("%s: err = %v, want PermissionDenied role required", what, err)
	}
}

// roleClaimCases are the roles claims a writer-only method must refuse.
var roleClaimCases = []struct {
	name   string
	claims map[string]any
}{
	{"reader", map[string]any{"tid": "acme", "vmafx_roles": []string{auth.RoleReader}}},
	{"no roles claim", map[string]any{"tid": "acme"}},
	{"empty roles claim", map[string]any{"tid": "acme", "vmafx_roles": []string{}}},
	{"unknown role", map[string]any{"tid": "acme", "vmafx_roles": []string{"vmafx:root"}}},
	{"role-like string", map[string]any{"tid": "acme", "vmafx_roles": "vmafx:writers"}},
}

func TestPolicyRefusesCallerWithoutGrantedRole(t *testing.T) {
	iss := authtest.NewIssuer(t)
	mw := policyMiddleware(t, iss)
	for _, tc := range roleClaimCases {
		ctx := bearerCtx(iss.Token(t, tc.claims))
		ran, err := callUnary(mw, ctx, writeMethod)
		wantRoleRefusal(t, "unary "+tc.name, ran, err)
		ran, err = callStream(mw, ctx, writeMethod)
		wantRoleRefusal(t, "stream "+tc.name, ran, err)
	}
}

func TestPolicyAdmitsCallerWithGrantedRole(t *testing.T) {
	iss := authtest.NewIssuer(t)
	mw := policyMiddleware(t, iss)
	for _, role := range []string{auth.RoleWriter, auth.RoleAdmin} {
		ctx := bearerCtx(iss.Token(t, map[string]any{"tid": "acme", "vmafx_roles": []string{role}}))
		if ran, err := callUnary(mw, ctx, writeMethod); err != nil || !ran {
			t.Errorf("unary as %s: ran=%v err=%v, want the handler to run", role, ran, err)
		}
		if ran, err := callStream(mw, ctx, writeMethod); err != nil || !ran {
			t.Errorf("stream as %s: ran=%v err=%v, want the handler to run", role, ran, err)
		}
	}
}

// wantUnlistedRefusal fails unless the call was refused as unlisted.
func wantUnlistedRefusal(t *testing.T, what string, ran bool, err error) {
	t.Helper()
	if ran || status.Code(err) != codes.PermissionDenied || !strings.Contains(err.Error(), "no role may call") {
		t.Errorf("%s: ran=%v err=%v, want PermissionDenied no role may call", what, ran, err)
	}
}

func TestPolicyRefusesMethodWithoutEntry(t *testing.T) {
	iss := authtest.NewIssuer(t)
	admin := bearerCtx(iss.Token(t, map[string]any{"tid": "acme", "vmafx_roles": []string{auth.RoleAdmin}}))
	mw := policyMiddleware(t, iss)
	ran, err := callUnary(mw, admin, unlistedMethod)
	wantUnlistedRefusal(t, "unary admin", ran, err)
	ran, err = callStream(mw, admin, unlistedMethod)
	wantUnlistedRefusal(t, "stream admin", ran, err)
	ran, err = callUnary(mw, admin, "")
	wantUnlistedRefusal(t, "unary without method", ran, err)

	disabled, err := auth.New(auth.Config{Disabled: true, MethodRoles: writePolicy()})
	if err != nil {
		t.Fatalf("auth.New(disabled): %v", err)
	}
	ran, err = callUnary(disabled, context.Background(), unlistedMethod)
	wantUnlistedRefusal(t, "disabled unary", ran, err)
	ran, err = callStream(disabled, context.Background(), unlistedMethod)
	wantUnlistedRefusal(t, "disabled stream", ran, err)
	if ran, err = callUnary(disabled, context.Background(), writeMethod); err != nil || !ran {
		t.Errorf("disabled listed method: ran=%v err=%v, want the synthetic admin admitted", ran, err)
	}
}

func TestNewRejectsMalformedPolicy(t *testing.T) {
	bad := map[string]auth.MethodRoles{
		"no leading slash":  {"test.Service/Write": {auth.RoleWriter}},
		"no method":         {"/test.Service": {auth.RoleWriter}},
		"empty service":     {"//Write": {auth.RoleWriter}},
		"extra segment":     {"/test.Service/Write/x": {auth.RoleWriter}},
		"no roles":          {writeMethod: {}},
		"unknown role":      {writeMethod: {"vmafx:writers"}},
		"one unknown among": {writeMethod: {auth.RoleWriter, "admin"}},
	}
	for name, policy := range bad {
		if _, err := auth.New(auth.Config{Disabled: true, MethodRoles: policy}); err == nil {
			t.Errorf("%s: auth.New accepted %v", name, policy)
		}
	}
}

func TestNewKeepsItsOwnCopyOfThePolicy(t *testing.T) {
	policy := writePolicy()
	mw, err := auth.New(auth.Config{Disabled: true, MethodRoles: policy})
	if err != nil {
		t.Fatalf("auth.New: %v", err)
	}
	policy[unlistedMethod] = []string{auth.RoleAdmin}
	policy[writeMethod][0] = auth.RoleReader // the synthetic admin would lose both grants
	policy[writeMethod][1] = auth.RoleReader
	ran, err := callUnary(mw, context.Background(), unlistedMethod)
	wantUnlistedRefusal(t, "method added after New", ran, err)
	if ran, err = callUnary(mw, context.Background(), writeMethod); err != nil || !ran {
		t.Errorf("listed method after caller mutation: ran=%v err=%v", ran, err)
	}
}
