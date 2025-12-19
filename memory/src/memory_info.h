#ifndef MEMORY_INFO_H
#define MEMORY_INFO_H
#include <libvirt/libvirt.h>

#define MINIMUM_MEMORY 120
#define MAX_MEMORY (2048)
#define GivingMemory 150

enum guest_state
{
    STEADY = 0,
    NEEDY = 1,
    DONOR = 2
};

typedef struct guest_memory_info
{
    char domainName[256];
    double available_memory[2];
    double unused_memory[2];
    double total_memory_assigned[2];
    double change_in_unused_memory;
    double lastGave;
    int idle;
    enum guest_state state; // 1 if need
} guest_memory_info_t;

#endif