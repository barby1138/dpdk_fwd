
#include <stdint.h>
#include <inttypes.h>
#include <rte_eal.h>
#include <rte_ethdev.h>
#include <rte_cycles.h>
#include <rte_lcore.h>
#include <rte_mbuf.h>

#include <unistd.h>
#include <time.h>

#define RX_RING_SIZE 1024
#define TX_RING_SIZE 1024

#define NUM_MBUFS 8191
#define MBUF_CACHE_SIZE 250
#define BURST_SIZE 32

static uint16_t num_queues = 1;

#define MAX_LCORES 128
#define MAX_PORTS 2

struct lcore_queue_conf 
{
    uint16_t port_id;
    uint16_t queue_id;
};

static struct lcore_queue_conf lcore_conf[MAX_LCORES];

// Align burst buffers to cache line size (usually 64 bytes)
struct rte_mbuf* rx_bufs[BURST_SIZE] __rte_cache_aligned;
struct rte_mbuf* tx_bufs[BURST_SIZE] __rte_cache_aligned;

struct port_stats 
{
    volatile uint64_t rx_packets;
    volatile uint64_t tx_packets;
    volatile uint64_t rx_bytes;
    volatile uint64_t tx_bytes;
    volatile uint64_t dropped;
};

struct port_stats g_port_stats[MAX_PORTS];

#define MAX_BLOCKED_IPS 8
static uint32_t blocked_ips[MAX_BLOCKED_IPS];
static int num_blocked_ips = 0;

// Add a blocked IP (in dot notation)
void add_blocked_ip(const char* ip_str)
{
    if (num_blocked_ips >= MAX_BLOCKED_IPS) return;
    struct in_addr addr;
    if (inet_aton(ip_str, &addr))
        blocked_ips[num_blocked_ips++] = rte_be_to_cpu_32(addr.s_addr);
}

void* stats_loop(void* arg)
{
    const uint64_t hz = rte_get_timer_hz();
    const int interval_sec = *((int*)arg);
    uint64_t prev_tsc = rte_get_tsc_cycles();

    struct port_stats prev[MAX_PORTS] = {0};

    while (1) 
	{
        uint64_t cur_tsc;
        do 
		{
            cur_tsc = rte_get_tsc_cycles();
        } while ((cur_tsc - prev_tsc) < interval_sec * hz);

        prev_tsc = cur_tsc;

        printf("=== Traffic Stats (Interval: %ds) ===\n", interval_sec);
        for (int i = 0; i < MAX_PORTS; i++) 
		{
            uint64_t rx = g_port_stats[i].rx_packets;
            uint64_t tx = g_port_stats[i].tx_packets;
            uint64_t rx_b = g_port_stats[i].rx_bytes;
            uint64_t tx_b = g_port_stats[i].tx_bytes;
            uint64_t drop = g_port_stats[i].dropped;

            uint64_t drx = rx - prev[i].rx_packets;
            uint64_t dtx = tx - prev[i].tx_packets;
            uint64_t drxb = rx_b - prev[i].rx_bytes;
            uint64_t dtxb = tx_b - prev[i].tx_bytes;

            printf("Port %d | RX: %10lu pkts | TX: %10lu pkts | Drop: %8lu | RX: %7.2f Mbps | TX: %7.2f Mbps\n",
                i, rx, tx, drop,
                (drxb * 8.0) / (interval_sec * 1e6),
                (dtxb * 8.0) / (interval_sec * 1e6));

            prev[i].rx_packets = rx;
            prev[i].tx_packets = tx;
            prev[i].rx_bytes = rx_b;
            prev[i].tx_bytes = tx_b;
        }
        printf("=====================================\n\n");
    }

    return NULL;
}

