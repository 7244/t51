FUNC uint32_t checksum_pre(void *ptr, uint32_t size){
  uint32_t sum = 0;

  while(size > 1) {
    sum += *(uint16_t *)ptr;
    *(uintptr_t *)&ptr += 2;
    size -= 2;
  }

  if(size == 1) {
    sum += *(uint8_t *)ptr;
  }

  return sum;
}

FUNC uint32_t checksum_pre_single16(uint32_t p){
  return ((uint16_t *)&p)[0];
}
FUNC uint32_t checksum_pre_single32(uint32_t p){
  return ((uint16_t *)&p)[0] + ((uint16_t *)&p)[1];
}

FUNC uint16_t checksum_final(uint32_t pre){
  pre = (pre >> 16) + (pre & 0xffff);
  pre = pre + (pre >> 16);

  return ~(uint16_t)pre;
}

FUNC void get_src_mac(NET_socket_t *sock, const void *ifname, uint8_t *mac){
  NET_ifreq_t ifr;
  __builtin_memcpy(ifr.ifr_name, ifname, MEM_cstreu(ifname) + 1);

  if(NET_ctl3(sock, NET_SIOCGIFHWADDR, &ifr) < 0) {
    _abort();
  }

  __builtin_memcpy(mac, ifr.ifr_hwaddr.sa_data, 6);
}

FUNC void get_dst_mac(NET_socket_t *sock, const void *ifname, uint8_t *mac) {
  sint32_t err;

  if(pile.force_gateway32){
    err = NET_GetMacAddressByGateway32_ifname_cstr(mac, pile.gateway32, ifname);
  }
  else{
    err = NET_GetDefaultRouteMacAddress_ifname_cstr(mac, ifname);
  }

  if(err != 0){
    _abort();
  }
}

#include "_run_thread_PACKET.h"
#include "_run_thread_dpdk.h"

FUNC void run_entry(void *p_0){
  (void)p_0;

  #ifdef set_use_dpdk
    {
      /* argc 0 gives error in some dpdk versions */
      char *rte_argv = "";
      int err = rte_eal_init(1, &rte_argv);
      if(err){
        _abort();
      }

      uint32_t dpdk_thread_count = rte_lcore_count();
      uint32_t wanted_thread_count = pile.threads;
      if(wanted_thread_count > dpdk_thread_count){
        _abort();
      }


      const uint8_t *wanted_pci_name;
      if(pile.pci_name != NULL){
        wanted_pci_name = pile.pci_name;
      }
      else if(pile.difacename != NULL){
        /* TODO */
        _abort();
      }
      else{
        /* TODO */
        _abort();
      }

      uint16_t dpdk_interface_count = rte_eth_dev_count_avail();
      uint16_t i_dpdk_interface = 0;
      for(; i_dpdk_interface < dpdk_interface_count; i_dpdk_interface++){
        uint8_t name_buffer[RTE_ETH_NAME_MAX_LEN];
        err = rte_eth_dev_get_name_by_port(i_dpdk_interface, (char *)name_buffer);
        if(err){
          /* TODO */
          _abort();
        }

        if(!STR_cmp(wanted_pci_name, name_buffer)){
          break;
        }
      }
      if(i_dpdk_interface == dpdk_interface_count){
        /* TODO */
        _abort();
      }

      struct rte_eth_dev_info eth_dev_info;
      err = rte_eth_dev_info_get(i_dpdk_interface, &eth_dev_info);
      if(err){
        _abort();
      }

      if(wanted_thread_count > eth_dev_info.max_tx_queues){
        puts_literal("[WARNING] wanted_thread_count is above eth_dev_info.max_tx_queues. reducing wanted_thread_count to ");
        utility_puts_number(eth_dev_info.max_tx_queues);
        puts_literal(" from ");
        utility_puts_number(wanted_thread_count);
        wanted_thread_count = eth_dev_info.max_tx_queues;
        puts_literal(".\n");
        flush_print();
      }

      uint16_t wanted_mtu = 1500;

      if(eth_dev_info.max_mtu < wanted_mtu){
        _abort();
      }

      struct rte_eth_conf eth_conf = (struct rte_eth_conf){
        .link_speeds = RTE_ETH_LINK_SPEED_AUTONEG,
        .rxmode = (struct rte_eth_rxmode){
          .mq_mode = RTE_ETH_MQ_RX_NONE,
          .mtu = wanted_mtu,
          .max_lro_pkt_size = eth_dev_info.max_lro_pkt_size,
          .offloads = 0
        },
        .txmode = (struct rte_eth_txmode){
          .mq_mode = RTE_ETH_MQ_TX_NONE,
          .offloads = 0
        },
        .lpbk_mode = 0,
        .dcb_capability_en = 0,
        .intr_conf = (struct rte_eth_intr_conf){
          .lsc = 0,
          .rxq = 0,
          .rmv = 0
        }
      };
      err = rte_eth_dev_configure(i_dpdk_interface, 1, wanted_thread_count, &eth_conf);
      if(err){
        _abort();
      }

      struct rte_mempool *mempool = rte_pktmbuf_pool_create(
        "mempool",
        8191,
        256,
        0,
        RTE_MBUF_DEFAULT_BUF_SIZE,
        rte_socket_id()
      );
      if(mempool == NULL){
        _abort();
      }

      err = rte_eth_rx_queue_setup(i_dpdk_interface, 0, 512, rte_eth_dev_socket_id(i_dpdk_interface), NULL, mempool);
      if(err){
        _abort();
      }

      for(uint32_t ith = 0; ith < wanted_thread_count; ith++){
        err = rte_eth_tx_queue_setup(i_dpdk_interface, ith, 512, rte_eth_dev_socket_id(i_dpdk_interface), NULL);
        if(err){
          _abort();
        }
      }

      err = rte_eth_dev_start(i_dpdk_interface);
      if(err){
        _abort();
      }

      #if 0
      err = rte_eth_promiscuous_enable(i_dpdk_interface);
      if(err){
        _abort();
      }
      #endif


      uint32_t given_threads = 0;
      uint32_t lcore_id;
      RTE_LCORE_FOREACH_WORKER(lcore_id){
        if(given_threads < wanted_thread_count){
          err = rte_eal_remote_launch(_run_thread_dpdk, &i_dpdk_interface, lcore_id);
          if(err){
            _abort();
          }
          given_threads++;
        }
      }

      rte_eal_mp_wait_lcore();

      /* TODO */
      _abort();
    }
  #endif

  while(1){
    uint32_t ct = __atomic_add_fetch(&pile.current_thread, 1, __ATOMIC_SEQ_CST);
    if(ct >= pile.threads){
      _run_thread_PACKET();
      syscall1(__NR_exit, 0);
      __unreachable();
    }
    if(TH_newthread_orphan((void (*)(void*))run_entry, NULL) < 0){
      __abort();
    }
  }
}
