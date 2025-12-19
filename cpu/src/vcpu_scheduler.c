#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include "types.h"
#define MIN(a, b) ((a) < (b) ? a : b)
#define MAX(a, b) ((a) > (b) ? a : b)

int is_exit = 0; // DO NOT MODIFY THIS VARIABLE
int nPcpus = 0;
static struct SchedulerState *scheduler_state;

void CPUScheduler(virConnectPtr conn, int interval);

/*
   DO NOT CHANGE THE FOLLOWING FUNCTION
   */
void signal_callback_handler()
{
    printf("Caught Signal");
    is_exit = 1;
}

/*
   DO NOT CHANGE THE FOLLOWING FUNCTION
   */
int main(int argc, char *argv[])
{

    virConnectPtr conn;

    if (argc != 2)
    {
        printf("Incorrect number of arguments\n");
        return 0;
    }

    // Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
    int interval = atoi(argv[1]);

    conn = virConnectOpen("qemu:///system");
    if (conn == NULL)
    {
        fprintf(stderr, "Failed to open connection\n");
        return 1;
    }

    // Get the total number of pCpus in the host
    signal(SIGINT, signal_callback_handler);

    while (!is_exit)
    // Run the CpuScheduler function that checks the CPU Usage and sets the pin at an interval of "interval" seconds
    {
        CPUScheduler(conn, interval);
        sleep(interval);
    }

    // Closing the connection
    virConnectClose(conn);
    return 0;
}

/* COMPLETE THE IMPLEMENTATION */
void collectHostStats(virConnectPtr conn)
{
    virNodeInfoPtr vNodeInfo;

    // collect information about host cpus
    vNodeInfo = malloc(sizeof(virNodeInfo));
    if (virNodeGetInfo(conn, vNodeInfo) < 0)
    {
        fprintf(stderr, "Failed to get node info\n");
        return;
    }
    nPcpus = vNodeInfo->cpus;
    scheduler_state->host_ncpus = nPcpus;
}

virVcpuInfoPtr *collectGuestStats(virConnectPtr conn)
{
    int number_domains = virConnectNumOfDomains(conn);
    virDomainPtr *domain = malloc(sizeof(virDomainPtr) * number_domains);
    virConnectListAllDomains(conn, &domain, VIR_CONNECT_LIST_DOMAINS_ACTIVE);

    scheduler_state->ndomains = number_domains;
    virDomainInfoPtr domain_info = malloc(sizeof(virDomainInfo));
    virVcpuInfoPtr *res = malloc(sizeof(virVcpuInfoPtr) * number_domains);

    for (int i = 0; i < number_domains; i++)
    {
        virDomainGetInfo(domain[i], domain_info);
        scheduler_state->domains[i].nr_vcpus = domain_info->nrVirtCpu;
        scheduler_state->domains[i].domain = domain[i];
        strcpy(scheduler_state->domains[i].name, virDomainGetName(domain[i]));
        int nVcpus = domain_info->nrVirtCpu;
        res[i] = malloc(sizeof(virVcpuInfo) * nVcpus);
        virDomainGetVcpus(domain[i], res[i], nVcpus, NULL, 0);
    }

    free(domain_info);
    free(domain);

    return res;
}

