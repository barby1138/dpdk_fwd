# dpdk_fwd

DPDK packet forwarder / processor.

## Features

### Functionality
- Drop packets if they are non-IP packets.
- Modify the packet headers (e.g., change the destination MAC address).
- Forward valid packets to the appropriate interface.

### Periodic real-time traffic statistics
- Per-interface received, forwarded, and dropped packets.
- Configurable interval.

### Bonus Features
- Add a basic filtering mechanism to drop packets from certain IP addresses.
#### TODO
- Implement a simple rate limiter to control the packet forwarding rate.
- Use multiple cores for improved performance (e.g., separate RX and TX threads).

## Performance
- Use multiple RX/TX queues to enable parallel processing by multiple worker threads.
- Optimize memory access patterns for cache efficiency.

## Dependencies
- DPDK (tested with 21.11) and DPDK dependencies
- `gcc` to build

## Build
```
cd fwd
make
```

## Run
```
./fwd [-q num_queues] [-s stats_interval_sec] -- <EAL args>
```

## Expected output
```
./fwd -q 4 -s 2 -- -l 0-4 -n 4


EAL: Detected CPU lcores: 20
EAL: Detected NUMA nodes: 1
EAL: Detected shared linkage of DPDK
EAL: Multi-process socket /var/run/dpdk/rte/mp_socket
EAL: Selected IOVA mode 'VA'
EAL: No available 2048 kB hugepages reported
EAL: VFIO support initialized
EAL: Using IOMMU type 1 (Type 1)
EAL: Ignore mapping IO port bar(1)
EAL: Ignore mapping IO port bar(4)
EAL: Probe PCI driver: net_i40e (8086:15ff) device: 0000:b1:00.0 (socket 0)
EAL: Ignore mapping IO port bar(1)
EAL: Ignore mapping IO port bar(4)
EAL: Probe PCI driver: net_i40e (8086:15ff) device: 0000:b1:00.3 (socket 0)
TELEMETRY: No legacy callbacks, legacy socket not created
Using 4 RX/TX queues
Port 0 MAC: 6c ee aa ff 7a 60
Port 1 MAC: 6c ee aa ff 7a 63
Starting core 1 (port 0, queue 0)
Starting core 2 (port 0, queue 1)
Starting core 3 (port 0, queue 2)
Starting core 4 (port 0, queue 3)
=== Traffic Stats (Interval: 1s) ===
Port 0 | RX:          0 pkts | TX:          0 pkts | Drop:        0 | RX:    0.00 Mbps | TX:    0.00 Mbps
Port 1 | RX:          0 pkts | TX:          0 pkts | Drop:        0 | RX:    0.00 Mbps | TX:    0.00 Mbps
=====================================

=== Traffic Stats (Interval: 1s) ===
Port 0 | RX:          0 pkts | TX:    1219128 pkts | Drop:        0 | RX:    0.00 Mbps | TX: 4258.26 Mbps
Port 1 | RX:    1219128 pkts | TX:          0 pkts | Drop:        0 | RX: 4258.24 Mbps | TX:    0.00 Mbps
=====================================

=== Traffic Stats (Interval: 1s) ===
Port 0 | RX:          0 pkts | TX:    1700260 pkts | Drop:        0 | RX:    0.00 Mbps | TX: 4257.06 Mbps
Port 1 | RX:    1700264 pkts | TX:          0 pkts | Drop:        0 | RX: 4257.09 Mbps | TX:    0.00 Mbps
=====================================

=== Traffic Stats (Interval: 1s) ===
Port 0 | RX:          0 pkts | TX:    2180775 pkts | Drop:        0 | RX:    0.00 Mbps | TX: 4251.60 Mbps
Port 1 | RX:    2181400 pkts | TX:          0 pkts | Drop:      621 | RX: 4257.09 Mbps | TX:    0.00 Mbps
=====================================

=== Traffic Stats (Interval: 1s) ===
Port 0 | RX:          0 pkts | TX:    2663787 pkts | Drop:        0 | RX:    0.00 Mbps | TX: 4273.69 Mbps
Port 1 | RX:    2664412 pkts | TX:          0 pkts | Drop:      621 | RX: 4273.69 Mbps | TX:    0.00 Mbps
=====================================

=== Traffic Stats (Interval: 1s) ===
Port 0 | RX:          0 pkts | TX:    3144923 pkts | Drop:        0 | RX:    0.00 Mbps | TX: 4257.09 Mbps
Port 1 | RX:    3145544 pkts | TX:          0 pkts | Drop:      621 | RX: 4257.06 Mbps | TX:    0.00 Mbps
```
