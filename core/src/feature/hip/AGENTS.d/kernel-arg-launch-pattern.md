---
paths:
  - core/src/feature/hip/integer_adm_hip.c
  - core/src/feature/hip/integer_cambi_hip.c
invariant: Pass kernel pointer arguments as arrays of void pointers to hipModuleLaunchKernel.
---
<!-- markdownlint-disable MD013 MD032 MD060 -->
# Kernel-arg pattern for `hipModuleLaunchKernel` pointer parameters (ADR-0537)

`__global__` kernel taking pointer parameter (e.g.
`const uint16_t *vif_filt_dev`) -> corresponding entry in host
`void *args[]` array
must be **address of variable holding device pointer**. NOT device
pointer value itself, and NOT address of host memory.

```c
/* CORRECT — &dev_ptr_var points to the variable storing the device ptr */
void *dev_ptr = s->some_dev_malloc;
void *args[] = { /* …, */ &dev_ptr, /* … */ };
hipModuleLaunchKernel(func, …, args, NULL);

/* WRONG — passes a host address that the GPU will dereference */
void *args[] = { /* …, */ (void *)host_static_array, /* … */ };

/* WRONG — passes the pointer value into the position where the HIP
 * runtime expects the address-of-pointer */
void *args[] = { /* …, */ s->some_dev_malloc, /* … */ };
```

Pre-ADR-0537 `integer_vif_hip.c` had second form for filter table
parameter, which AMD GPU dereferenced and faulted on with "Memory
access fault by GPU node-1 ... Reason: Page not present or supervisor
privilege" — on first frame, before any score produced.
