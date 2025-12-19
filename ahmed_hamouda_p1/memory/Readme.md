# Memory Coordinator

## Algorithm

The algorithm for the code is quite simple. I set the stat period to be the interval. Then collect the memory per domain on every interval. I keep track of last two intervals information so that we can detect the change allowing for some room of difference like if the change is lessa than 20 MB I don't consider it as a change unless we start hitting the minimum memory boundary. I set the memory boundary to (120MB) so that if sudden spike happens(relative to the workload we have which is around 50MB/S in tests). The code classifies the domain as NEEDY or DONOR. The NEEDY will keep receiving more memory(150) untill it hits the maximum memory allowed per domain or no more free memory availble from the host. The DONOR will keep giving away memory(100MB) untill it reaches the minimum. Then it becomes STEADY. The classification is done on every interval to account for continuous consumption or relaxation. This also helps if a sudden spike happens to a DONOR/STEADY making it reach its minimum.

## WEIRD BEHAVIOR

- The consumptions stays till round 57 not 33 as indicated by the manual, hence it goes below 100MB no matter what because the limit is set to 2GIB
- The plot for testcase2 draws 140 iterations duplicating the graph of the first 40 iterations for some reason