static inline int
port_init(uint16_t port, struct rte_mempool *mbuf_pool)
{
	struct rte_eth_conf port_conf;
	const uint16_t rx_rings = num_queues;
	const uint16_t tx_rings = num_queues;
	uint16_t nb_rxd = RX_RING_SIZE;
	uint16_t nb_txd = TX_RING_SIZE;
	int retval;
	uint16_t q;
	struct rte_eth_dev_info dev_info;
	struct rte_eth_txconf txconf;


	if (!rte_eth_dev_is_valid_port(port))
		return -1;

	memset(&port_conf, 0, sizeof(struct rte_eth_conf));

	retval = rte_eth_dev_info_get(port, &dev_info);
	if (retval != 0) 
	{
		printf("Error during getting device (port %u) info: %s\n",
				port, strerror(-retval));
		return retval;
	}

	if (dev_info.tx_offload_capa & RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE)
		port_conf.txmode.offloads |=
			RTE_ETH_TX_OFFLOAD_MBUF_FAST_FREE;

	/* Configure the Ethernet device. */
	retval = rte_eth_dev_configure(port, rx_rings, tx_rings, &port_conf);
	if (retval != 0)
		return retval;

	retval = rte_eth_dev_adjust_nb_rx_tx_desc(port, &nb_rxd, &nb_txd);
	if (retval != 0)
		return retval;

	/* Allocate and set up 1 RX queue per Ethernet port. */
	for (q = 0; q < rx_rings; q++) 
	{
		retval = rte_eth_rx_queue_setup(port, q, nb_rxd,
				rte_eth_dev_socket_id(port), NULL, mbuf_pool);
		if (retval < 0)
			return retval;
	}

	txconf = dev_info.default_txconf;
	txconf.offloads = port_conf.txmode.offloads;
	/* Allocate and set up 1 TX queue per Ethernet port. */
	for (q = 0; q < tx_rings; q++) 
	{
		retval = rte_eth_tx_queue_setup(port, q, nb_txd,
				rte_eth_dev_socket_id(port), &txconf);
		if (retval < 0)
			return retval;
	}

	/* Starting Ethernet port. 8< */
	retval = rte_eth_dev_start(port);
	/* >8 End of starting of ethernet port. */
	if (retval < 0)
		return retval;

	/* Display the port MAC address. */
	struct rte_ether_addr addr;
	retval = rte_eth_macaddr_get(port, &addr);
	if (retval != 0)
		return retval;

	printf("Port %u MAC: %02" PRIx8 " %02" PRIx8 " %02" PRIx8
			   " %02" PRIx8 " %02" PRIx8 " %02" PRIx8 "\n",
			port, RTE_ETHER_ADDR_BYTES(&addr));

	/* Enable RX in promiscuous mode for the Ethernet device. */
	retval = rte_eth_promiscuous_enable(port);
	/* End of setting RX port in promiscuous mode. */
	if (retval != 0)
		return retval;

	return 0;
}

static inline int is_blocked_ip(uint32_t src_ip)
{
    for (int i = 0; i < num_blocked_ips; ++i) 
	{
        if (blocked_ips[i] == src_ip)
            return 1;
    }
    return 0;
}

static int
lcore_worker_main(__rte_unused void *arg)
{
    uint16_t lcore_id = rte_lcore_id();
    uint16_t port = lcore_conf[lcore_id].port_id;
    uint16_t queue = lcore_conf[lcore_id].queue_id;

    struct rte_mbuf *bufs[BURST_SIZE];

	if (rte_eth_dev_socket_id(port) >= 0 && rte_eth_dev_socket_id(port) != (int)rte_socket_id())
			printf("WARNING, port %u is on remote NUMA node to "
					"polling thread.\n\tPerformance will "
					"not be optimal.\n", port);

    printf("Starting core %u (port %u, queue %u)\n", lcore_id, port, queue);

    while (1) 
	{
		uint64_t rx_bytes = 0;

        uint16_t nb_rx = rte_eth_rx_burst(port ^ 1, queue, bufs, BURST_SIZE);
        if (nb_rx == 0) 
			continue;

		// stats
		for (int i = 0; i < nb_rx; i++) 
		{
    		rx_bytes += rte_pktmbuf_pkt_len(bufs[i]);
		}
		g_port_stats[port ^ 1].rx_packets += nb_rx;
		g_port_stats[port ^ 1].rx_bytes += rx_bytes;


		for (uint16_t i = 0; i < nb_rx; i++) 
		{
    		struct rte_mbuf *mbuf = bufs[i];
			if(mbuf) 
			{
				rte_prefetch0(rte_pktmbuf_mtod(mbuf, void*));
    			// process mbuf
			}
		}

		uint16_t nb_to_tx = 0;
    	struct rte_mbuf *to_tx_bufs[BURST_SIZE];
    	for (uint16_t i = 0; i < nb_rx; i++) 
		{
        	struct rte_mbuf *mbuf = bufs[i];
        	uint8_t *pkt_data = rte_pktmbuf_mtod(mbuf, uint8_t *);

        	// Parse Ethernet header
        	struct rte_ether_hdr *eth_hdr = (struct rte_ether_hdr *)pkt_data;

        	// Check if packet is IPv4
        	if (rte_be_to_cpu_16(eth_hdr->ether_type) != RTE_ETHER_TYPE_IPV4) 
			{
            	// Drop non-IP packets
            	rte_pktmbuf_free(mbuf);
            	continue;
        	}

        	// Parse IPv4 header (immediately after Ethernet header)
        	struct rte_ipv4_hdr *ip_hdr = (struct rte_ipv4_hdr *)(pkt_data + sizeof(struct rte_ether_hdr));

        	// You can access IP info here, for example:
        	uint32_t src_ip = rte_be_to_cpu_32(ip_hdr->src_addr);
        	uint32_t dst_ip = rte_be_to_cpu_32(ip_hdr->dst_addr);

    		if (is_blocked_ip(src_ip)) 
			{
        		rte_pktmbuf_free(mbuf); // Drop blocked
        		continue;
    		}

        	// Modify destination MAC address to some predefined MAC
        	struct rte_ether_addr new_dst_mac = 
			{
            	.addr_bytes = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55}
        	};
        	rte_ether_addr_copy(&new_dst_mac, &eth_hdr->dst_addr);

        	// Add the packet to tx buffer list for forwarding
        	to_tx_bufs[nb_to_tx++] = mbuf;
    	}

        // TX to other port (you can add port^1 logic)
        uint16_t nb_tx = rte_eth_tx_burst(port, queue, bufs, nb_rx);

		uint64_t tx_bytes = 0;
		for (int i = 0; i < nb_tx; i++) 
		{
    		tx_bytes += rte_pktmbuf_pkt_len(bufs[i]);
		}
		g_port_stats[port].tx_packets += nb_tx;
		g_port_stats[port].tx_bytes += tx_bytes;

        // Free unsent
        if (unlikely(nb_tx < nb_rx)) 
		{
            for (uint16_t i = nb_tx; i < nb_rx; i++)
                rte_pktmbuf_free(bufs[i]);

    		g_port_stats[port ^ 1].dropped += (nb_rx - nb_tx);
        }
    }

    return 0;
}

