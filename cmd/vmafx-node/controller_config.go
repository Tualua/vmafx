// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-node/controller_config.go — configuration of the node's
// controller client (ADR-0713 node loop).
//
// The node reads these keys through the golusoris config tree (env prefix
// VMAFX_, "." delimiter). Keys whose leaf contains an underscore are declared
// as CompoundKeys in nodeEnvOptions so the env transform keeps them intact:
//
//	VMAFX_CONTROLLER_ADDR               -> controller.addr               gRPC target of the controller; empty disables the client.
//	VMAFX_CONTROLLER_TLS                -> controller.tls                "true" dials with TLS (system roots unless a CA file is set).
//	VMAFX_CONTROLLER_CA_FILE            -> controller.ca_file            PEM bundle that verifies the controller certificate.
//	VMAFX_CONTROLLER_SERVER_NAME        -> controller.server_name        TLS server name override.
//	VMAFX_CONTROLLER_TOKEN_FILE         -> controller.token_file         File holding the bearer token, re-read on every call.
//	VMAFX_CONTROLLER_TOKEN              -> controller.token              Bearer token given inline (MCP parity).
//	VMAFX_CONTROLLER_RPC_TIMEOUT        -> controller.rpc_timeout        Deadline of every controller RPC (default 10s).
//	VMAFX_CONTROLLER_HEARTBEAT_INTERVAL -> controller.heartbeat_interval Heartbeat period (default 10s).
//	VMAFX_CONTROLLER_POLL_INTERVAL      -> controller.poll_interval      Wait after an empty PullWork (default 2s).
//	VMAFX_NODE_ID                       -> node.id                       Name announced in RegisterNode (default: host name).
//	VMAFX_NODE_SLOTS                    -> node.slots                    Concurrent jobs this node runs (default 1, at most 64).
//
// Every value is validated when the node starts: a malformed duration, a slot
// count out of range, an unreadable CA file or both token sources at once is
// a startup error, never a silently substituted default.

package main

import (
	"crypto/tls"
	"crypto/x509"
	"errors"
	"fmt"
	"os"
	"strconv"
	"strings"
	"time"

	"google.golang.org/grpc/credentials"
)

const (
	defaultControllerRPCTimeout = 10 * time.Second
	// defaultHeartbeatInterval matches the controller contract ("Heartbeat is
	// called by a vmafx-node every ~10 s", controller.proto).
	defaultHeartbeatInterval = 10 * time.Second
	defaultPollInterval      = 2 * time.Second
	defaultNodeSlots         = 1
	maxNodeSlots             = 64
)

// controllerConfigKeys lists the underscore-bearing leaf keys of this file.
// nodeEnvOptions appends them to its CompoundKeys.
var controllerConfigKeys = []string{
	"controller.ca_file",
	"controller.server_name",
	"controller.token_file",
	"controller.rpc_timeout",
	"controller.heartbeat_interval",
	"controller.poll_interval",
}

// controllerConfig is the validated configuration of the controller client.
type controllerConfig struct {
	Addr              string
	TLS               bool
	CAFile            string
	ServerName        string
	TokenFile         string
	Token             string
	RPCTimeout        time.Duration
	HeartbeatInterval time.Duration
	PollInterval      time.Duration
	NodeName          string
	Slots             int
}

// configGetter is the read side of *config.Config the parser needs; tests
// pass a map.
type configGetter interface {
	Get(path string) string
}

// Enabled reports whether a controller address is configured.
func (c controllerConfig) Enabled() bool { return c.Addr != "" }

