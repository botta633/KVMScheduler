# VCPU Scheduler

## Algorithm

On every interval:

- The CPU collect host information about number of PCPUs and bookkeept it
- For every domain, the code collects guest information by getting number of VCPUs and mapping a VCPU to a domain by storing the info in the scheduler state used for later access. Then it sleeps for an interval and collect them again to be able to caclulate recent consumption over a period(interva).
- After collecting information from all guests, the code loops over all stored VCPUs and accumulating their consumption time to their PCPU and store a map between each Pcpu and its VCPUs and vice versa.
- The code calculates the standard deviation among PCPUs and if it is less than 5 it contiunes and the following interval is run
- Otherwise it calls the load_balance() method which is a greedy method that heavily utilizes the limitations of the test envioronment but also can work in many scenarios as will be indicated shortly. The function assumes that no VCPU is assigned to any PCPU and hence zeros out PCPU->consumption(pcpu.vm_ns), then it sorts VCPUs descendengily based on consumption. On every run it picks the PCPU with lowest recorded time pins itself to it and sort PCPUs again. This continuous sorting instead of building a binary heap for example is the utilization of nature of tests mentioned above. This, usually results in SD to be below 5 from the first pass. However, to account for uncertaintiny. I left a loop to do 10 passes from the older algorithm I've implemented. However, it is not needed here. The code will converge from the first pass. This code does rewiring a lot at first. Despite this cold start, it remains so stable and very cache friendly as fewer and fewer VCPUs will be migrated later on.
