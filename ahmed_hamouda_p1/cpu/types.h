#ifndef TYPES_H
#define TYPES_H

#include <libvirt/libvirt.h>
#define MAX_DOMAINS 128
#define MAX_DOMAIN_NAME 64
#define MAX_VCPUS 128
#define MAX_VCPUS_PER_PCPU 32
#define MAX_PCPUS 128

/* Per-domain minimal metadata (name must be unique) */
typedef struct DomainMeta
{
    char name[MAX_DOMAIN_NAME]; /* unique name */
    int nr_vcpus;
    virDomainPtr domain; /* libvirt handle (can be NULL) */
} DomainMeta;

/* Per-vCPU minimal snapshot info (one entry per guest vCPU) */
typedef struct VcpuState
{
    DomainMeta domain;           /* which domain it belongs to */
    int vcpu_number;             /* guest vCPU index */
    int current_pcpu;            /* host pCPU index where it ran (from snapshot) */
    unsigned long long delta_ns; /* cpuTime_after - cpuTime_before (nanoseconds) */
} VcpuState;

/* tiny record stored inside a PcpuState to list which vCPUs ran on that pCPU */
typedef struct PcpuVcpuEntry
{
    DomainMeta domain;
    int vcpu_number;
    unsigned long long delta_ns;
} PcpuVcpuEntry;

/* Per-physical-CPU state for the interval */
typedef struct PcpuState
{
    int pcpu;                            /* pCPU index (0..N-1) */
    VcpuState vcpus[MAX_VCPUS_PER_PCPU]; /* which vCPUs ran on this pCPU */
    int total_vcpus;                     /* how many vCPUs ran on this pCPU */
    unsigned long long vm_ns;            /* total ns consumed by VMs on this pCPU in the interval */
} PcpuState;

/* Global scheduler state (minimal fields only) */
typedef struct SchedulerState
{
    int host_ncpus;
    DomainMeta domains[MAX_DOMAINS];
    int ndomains;
    VcpuState vcpus[MAX_VCPUS]; /* array of size total_vcpus (allocated by init) */
    int total_vcpus;
    PcpuState pcpus[MAX_PCPUS]; /* array of size host_ncpus (allocated by init) */
    int interval_seconds;       /* sampling interval used to produce delta_ns */
} SchedulerState;

#endif
