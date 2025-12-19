# VM CPU Scheduler and Memory Coordinator

## Architecture Overview

This project consists of two main C programs for managing virtual machine resources:

### 1. vCPU Scheduler

- Periodically monitors guest VM CPU usage and dynamically assigns vCPUs to pCPUs to balance workload.
- Uses libvirt APIs to interact with the hypervisor, collect statistics, and update vCPU pinning.
- Designed to work with any number of VMs and CPUs, aiming for balanced and stable CPU utilization.

### 2. Memory Coordinator

- Periodically monitors guest VM memory usage and dynamically adjusts VM memory allocation using the balloon driver.
- Ensures both host and VMs retain sufficient free memory, releasing or allocating memory gradually and safely.
- Uses libvirt APIs to collect memory statistics and update allocations.

Both programs run at user-specified intervals and connect to the hypervisor at `qemu:///system`.

## Directory Structure

- `cpu/src/`: vCPU scheduler source code and Makefile.
- `memory/src/`: Memory coordinator source code and Makefile.
- `cpu/test/` and `memory/test/`: Python scripts, shell scripts, and test cases for automated testing and monitoring.
- `ahmed_hamouda_p1/`: Example submission structure, with separate folders for CPU and memory components, each including source code, Makefile, Readme, and log files.

## Testing Framework

- **Automated Tests**:

  - Each component is tested using three provided test cases simulating different resource usage scenarios.
  - Python scripts (`monitor.py`, `runtest1.py`, etc.) launch workloads and monitor resource usage.
  - The `script` command records terminal sessions and generates log files for each test case.

- **Test Validation**:
  - vCPU scheduler tests check for balanced and stable CPU usage across pCPUs.
  - Memory coordinator tests verify correct memory allocation and safe operation (no VM or host crashes).
  - Log files are generated for each test and included in the submission.

## Summary

This architecture ensures modularity, testability, and safe resource management for virtual machines using industry-standard APIs and practices.