void accumulate_vm_usage_by_pcpu(virConnectPtr conn,
                                 virVcpuInfoPtr *v1_per_domain,
                                 virVcpuInfoPtr *v2_per_domain)
{
    if (!scheduler_state)
        return; /* must be initialized elsewhere */

    long long total_delta = 0LL;
    for (int p = 0; p < nPcpus; ++p)
    {
        scheduler_state->pcpus[p].pcpu = p;
        scheduler_state->pcpus[p].vm_ns = 0ULL;
        scheduler_state->pcpus[p].total_vcpus = 0;
    }
    scheduler_state->total_vcpus = 0;

    /* For each canonical domain in scheduler_state, find its index in each snapshot by name */
    for (int d = 0; d < scheduler_state->ndomains; ++d)
    {
        const char *canon_name = scheduler_state->domains[d].name;
        if (!canon_name || canon_name[0] == '\0')
            continue;

        /* use the stored nr_vcpus from scheduler_state (you told me it is available) */
        int nVcpus = scheduler_state->domains[d].nr_vcpus;
        if (nVcpus <= 0)
            continue;

        /* iterate guest vcpus for this domain */
        for (int i = 0; i < nVcpus; ++i)
        {
            unsigned int vnum = v1_per_domain[d][i].number;

            /* find matching vcpu in snapshot2 for same domain */
            for (int j = 0; j < nVcpus; ++j)
            {
                if (v2_per_domain[d][j].number == vnum)
                {
                    unsigned long long t1 = (unsigned long long)v1_per_domain[d][i].cpuTime;
                    unsigned long long t2 = (unsigned long long)v2_per_domain[d][j].cpuTime;
                    unsigned long long delta = (t2 > t1) ? (t2 - t1) : 0ULL;
                    total_delta += (long long)delta;
                    // bookkeep pcpu details
                    int host_pcpu = v2_per_domain[d][j].cpu; /* use after snapshot's host cpu */
                    if (host_pcpu >= 0 && host_pcpu < nPcpus)
                    {
                        scheduler_state->pcpus[host_pcpu].pcpu = host_pcpu;
                        scheduler_state->pcpus[host_pcpu].vm_ns += delta;
                        scheduler_state->pcpus[host_pcpu].vcpus[scheduler_state->pcpus[host_pcpu].total_vcpus++] = (VcpuState){
                            .vcpu_number = vnum,
                            .current_pcpu = host_pcpu,
                            .delta_ns = delta,
                            .domain = scheduler_state->domains[d]};
                    }
                    scheduler_state->vcpus[scheduler_state->total_vcpus++] = (VcpuState){
                        .vcpu_number = vnum,
                        .current_pcpu = host_pcpu,
                        .delta_ns = delta,
                        .domain = scheduler_state->domains[d]};
                    break; /* matched this vcpu */
                }
            }
        }
    }
}

double calculateStandardDeviation()
{
    if (nPcpus <= 0)
        return 0.0;

    unsigned long long interval_ns = 1000000000ULL;
    if (scheduler_state && scheduler_state->interval_seconds > 0)
        interval_ns = (unsigned long long)scheduler_state->interval_seconds * 1000000000ULL;

    double sum = 0.0;
    for (int i = 0; i < nPcpus; ++i)
    {
        double pct = 100.0 * (double)scheduler_state->pcpus[i].vm_ns / (double)interval_ns;
        sum += pct;
    }

    double mean = sum / (double)nPcpus;

    double sqsum = 0.0;
    for (int i = 0; i < nPcpus; ++i)
    {
        double pct = 100.0 * (double)scheduler_state->pcpus[i].vm_ns / (double)interval_ns;
        double diff = pct - mean;
        sqsum += diff * diff;
    }

    double variance = sqsum / (double)nPcpus;
    return sqrt(variance);
}

static int compare_vcpu_delta_desc(const void *a, const void *b)
{
    const VcpuState *A = (const VcpuState *)a;
    const VcpuState *B = (const VcpuState *)b;

    if (A->delta_ns > B->delta_ns)
        return -1; /* A heavier -> comes first (desc) */
    if (A->delta_ns < B->delta_ns)
        return 1;
    return 0;
}

static int compare_pcpu_vmns_asc(const void *a, const void *b)
{
    const PcpuState *A = (const PcpuState *)a;
    const PcpuState *B = (const PcpuState *)b;

    if (A->vm_ns < B->vm_ns)
        return -1; /* A less used -> comes first (asc) */
    if (A->vm_ns > B->vm_ns)
        return 1;
    return 0;
}

unsigned char *build_cpumap(int targetCpu, int ncpus)
{
    int size = (ncpus + 7) / 8;
    unsigned char *cpumap = calloc(size, sizeof(unsigned char));
    int byte = targetCpu / 8;
    int bit = targetCpu % 8;
    cpumap[byte] |= (1 << bit);
    return cpumap;
}