int parse_args(int argc, char **argv) 
{
    int opt;
    while ((opt = getopt(argc, argv, "q:")) != -1) 
	{
        switch (opt) 
		{
            case 'q':
                num_queues = (uint16_t)atoi(optarg);
                break;
            default:
                printf("Usage: %s [-q num_queues] -- <EAL args>\n", argv[0]);
                exit(EXIT_FAILURE);
        }
    }
    return optind;
}

void usage() 
{
    printf("Usage: fwd [-q num_queues] -- <EAL args>\n");
    exit(EXIT_FAILURE);
}

int
main(int argc, char *argv[])
{
	struct rte_mempool *mbuf_pool;
	unsigned nb_ports;
	uint16_t portid;

	unsigned lcore_id;
	uint16_t qid = 0;

	if (argc < 2)
	{
		usage();
		return 0;
	}

	add_blocked_ip("192.168.1.100");
	add_blocked_ip("10.0.0.1");

	int ret = parse_args(argc, argv); // Handle -q
	argc -= ret;
	argv += ret;

	/* Initializion the Environment Abstraction Layer (EAL). 8< */
	ret = rte_eal_init(argc, argv);
	if (ret < 0)
		rte_exit(EXIT_FAILURE, "Error with EAL initialization\n");
	/* >8 End of initialization the Environment Abstraction Layer (EAL). */

	unsigned max_lcores = rte_lcore_count() - 1;
	if (num_queues > max_lcores) 
	{
		rte_exit(EXIT_FAILURE, "Error: num_queues > max_lcores\n");
	}

	printf("Using %u RX/TX queues\n", num_queues);

	nb_ports = rte_eth_dev_count_avail();
	if (nb_ports != 2)
		rte_exit(EXIT_FAILURE, "Error: number of ports must be 2\n");

	/* Creates a new mempool in memory to hold the mbufs. */

	/* Allocates mempool to hold the mbufs. 8< */
	mbuf_pool = rte_pktmbuf_pool_create("MBUF_POOL", NUM_MBUFS * nb_ports,
		MBUF_CACHE_SIZE, 0, RTE_MBUF_DEFAULT_BUF_SIZE, rte_socket_id());
	/* >8 End of allocating mempool to hold mbuf. */

	if (mbuf_pool == NULL)
		rte_exit(EXIT_FAILURE, "Cannot create mbuf pool\n");

	/* Initializing all ports. 8< */
	RTE_ETH_FOREACH_DEV(portid)
		if (port_init(portid, mbuf_pool) != 0)
			rte_exit(EXIT_FAILURE, "Cannot init port %"PRIu16 "\n", portid);
	/* >8 End of initializing all ports. */

	RTE_LCORE_FOREACH_WORKER(lcore_id) 
	{
    	if (qid >= num_queues)
        	break;
    	lcore_conf[lcore_id].port_id = 0;
    	lcore_conf[lcore_id].queue_id = qid;
    	rte_eal_remote_launch(lcore_worker_main, NULL, lcore_id);
    	qid++;
	}

	int stats_interval = 1; // configurable, e.g. via command-line

	pthread_t stats_thread;
	pthread_create(&stats_thread, NULL, stats_loop, &stats_interval);


	/* clean up the EAL */
	rte_eal_cleanup();

	return 0;
}
