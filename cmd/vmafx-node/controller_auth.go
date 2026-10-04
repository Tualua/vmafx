// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/controller_auth.go — bearer-token credentials for the node's
// controller client.
//
// The controller's gRPC auth interceptor reads "authorization: Bearer <JWT>"
// from the request metadata (cmd/vmafx-controller/auth/grpc_interceptor.go).
// The node attaches the token as per-RPC credentials. A token file is re-read
// on every call so a rotated Kubernetes projected token or Secret takes effect
// without a restart.

package main

import (
	"context"
	"errors"
	"fmt"
	"os"
	"strings"

	"google.golang.org/grpc/credentials"
)

// bearerCredentials implements credentials.PerRPCCredentials.
type bearerCredentials struct {
	token      string
	tokenFile  string
	requireTLS bool
}

// newBearerCredentials returns per-RPC credentials for the configured token
// source, or nil when no token is configured (auth-disabled controller).
func newBearerCredentials(cfg controllerConfig) credentials.PerRPCCredentials {
	if cfg.Token == "" && cfg.TokenFile == "" {
		return nil
	}
	return &bearerCredentials{token: cfg.Token, tokenFile: cfg.TokenFile, requireTLS: cfg.TLS}
}

// GetRequestMetadata returns the authorization header for one RPC.
func (b *bearerCredentials) GetRequestMetadata(_ context.Context, _ ...string) (map[string]string, error) {
	tok, err := b.load()
	if err != nil {
		return nil, err
	}
	return map[string]string{"authorization": "Bearer " + tok}, nil
}

// RequireTransportSecurity makes gRPC refuse to send the token over a
// plaintext connection when the operator asked for TLS.
func (b *bearerCredentials) RequireTransportSecurity() bool { return b.requireTLS }

// load returns the inline token or the current content of the token file.
func (b *bearerCredentials) load() (string, error) {
	if b.tokenFile == "" {
		return b.token, nil
	}
	raw, err := os.ReadFile(b.tokenFile)
	if err != nil {
		return "", fmt.Errorf("read controller token file: %w", err)
	}
	tok := strings.TrimSpace(string(raw))
	if tok == "" {
		return "", errors.New("controller token file " + b.tokenFile + " is empty")
	}
	return tok, nil
}
