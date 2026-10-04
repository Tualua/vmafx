// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/auth/tenants_internal_test.go — the registry verifies a
// token's signature once per JWKS cache, so a forged token naming a shared
// issuer cannot cost one RSA verification per tenant (ADR-1519).

package auth

import (
	"crypto/rand"
	"crypto/rsa"
	"fmt"
	"log/slog"
	"net/http"
	"net/http/httptest"
	"strings"
	"sync/atomic"
	"testing"
	"time"

	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth/authtest"
)

func TestSignatureVerifiedOncePerJWKSCache(t *testing.T) {
	iss := authtest.NewIssuer(t)
	other := authtest.NewIssuer(t)
	const tenants = 50
	specs := make([]NamedTenantSpec, 0, tenants+1)
	for i := range tenants {
		specs = append(specs, NamedTenantSpec{Source: "t", Spec: TenantSpec{
			TenantID: fmt.Sprintf("org-%02d", i),
			OIDC:     TenantOIDC{Issuer: iss.URL(), JWKSEndpoint: iss.URL(), Audience: "api", TenantClaim: "org_id"},
		}})
	}
	// One more tenant of the same issuer behind a second JWKS endpoint.
	specs = append(specs, NamedTenantSpec{Source: "t", Spec: TenantSpec{
		TenantID: "mirror",
		OIDC:     TenantOIDC{Issuer: iss.URL(), JWKSEndpoint: other.URL(), Audience: "api", TenantClaim: "org_id"},
	}})
	reg, err := NewTenantRegistry(time.Minute, nil)
	if err != nil {
		t.Fatal(err)
	}
	if err := reg.Load(specs); err != nil {
		t.Fatalf("Load: %v", err)
	}
	var calls atomic.Int64
	saved := rs256Verify
	rs256Verify = func(m, s []byte, k *rsa.PublicKey) error { calls.Add(1); return saved(m, s, k) }
	t.Cleanup(func() { rs256Verify = saved })

	forged := iss.Token(t, map[string]any{"org_id": "org-07", "aud": "api"})
	forged = forged[:len(forged)-8] + "AAAAAAAA" // break the signature
	if _, err := reg.Resolve(forged); err == nil {
		t.Fatal("a token with a broken signature resolved")
	}
	if got := calls.Load(); got > 2 {
		t.Errorf("a forged token cost %d signature checks for 2 JWKS endpoints, want at most 2", got)
	}
	calls.Store(0)
	claims, err := reg.Resolve(iss.Token(t, map[string]any{"org_id": "org-07", "aud": "api"}))
	if err != nil || claims.TenantID != "org-07" {
		t.Fatalf("valid token: %+v, %v", claims, err)
	}
	if got := calls.Load(); got > 2 {
		t.Errorf("a valid token cost %d signature checks, want at most 2", got)
	}
}

// swappableJWKS serves a JWKS document that the test can replace, or a 500.
type swappableJWKS struct {
	doc    atomic.Value // string
	broken atomic.Bool
}

func (s *swappableJWKS) ServeHTTP(w http.ResponseWriter, _ *http.Request) {
	if s.broken.Load() {
		http.Error(w, "down", http.StatusInternalServerError)
		return
	}
	_, _ = w.Write([]byte(s.doc.Load().(string)))
}

// fakeClock is a settable clock for the JWKS cache.
type fakeClock struct{ t atomic.Int64 }

func (c *fakeClock) now() time.Time          { return time.Unix(0, c.t.Load()) }
func (c *fakeClock) advance(d time.Duration) { c.t.Add(int64(d)) }

func newTestCache(t *testing.T, h http.Handler) (*jwksCache, *fakeClock) {
	t.Helper()
	srv := httptest.NewServer(h)
	t.Cleanup(srv.Close)
	clk := &fakeClock{}
	clk.t.Store(time.Now().UnixNano())
	c := newJWKSCache(srv.URL, slog.Default())
	c.now = clk.now
	return c, clk
}

func TestWithdrawnKeyStopsVerifyingAfterMaxAge(t *testing.T) {
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	h := &swappableJWKS{}
	h.doc.Store(authtest.JWKS(map[string]*rsa.PublicKey{"k1": &key.PublicKey}))
	c, clk := newTestCache(t, h)
	if _, err := c.Key("k1"); err != nil {
		t.Fatalf("first fetch: %v", err)
	}
	h.doc.Store(`{"keys":[]}`) // the IdP withdraws k1
	clk.advance(jwksKeyMaxAge - time.Second)
	if _, err := c.Key("k1"); err != nil {
		t.Fatalf("k1 inside its max age: %v", err)
	}
	clk.advance(2 * time.Second)
	if _, err := c.Key("k1"); err == nil {
		t.Fatal("a key the IdP withdrew still verifies after jwksKeyMaxAge")
	}
}

func TestCachedKeysOutliveAFailingEndpointOnlyUpToTheHardBound(t *testing.T) {
	key, err := rsa.GenerateKey(rand.Reader, 2048)
	if err != nil {
		t.Fatal(err)
	}
	h := &swappableJWKS{}
	h.doc.Store(authtest.JWKS(map[string]*rsa.PublicKey{"k1": &key.PublicKey}))
	c, clk := newTestCache(t, h)
	if _, err := c.Key("k1"); err != nil {
		t.Fatalf("first fetch: %v", err)
	}
	h.broken.Store(true)
	clk.advance(jwksKeyMaxAge + time.Minute)
	if _, err := c.Key("k1"); err != nil {
		t.Errorf("endpoint down, key past max age but within the hard bound: %v", err)
	}
	clk.advance(jwksKeyHardMaxAge)
	if _, err := c.Key("k1"); err == nil {
		t.Error("endpoint down, key past the hard bound still verifies")
	}
}

func TestJWKSRedirectToPlainHTTPRefused(t *testing.T) {
	srv := httptest.NewTLSServer(http.HandlerFunc(func(w http.ResponseWriter, r *http.Request) {
		http.Redirect(w, r, "http://198.51.100.7/keys", http.StatusFound)
	}))
	t.Cleanup(srv.Close)
	c := newJWKSCache(srv.URL, slog.Default())
	c.client.Transport = srv.Client().Transport
	_, err := c.Key("k1")
	if err == nil || !strings.Contains(err.Error(), "refusing redirect") {
		t.Fatalf("https JWKS redirected to plain http: err = %v, want a refused redirect", err)
	}
}

func TestReloadForgetsUnreferencedJWKSCaches(t *testing.T) {
	reg, err := NewTenantRegistry(time.Minute, nil)
	if err != nil {
		t.Fatal(err)
	}
	spec := func(id, host string) NamedTenantSpec {
		return NamedTenantSpec{Source: id, Spec: TenantSpec{TenantID: id,
			OIDC: TenantOIDC{Issuer: "https://" + host, JWKSEndpoint: "https://" + host + "/keys"}}}
	}
	if err := reg.Load([]NamedTenantSpec{spec("acme", "a.example.com"), spec("rival", "r.example.com")}); err != nil {
		t.Fatal(err)
	}
	if errs := reg.Reload([]NamedTenantSpec{spec("acme", "a.example.com")}); errs != nil {
		t.Fatal(errs)
	}
	reg.mu.Lock()
	defer reg.mu.Unlock()
	if _, ok := reg.caches["https://r.example.com/keys"]; ok || len(reg.caches) != 1 {
		t.Errorf("caches after reload: %v, want only acme's", reg.caches)
	}
}