// loadControllerConfig reads and validates the controller-client keys.
func loadControllerConfig(cfg configGetter) (controllerConfig, error) {
	out := controllerConfig{
		Addr:       strings.TrimSpace(cfg.Get("controller.addr")),
		CAFile:     strings.TrimSpace(cfg.Get("controller.ca_file")),
		ServerName: strings.TrimSpace(cfg.Get("controller.server_name")),
		TokenFile:  strings.TrimSpace(cfg.Get("controller.token_file")),
		Token:      strings.TrimSpace(cfg.Get("controller.token")),
		NodeName:   strings.TrimSpace(cfg.Get("node.id")),
	}
	var errs []error
	var err error
	if out.TLS, err = parseBoolKey(cfg, "controller.tls"); err != nil {
		errs = append(errs, err)
	}
	if out.RPCTimeout, err = parseDurationKey(cfg, "controller.rpc_timeout", defaultControllerRPCTimeout); err != nil {
		errs = append(errs, err)
	}
	if out.HeartbeatInterval, err = parseDurationKey(cfg, "controller.heartbeat_interval", defaultHeartbeatInterval); err != nil {
		errs = append(errs, err)
	}
	if out.PollInterval, err = parseDurationKey(cfg, "controller.poll_interval", defaultPollInterval); err != nil {
		errs = append(errs, err)
	}
	if out.Slots, err = parseSlots(cfg); err != nil {
		errs = append(errs, err)
	}
	errs = append(errs, out.validateCredentials()...)
	if len(errs) > 0 {
		return controllerConfig{}, fmt.Errorf("controller client config: %w", errors.Join(errs...))
	}
	return out.withNodeName()
}

// validateCredentials checks the combinations that cannot be honoured.
func (c controllerConfig) validateCredentials() []error {
	var errs []error
	if c.Token != "" && c.TokenFile != "" {
		errs = append(errs, errors.New("set controller.token or controller.token_file, not both"))
	}
	if !c.TLS && (c.CAFile != "" || c.ServerName != "") {
		errs = append(errs, errors.New("controller.ca_file and controller.server_name need controller.tls=true"))
	}
	return errs
}

// withNodeName fills NodeName from the host name when it was not configured.
func (c controllerConfig) withNodeName() (controllerConfig, error) {
	if c.NodeName != "" {
		return c, nil
	}
	host, err := os.Hostname()
	if err != nil || host == "" {
		return controllerConfig{}, fmt.Errorf("controller client config: node.id unset and host name unavailable: %w", err)
	}
	c.NodeName = host
	return c, nil
}

// parseBoolKey parses an optional boolean key; empty means false.
func parseBoolKey(cfg configGetter, key string) (bool, error) {
	raw := strings.TrimSpace(cfg.Get(key))
	if raw == "" {
		return false, nil
	}
	v, err := strconv.ParseBool(raw)
	if err != nil {
		return false, fmt.Errorf("%s=%q is not a boolean", key, raw)
	}
	return v, nil
}

// parseDurationKey parses an optional positive duration key.
func parseDurationKey(cfg configGetter, key string, def time.Duration) (time.Duration, error) {
	raw := strings.TrimSpace(cfg.Get(key))
	if raw == "" {
		return def, nil
	}
	d, err := time.ParseDuration(raw)
	if err != nil {
		return 0, fmt.Errorf("%s=%q is not a duration (examples: 500ms, 10s)", key, raw)
	}
	if d <= 0 {
		return 0, fmt.Errorf("%s=%q must be positive", key, raw)
	}
	return d, nil
}

// parseSlots parses node.slots, which must lie in [1, maxNodeSlots].
func parseSlots(cfg configGetter) (int, error) {
	raw := strings.TrimSpace(cfg.Get("node.slots"))
	if raw == "" {
		return defaultNodeSlots, nil
	}
	n, err := strconv.Atoi(raw)
	if err != nil || n < 1 || n > maxNodeSlots {
		return 0, fmt.Errorf("node.slots=%q must be an integer from 1 to %d", raw, maxNodeSlots)
	}
	return n, nil
}

// transportCredentials returns the TLS credentials for the dial, or nil for a
// plaintext connection.
func (c controllerConfig) transportCredentials() (credentials.TransportCredentials, error) {
	if !c.TLS {
		return nil, nil
	}
	tlsCfg := &tls.Config{MinVersion: tls.VersionTLS13, ServerName: c.ServerName}
	if c.CAFile != "" {
		pem, err := os.ReadFile(c.CAFile)
		if err != nil {
			return nil, fmt.Errorf("read controller CA file: %w", err)
		}
		pool := x509.NewCertPool()
		if !pool.AppendCertsFromPEM(pem) {
			return nil, fmt.Errorf("controller CA file %s holds no PEM certificate", c.CAFile)
		}
		tlsCfg.RootCAs = pool
	}
	return credentials.NewTLS(tlsCfg), nil
}
