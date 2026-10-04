// SPDX-License-Identifier: EUPL-1.2
// Copyright 2026 Lusoris
//
// cmd/vmafx-controller/grpc_roles.go — the role each gRPC method of the
// controller requires.
//
// The auth interceptors (auth.Middleware, ADR-1518) authenticate every call
// and then check the caller's roles against this table; a method missing from
// it is refused for every caller. grpc_roles_test.go fails when a method
// registered on the server has no entry here, or an entry names a method the
// server does not serve, so adding an RPC forces a decision about who may
// call it.
//
// The roles are ADR-0794's: a reader reads the jobs of its tenant, a writer
// also submits and cancels them and scores directly (as HTTP POST /v1/score
// requires), and only an admin may act as a compute node.
//
// ADR-0794: multi-tenant auth gateway. ADR-1518: gRPC authorisation.

//go:build cgo

package main

import (
	"github.com/VMAFx/vmafx/cmd/vmafx-controller/auth"
	vmafxv1 "github.com/VMAFx/vmafx/gen/go"
	controllerv1 "github.com/VMAFx/vmafx/gen/go/controller"
)

// controllerMethodRoles returns the role policy of the controller's gRPC
// server. Each call returns a fresh map; auth.New keeps its own copy anyway.
func controllerMethodRoles() auth.MethodRoles {
	read := []string{auth.RoleReader, auth.RoleWriter, auth.RoleAdmin}
	write := []string{auth.RoleWriter, auth.RoleAdmin}
	node := []string{auth.RoleAdmin}
	return auth.MethodRoles{
		vmafxv1.VmafxScoring_Score_FullMethodName:       write,
		vmafxv1.VmafxScoring_ScoreStream_FullMethodName: write,
		vmafxv1.VmafxScoring_Health_FullMethodName:      read,

		controllerv1.VmafxController_SubmitJob_FullMethodName:  write,
		controllerv1.VmafxController_GetJob_FullMethodName:     read,
		controllerv1.VmafxController_CancelJob_FullMethodName:  write,
		controllerv1.VmafxController_StreamJobs_FullMethodName: read,

		controllerv1.VmafxController_RegisterNode_FullMethodName: node,
		controllerv1.VmafxController_Heartbeat_FullMethodName:    node,
		controllerv1.VmafxController_PullWork_FullMethodName:     node,
		controllerv1.VmafxController_ReportResult_FullMethodName: node,
	}
}
