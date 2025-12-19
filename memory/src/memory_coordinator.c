#include <stdio.h>
#include <stdlib.h>
#include <libvirt/libvirt.h>
#include <math.h>
#include <string.h>
#include <unistd.h>
#include <limits.h>
#include <signal.h>
#include "math.h"
#include "memory_info.h"
#define MIN(a, b) ((a) < (b) ? a : b)
#define MAX(a, b) ((a) > (b) ? a : b)

int is_exit = 0; // DO NOT MODIFY THE VARIABLE
int changeDetected = 0;

void MemoryScheduler(virConnectPtr conn, int interval);
static guest_memory_info_t guest_memory_info[100];
int number_domains = 0;
virDomainPtr *domains;
virConnectPtr conn;

void zero_memory_info();
/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
void signal_callback_handler()
{
	// printf("Caught Signal");
	is_exit = 1;
}

/*
DO NOT CHANGE THE FOLLOWING FUNCTION
*/
int main(int argc, char *argv[])
{
	if (argc != 2)
	{
		// printf("Incorrect number of arguments\n");
		return 0;
	}

	// Gets the interval passes as a command line argument and sets it as the STATS_PERIOD for collection of balloon memory statistics of the domains
	int interval = atoi(argv[1]);

	conn = virConnectOpen("qemu:///system");
	if (conn == NULL)
	{
		// printf(stderr, "Failed to open connection\n");
		return 1;
	}

	signal(SIGINT, signal_callback_handler);
	number_domains = virConnectNumOfDomains(conn);
	domains = malloc(number_domains * sizeof(virDomainPtr));
	virConnectListAllDomains(conn, &domains, VIR_CONNECT_LIST_DOMAINS_ACTIVE);
	zero_memory_info();

	while (!is_exit)
	{
		// Calls the MemoryScheduler function after every 'interval' seconds
		MemoryScheduler(conn, interval);
		sleep(interval);
	}

	// Close the connection
	free(domains);
	virConnectClose(conn);
	return 0;
}

/*
COMPLETE THE IMPLEMENTATION
*/

int getDomainIdx(char *domain_name)
{
	for (int i = 0; i < 100; i++)
	{
		if (strcmp(guest_memory_info[i].domainName, domain_name) == 0)
		{
			return i;
		}
	}
	return -1;
}

void zero_memory_info()
{
	for (int i = 0; i < 100; i++)
	{
		memset(&guest_memory_info[i], 0, sizeof(guest_memory_info_t));
	}
}

void collect_guest_memory_info(virDomainPtr domain, int idx, int slot)
{
	int nr_stats = 8;
	virDomainMemoryStatPtr stats = malloc(sizeof(virDomainMemoryStatStruct) * nr_stats);
	if (!stats)
		return;

	virDomainMemoryStats(domain, stats, nr_stats, 0);
	const char *name = (char *)virDomainGetName(domain);
	strncpy(guest_memory_info[idx].domainName, name, sizeof(guest_memory_info[idx].domainName) - 1);
	guest_memory_info[idx].domainName[sizeof(guest_memory_info[idx].domainName) - 1] = '\0';

	for (int i = 0; i < nr_stats; i++)
	{
		switch (stats[i].tag)
		{
		case VIR_DOMAIN_MEMORY_STAT_ACTUAL_BALLOON:
			guest_memory_info[idx].total_memory_assigned[slot] = (double)stats[i].val / 1024.0;
			break;
		case VIR_DOMAIN_MEMORY_STAT_USABLE:
			guest_memory_info[idx].available_memory[slot] = (double)stats[i].val / 1024.0;
			break;
		case VIR_DOMAIN_MEMORY_STAT_UNUSED:
			guest_memory_info[idx].unused_memory[slot] = (double)stats[i].val / 1024.0;
			// printf("Domain %s: Unused memory: %.2f MB\n", guest_memory_info[idx].domainName, guest_memory_info[idx].unused_memory[slot]);
			break;
		default:
			break;
		}
		// printf("Domain %s: Unused memory: %.2f MB\n", guest_memory_info[idx].domainName, guest_memory_info[idx].unused_memory[slot]);
	}

	free(stats);
}

double getHostMemoryinGB()
{
	return (double)virNodeGetFreeMemory(conn) / (1024.0 * 1024.0 * 1024);
}

