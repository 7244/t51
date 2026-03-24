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

FUNC void get_ifname_src_mac_cstr(const void *ifname, uint8_t *mac){
  if(NET_GetSRCMACFromIFName_cstr((const char *)ifname, mac)){
    _abort();
  }
}

FUNC void get_ifname_dst_mac_cstr(const void *ifname, uint8_t *mac) {
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

#include "_run_thread_common.h"

#include "_run_thread_PACKET.h"
#ifdef set_use_dpdk
  #include "_run_thread_dpdk.h"
  #include <WITCH/STR/psu.h>
  #include <WITCH/STR/uto.h>
#endif

FUNC void run_entry(void *p_0){
  (void)p_0;

  #ifdef set_use_dpdk
    {
      do{
        /* need to check 2mb huge pages */
        IO_fd_t fd;
        sint32_t r = IO_open("/sys/devices/system/node/node0/hugepages/hugepages-2048kB/nr_hugepages", O_RDWR, &fd);
        if(r){
          /* TOOD give error print */
          break;
        }

        uint8_t buf[64];
        IO_ssize_t ssize = IO_read(&fd, buf, sizeof(buf));
        if(ssize <= 0){
          _abort();
        }

        uintptr_t stri = 0;
        uintptr_t page_count = STR_psu_iguess(buf, &stri);
        if(page_count == 0){
          if(IO_lseek(&fd, 0, SEEK_SET)){
            _abort();
          }

          uint8_t *ptr = buf;
          uintptr_t ptr_size;
          if(STR_uto(64, 10, &ptr, &ptr_size)){
            _abort();
          }

          if((uintptr_t)IO_write(&fd, ptr, ptr_size) != ptr_size){
            _abort();
          }
        }

        IO_close(&fd);
      }while(0);

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

      /* TODO put this check to somewhere else */
      if(pile.pci_name != NULL && pile.difacename != NULL){
        _abort();
      }

      uint8_t _wanted_pci_name[4 + 1 + 2 + 1 + 2 + 1 + 1 + 1];
      const uint8_t *wanted_pci_name;
      if(pile.pci_name != NULL){
        wanted_pci_name = pile.pci_name;
      }
      else if(pile.difacename != NULL){
        if(NET_GetPCIStringFromIFName_cstr((const char *)pile.difacename, _wanted_pci_name)){
          _abort();
        }
        _wanted_pci_name[sizeof(_wanted_pci_name) - 1] = 0;
        wanted_pci_name = _wanted_pci_name;
      }
      else{
        /* TODO */
        _abort();
      }


      uint16_t i_dpdk_interface;
      while(1){
        uint16_t dpdk_interface_count = rte_eth_dev_count_avail();
        for(i_dpdk_interface = 0; i_dpdk_interface < dpdk_interface_count; i_dpdk_interface++){
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
        if(i_dpdk_interface != dpdk_interface_count){
          break;
        }
        if(pile.difacename != NULL){
          puts_literal("[WARNING] your ifname doesnt support dpdk\n");
        }
        else{
          puts_literal("[WARNING] your pci name doesnt support dpdk\n");
        }
        /* TODO QUESTION try sriov */
        if(pile.difacename != NULL){
          puts_literal("[QUESTION] do you want to change driver of it? (bool): ");
          /* TODO flush get input */
          puts_literal("[QUESTION] this gonna make your ifname unusable. are you sure? (bool): ");
          /* TODO flush get input */
        }
        else{
          puts_literal("[QUESTION] do you want to change driver of it? (bool): ");
          /* TODO flush get input */
        }

        gt_cant_solve_interface_search:;

        puts_literal("[ERROR] cant solve dpdk interface search. aborting.\n");

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

      pile.dpdk.mempool = rte_pktmbuf_pool_create(
        "mempool",
        8191,
        256,
        0,
        RTE_MBUF_DEFAULT_BUF_SIZE,
        rte_socket_id()
      );
      if(pile.dpdk.mempool == NULL){
        _abort();
      }

      err = rte_eth_rx_queue_setup(i_dpdk_interface, 0, 512, rte_eth_dev_socket_id(i_dpdk_interface), NULL, pile.dpdk.mempool);
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

      pile.dpdk.given_worker_queues = 0;
      __flush_compiler_variable_rw(pile.dpdk.given_worker_queues);

      uint32_t given_threads = 0;
      uint32_t lcore_id;
      RTE_LCORE_FOREACH_WORKER(lcore_id){
        if(given_threads < wanted_thread_count){
          err = rte_eal_remote_launch(_run_thread_dpdk, NULL, lcore_id);
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
