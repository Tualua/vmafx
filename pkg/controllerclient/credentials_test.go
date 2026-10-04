// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// pkg/controllerclient/credentials_test.go — positive, negative and boundary
// cases of the controller client credentials (ADR-1569).

package controllerclient

import (
	"context"
	"encoding/base64"
	"fmt"
	"os"
	"path/filepath"
	"strings"
	"testing"
	"time"
)

type mapGetter map[string]string

func (m mapGetter) Get(k string) string { return m[k] }

func TestLoad(t *testing.T) {
	got, err := Load(mapGetter{
		"controller.tls": "true", "controller.ca_file": "/ca.pem",
		"controller.server_name": "vmafx-controller", "controller.token_file": " /tok ",
	})
	if err != nil || !got.TLS || got.CAFile != "/ca.pem" || got.ServerName != "vmafx-controller" || got.TokenFile != "/tok" {
		t.Fatalf("Load = %+v, %v", got, err)
	}
	if got, err := Load(mapGetter{}); err != nil || got.HasToken() || got.TLS {
		t.Fatalf("empty config: %+v, %v; want plaintext without a token", got, err)
	}
	refused := map[string]mapGetter{
		"tls not a boolean": {"controller.tls": "yes please"},
		"both tokens":       {"controller.token": "a", "controller.token_file": "/tok"},
		"CA without TLS":    {"controller.ca_file": "/ca.pem"},
		"name without TLS":  {"controller.server_name": "x"},
	}
	for name, cfg := range refused {
		if _, err := Load(cfg); err == nil {
			t.Errorf("%s: Load accepted it", name)
		}
	}
}

func TestTransportCredentials(t *testing.T) {
	if creds, err := (Credentials{}).TransportCredentials(); creds != nil || err != nil {
		t.Fatalf("plaintext: %v, %v; want nil, nil", creds, err)
	}
	if creds, err := (Credentials{TLS: true}).TransportCredentials(); creds == nil || err != nil {
		t.Fatalf("TLS with system roots: %v, %v", creds, err)
	}
	if _, err := (Credentials{TLS: true, CAFile: filepath.Join(t.TempDir(), "missing.pem")}).TransportCredentials(); err == nil {
		t.Fatal("missing CA file accepted")
	}
	garbage := filepath.Join(t.TempDir(), "ca.pem")
	writeFile(t, garbage, "not a certificate")
	if _, err := (Credentials{TLS: true, CAFile: garbage}).TransportCredentials(); err == nil {
		t.Fatal("CA file without a PEM certificate accepted")
	}
}

func TestDialOptions(t *testing.T) {
	if opts, err := (Credentials{}).DialOptions(); err != nil || len(opts) != 0 {
		t.Fatalf("no TLS, no token: %d options, %v; want none", len(opts), err)
	}
	if opts, err := (Credentials{TLS: true, Token: "t"}).DialOptions(); err != nil || len(opts) != 2 {
		t.Fatalf("TLS and token: %d options, %v; want 2", len(opts), err)
	}
}

// header returns the authorization value b sends, or the error.
func header(b *BearerCredentials) (string, error) {
	md, err := b.GetRequestMetadata(context.Background())
	return md["authorization"], err
}

func TestBearerReadsTheFileOnEveryCall(t *testing.T) {
	file := filepath.Join(t.TempDir(), "token")
	writeFile(t, file, "first\n")
	b := Credentials{TokenFile: file}.Bearer()
	if got, err := header(b); err != nil || got != "Bearer first" {
		t.Fatalf("first call: %q, %v", got, err)
	}
	writeFile(t, file, "rotated")
	if got, err := header(b); err != nil || got != "Bearer rotated" {
		t.Fatalf("after rotation: %q, %v; want the new token", got, err)
	}
	writeFile(t, file, "  \n")
	if _, err := header(b); err == nil || !strings.Contains(err.Error(), "is empty") {
		t.Fatalf("empty file: %v; want a refusal", err)
	}
	if err := os.Remove(file); err != nil {
		t.Fatal(err)
	}
	if _, err := header(b); err == nil {
		t.Fatal("missing file accepted")
	}
}

// jwt returns an unsigned JWT-shaped token with the given payload JSON.
func jwt(payload string) string {
	enc := base64.RawURLEncoding.EncodeToString
	return enc([]byte(`{"alg":"RS256"}`)) + "." + enc([]byte(payload)) + ".c2ln"
}

func TestBearerRefusesAnExpiredJWT(t *testing.T) {
	now := time.Unix(2_000_000_000, 0)
	file := filepath.Join(t.TempDir(), "token")
	b := Credentials{TokenFile: file}.Bearer()
	b.now = func() time.Time { return now }
	cases := []struct {
		name, token string
		ok          bool
	}{
		{"expires in a minute", jwt(fmt.Sprintf(`{"exp":%d}`, now.Unix()+60)), true},
		{"expires now (boundary)", jwt(fmt.Sprintf(`{"exp":%d}`, now.Unix())), false},
		{"expired an hour ago", jwt(fmt.Sprintf(`{"exp":%d}`, now.Unix()-3600)), false},
		{"JWT without exp", jwt(`{"sub":"node"}`), true},
		{"opaque token", "not-a-jwt", true},
	}
	for _, tc := range cases {
		writeFile(t, file, tc.token)
		_, err := header(b)
		if tc.ok && err != nil {
			t.Errorf("%s: refused: %v", tc.name, err)
		}
		if !tc.ok && (err == nil || !strings.Contains(err.Error(), file) || !strings.Contains(err.Error(), "expired")) {
			t.Errorf("%s: err %v; want a refusal naming the file and the expiry", tc.name, err)
		}
	}
}

func TestBearerTransportSecurity(t *testing.T) {
	if !(Credentials{TLS: true, Token: "t"}).Bearer().RequireTransportSecurity() {
		t.Error("TLS credentials allow the token over plaintext")
	}
	if (Credentials{Token: "t"}).Bearer().RequireTransportSecurity() {
		t.Error("plaintext credentials demand TLS")
	}
}

func writeFile(t *testing.T, path, content string) {
	t.Helper()
	if err := os.WriteFile(path, []byte(content), 0o600); err != nil {
		t.Fatal(err)
	}
}