void classifyDomain(int i)
{

	if (guest_memory_info[i].change_in_unused_memory - guest_memory_info[i].lastGave <= -20.0)
	{
		printf("Domain %s is needy\n", guest_memory_info[i].domainName);
		printf("Change in unused memory is %.2f MB\n", guest_memory_info[i].change_in_unused_memory);
		changeDetected = 1;
		guest_memory_info[i].state = NEEDY;
		return;
	}
	// printf("Domain %s Change detected is %d\n", guest_memory_info[i].domainName, changeDetected);
	// printf("Domain %s Unused memory is %.2f\n", guest_memory_info[i].domainName, guest_memory_info[i].unused_memory[1]);
	if (changeDetected && guest_memory_info[i].unused_memory[1] > (MINIMUM_MEMORY + 5.0))
	{
		guest_memory_info[i].state = DONOR;
		// printf("About to retrieve memory from %s\n", guest_memory_info[i].domainName);
		guest_memory_info[i].idle = 1;
		virDomainSetMemory(virDomainLookupByName(conn, guest_memory_info[i].domainName), 1024 * (guest_memory_info[i].total_memory_assigned[1] -

																								 MIN((unsigned long)guest_memory_info[i].unused_memory[1] - MINIMUM_MEMORY, MINIMUM_MEMORY)));
		guest_memory_info[i].total_memory_assigned[1] -= MIN((unsigned long)guest_memory_info[i].unused_memory[1] - MINIMUM_MEMORY, MINIMUM_MEMORY);
		guest_memory_info[i].lastGave = -((double)MIN((unsigned long)guest_memory_info[i].unused_memory[1] - MINIMUM_MEMORY, MINIMUM_MEMORY));
		// printf("Just retrieved memory %lu MB from %s\n",
		// MIN((unsigned long)guest_memory_info[i].unused_memory[1] - MINIMUM_MEMORY, MINIMUM_MEMORY), guest_memory_info[i].domainName);
		return;
	}
	guest_memory_info[i].state = STEADY;
	guest_memory_info[i].idle = 1;
}
void distributeMemory()
{
	for (int i = 0; i < number_domains; i++)
	{
		if ((guest_memory_info[i].state == NEEDY && !guest_memory_info[i].idle) || guest_memory_info[i].unused_memory[1] < 95.0)
		{
			if (getHostMemoryinGB() > 1.0)
			{
				guest_memory_info[i].idle = 0;
				// printf("Total memory assigned to "
				//"%s is %.2f MB\n",
				// guest_memory_info[i].domainName, guest_memory_info[i].total_memory_assigned[1]);
				double new_memory = MIN(MAX_MEMORY, (GivingMemory + (unsigned long)(guest_memory_info[i].total_memory_assigned[1])));
				if (new_memory > MAX_MEMORY)
				{
					guest_memory_info[i].lastGave = 0;
					continue;
				}

				if (virDomainSetMemory(virDomainLookupByName(conn, guest_memory_info[i].domainName), new_memory * 1024) == -1)
				{
					// printf("Failed to set memory for domain %s\n", guest_memory_info[i].domainName);
					guest_memory_info[i].lastGave = 0;
					continue;
				}
				guest_memory_info[i].lastGave = GivingMemory;
				guest_memory_info[i].total_memory_assigned[1] += GivingMemory;
				// printf("Just assigned additional memory %lu MB to %s\n",
				//(MINIMUM_MEMORY + (unsigned long)(guest_memory_info[i].total_memory_assigned[1])), guest_memory_info[i].domainName);
			}
		}
	}
}

void MemoryScheduler(virConnectPtr conn, int interval)
{
	/* ensure domains list is up-to-date (you already populate 'domains' in main) */
	/* do not free domains here; main manages it */

	for (int i = 0; i < number_domains; i++)
	{
		/* if this is first snapshot (slot 0 empty), collect into slot 0 */
		if (guest_memory_info[i].total_memory_assigned[0] == 0.0)
		{
			collect_guest_memory_info(domains[i], i, 0);
			virDomainSetMemoryStatsPeriod(domains[i], interval, 0);

			continue;
		}

		/* shift previous current -> old (copy slot1 -> slot0) */
		guest_memory_info[i].available_memory[0] = guest_memory_info[i].available_memory[1];
		guest_memory_info[i].unused_memory[0] = guest_memory_info[i].unused_memory[1];
		guest_memory_info[i].total_memory_assigned[0] = guest_memory_info[i].total_memory_assigned[1];
		/* collect new snapshot into slot 1 */
		collect_guest_memory_info(domains[i], i, 1);
		guest_memory_info[i].change_in_unused_memory =
			guest_memory_info[i].unused_memory[1] - guest_memory_info[i].unused_memory[0];
		// printf("Domain %s: Change in unused memory: %.2f MB\n", guest_memory_info[i].domainName, guest_memory_info[i].change_in_unused_memory);
		classifyDomain(i);
	}
	distributeMemory();
	sleep(interval);
}