void swap(VcpuState *a, VcpuState *b)
{
    VcpuState temp = *a;
    *a = *b;
    *b = temp;
}

void load_balance(void)
{
    if (!scheduler_state)
        return;
    if (nPcpus <= 0 || scheduler_state->total_vcpus <= 0)
        return;

    int max_passes = 10;
    double target_sd = 5.0;
    double prev_sd = calculateStandardDeviation();

    for (int pass = 0; pass < max_passes; ++pass)
    {
        /* clear pCPU bookkeeping before a full replan */
        for (int p = 0; p < scheduler_state->host_ncpus; ++p)
        {
            scheduler_state->pcpus[p].vm_ns = 0ULL;
            scheduler_state->pcpus[p].total_vcpus = 0;
        }

        /* sort vcpus heavy->light */
        qsort(scheduler_state->vcpus, scheduler_state->total_vcpus,
              sizeof(VcpuState), compare_vcpu_delta_desc);

        /* assign each vcpu to least-used pCPU */
        for (int i = 0; i < scheduler_state->total_vcpus; ++i)
        {
            VcpuState *v = &scheduler_state->vcpus[i];
            if (v->delta_ns == 0)
            {
                /* still record on its current pcpu if valid */
                if (v->current_pcpu >= 0 && v->current_pcpu < scheduler_state->host_ncpus)
                {
                    PcpuState *pp = &scheduler_state->pcpus[v->current_pcpu];
                    pp->vcpus[pp->total_vcpus++] = *v;
                    pp->vm_ns += v->delta_ns;
                }
                continue;
            }

            /* find least-used pCPU by sorting pcpus ascending */
            qsort(scheduler_state->pcpus, scheduler_state->host_ncpus,
                  sizeof(PcpuState), compare_pcpu_vmns_asc);
            PcpuState *target = &scheduler_state->pcpus[0];

            unsigned char *map = build_cpumap(target->pcpu, nPcpus);
            if (!map)
                continue;

            if (virDomainPinVcpu(v->domain.domain, v->vcpu_number, map, (nPcpus + 7) / 8) < 0)
            {
                free(map);
                continue;
            }

            /* update bookkeeping: record vcpu in target pCPU */
            target->vcpus[target->total_vcpus++] = *v;
            target->vm_ns += v->delta_ns;
            v->current_pcpu = target->pcpu;

            free(map);
        }

        /* compute new SD for this pass */
        double sd = calculateStandardDeviation();
        if (sd <= target_sd)
            return;
        if (sd >= prev_sd - 1e-9)
            break; /* no meaningful improvement -> stop */
        prev_sd = sd;
    }
}

void reset_scheduler()
{
    scheduler_state->total_vcpus = 0;
    scheduler_state->interval_seconds = 0;
    memset(scheduler_state->domains, 0, sizeof(scheduler_state->domains));
    memset(scheduler_state->vcpus, 0, sizeof(scheduler_state->vcpus));
    memset(scheduler_state->pcpus, 0, sizeof(scheduler_state->pcpus));
}

void CPUScheduler(virConnectPtr conn, int interval)
{
    virVcpuInfoPtr *vcpu_info1;
    virVcpuInfoPtr *vcpu_info2;

    scheduler_state = malloc(sizeof(SchedulerState));
    reset_scheduler();
    scheduler_state->interval_seconds = interval;
    collectHostStats(conn);
    vcpu_info1 = collectGuestStats(conn);
    sleep(interval);
    vcpu_info2 = collectGuestStats(conn);
    accumulate_vm_usage_by_pcpu(conn, vcpu_info1, vcpu_info2);
    printf("Standard Deviation before load balancing= %f\n", calculateStandardDeviation());
    if (calculateStandardDeviation() > 5)
    {
        printf("standard deviation= %f\n", calculateStandardDeviation());
        load_balance();
    }
    free(scheduler_state);
}
