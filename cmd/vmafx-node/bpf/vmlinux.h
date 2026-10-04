// Copyright 2026 Lusoris
// SPDX-License-Identifier: EUPL-1.2

// vmlinux.h — the kernel types rclone_bypass.bpf.c uses, instead of a full
// `bpftool btf dump` of one kernel (3.5 MB, kernel-specific). The two
// tracepoint contexts follow the stable tracepoint ABI of
// /sys/kernel/tracing/events/syscalls/sys_{enter,exit}_*/format; the map-type
// constants are the UAPI values of <linux/bpf.h>. Add a type here only when
// the program starts using it, and regenerate with `go generate`.
//
// ADR-1539.

#ifndef VMAFX_BPF_VMLINUX_H
#define VMAFX_BPF_VMLINUX_H

typedef signed char __s8;
typedef unsigned char __u8;
typedef short __s16;
typedef unsigned short __u16;
typedef int __s32;
typedef unsigned int __u32;
typedef long long __s64;
typedef unsigned long long __u64;
typedef __u16 __be16;
typedef __u32 __be32;
typedef __u32 __wsum;

typedef _Bool bool;
enum {
    false = 0,
    true = 1,
};

// User-space pointer annotation (sparse); no meaning for clang.
#define __user

enum bpf_map_type {
    BPF_MAP_TYPE_HASH = 1,
    BPF_MAP_TYPE_ARRAY = 2,
    BPF_MAP_TYPE_RINGBUF = 27,
};

enum {
    BPF_ANY = 0,
};

struct trace_entry {
    unsigned short type;
    unsigned char flags;
    unsigned char preempt_count;
    int pid;
};

struct trace_event_raw_sys_enter {
    struct trace_entry ent;
    long id;
    unsigned long args[6];
    char __data[0];
};

struct trace_event_raw_sys_exit {
    struct trace_entry ent;
    long id;
    long ret;
    char __data[0];
};

#endif // VMAFX_BPF_VMLINUX_H
