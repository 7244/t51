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

      char *rte_argv[] = {
        "exe",
        "--log-level=*:emerg"
      };
      int err = rte_eal_init(sizeof(rte_argv) / sizeof(rte_argv[0]), rte_argv);
      if(err != sizeof(rte_argv) / sizeof(rte_argv[0]) - 1){
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

      uint8_t _wanted_orig_pci_name[4 + 1 + 2 + 1 + 2 + 1 + 1 + 1];
      uint8_t _wanted_pci_name[4 + 1 + 2 + 1 + 2 + 1 + 1 + 1];
      const uint8_t *wanted_orig_pci_name;
      const uint8_t *wanted_pci_name;
      if(pile.pci_name != NULL){
        wanted_orig_pci_name = pile.pci_name;
      }
      else if(pile.difacename != NULL){
        if(NET_GetPCIStringFromIFName_cstr((const char *)pile.difacename, _wanted_orig_pci_name)){
          _abort();
        }
        _wanted_orig_pci_name[sizeof(_wanted_orig_pci_name) - 1] = 0;
        wanted_orig_pci_name = _wanted_orig_pci_name;
      }
      else{
        /* TODO */
        _abort();
      }

      wanted_pci_name = wanted_orig_pci_name;

      bool did_check_existing_sriov = false;
      bool is_sriov_available = false;

      uintptr_t tried_sriov_current = 0;
      uintptr_t tried_sriov_possible = 0;
      uintptr_t dont_question_for_pick_sriov_n = (uintptr_t)-1;

      uint16_t i_dpdk_interface;
      while(1){
        gt_retry_dpdk_interface_search:;

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

        puts_literal("[WARNING] this pci name doesnt support dpdk\n");

        if(did_check_existing_sriov == false){
          do{
            {
              const char bun_top[] = "/sys/bus/pci/devices/";
              const char bun_bottom[] = "/sriov_numvfs";
    
              uint8_t path[sizeof(bun_top) - 1 + 12 + sizeof(bun_bottom)];
    
              uint8_t *p = path;
              _memcpy_cstr_sumret(p, bun_top);
              _memcpy_cstr_sumret(p, (const char *)wanted_orig_pci_name);
              _memcpy_cstr_sumret(p, bun_bottom, +1);
    
              IO_fd_t fd;
              sint32_t open_r = IO_open(path, O_RDONLY, &fd);
              if(open_r != 0){
                break;
              }
              is_sriov_available = true;
    
              uint8_t buf[32];
              IO_ssize_t ssize = IO_read(&fd, buf, sizeof(buf));
              IO_close(&fd);
    
              if(ssize <= 0){
                _abort();
              }
    
              uintptr_t stri = 0;
              tried_sriov_possible = STR_psu_iguess(buf, &stri);
              if(tried_sriov_possible == 0){
                break;
              }
            }
          }while(0);

          did_check_existing_sriov = true;
        }

        gt_iterate_sriov:;
        {
          const char bun_top[] = "/sys/bus/pci/devices/";
          const char bun_bottom[] = "/virtfn";

          /* TOOD instead of 20 use base10 max digit size for uintptr_t */
          uint8_t path[sizeof(bun_top) - 1 + 12 + sizeof(bun_bottom) - 1 + 20 + 1];

          uint8_t *p = path;
          _memcpy_cstr_sumret(p, bun_top);
          _memcpy_cstr_sumret(p, (const char *)wanted_orig_pci_name);
          _memcpy_cstr_sumret(p, bun_bottom);

          for(; tried_sriov_current < tried_sriov_possible; tried_sriov_current++){
            uint8_t utobuf[64];
            uint8_t *utobuf_ptr = utobuf;
            uintptr_t utosize;
            STR_uto64(tried_sriov_current, 10, &utobuf_ptr, &utosize);

            __builtin_memcpy(p, utobuf_ptr, utosize);
            p[utosize] = 0;

            uint8_t buf[PATH_MAX];
            sintptr_t readlink_r = IO_readlink_cstr((const char *)path, buf, sizeof(buf));
            if(readlink_r <= 0){
              _abort();
            }

            uint8_t extracted_pci_name[12];
            if(!STR_ExtractPCIAddressInsideString(buf, readlink_r, extracted_pci_name)){
              _abort();
            }

            if(tried_sriov_current != dont_question_for_pick_sriov_n){
              if(wanted_pci_name == wanted_orig_pci_name){
                puts_literal("[QUESTION] do you want to try use existing sr-iov child\n");
                puts_literal("  parent: ");
                puts_size(wanted_orig_pci_name, 12);
                puts_literal(" child: ");
                puts_size(extracted_pci_name, 12);
                puts_literal("\n");
              }
              else{
                puts_literal("[QUESTION] do you want to try use other sr-iov child\n");
                puts_literal("  parent: ");
                puts_size(wanted_orig_pci_name, 12);
                puts_literal(" child: ");
                puts_size(wanted_pci_name, 12);
                puts_literal(" new child: ");
                puts_size(extracted_pci_name, 12);
                puts_literal("\n");
              }
              puts_literal("  ? (bool): ");
              flush_print();
  
              bool b = utility_get_stdin_bool_repeat();
              if(b == false){
                continue;
              }
            }

            __builtin_memcpy(_wanted_pci_name, extracted_pci_name, sizeof(extracted_pci_name));
            _wanted_pci_name[sizeof(extracted_pci_name)] = 0;
            wanted_pci_name = _wanted_pci_name;

            tried_sriov_current++;

            goto gt_retry_dpdk_interface_search;
          }
        }

        if(is_sriov_available){
          if(wanted_pci_name == wanted_orig_pci_name)do{
            puts_literal("[QUESTION] do you want to create a new sriov child under parent ");
            puts_size(wanted_orig_pci_name, 12);
            puts_literal(" ? (bool): ");
            flush_print();
            bool b = utility_get_stdin_bool_repeat();
            if(b == false){
              is_sriov_available = false;
              break;
            }

            tried_sriov_possible += 1;

            const char bun0[] = "/sys/bus/pci/devices/";
            const char bun1[] = "/sriov_numvfs";
  
            uint8_t path[sizeof(bun0) - 1 + 12 + sizeof(bun1) - 1 + 1];
  
            uint8_t *p = path;
            _memcpy_cstr_sumret(p, bun0);
            _memcpy_cstr_sumret(p, (const char *)wanted_orig_pci_name);
            _memcpy_cstr_sumret(p, bun1, +1);

            IO_QuickExistingFileWriteBase10_uint64_cstr(path, tried_sriov_possible,
              _abort();
            );

            dont_question_for_pick_sriov_n = tried_sriov_possible - 1;

            goto gt_iterate_sriov;
          }while(0);
        }

        puts_literal("[QUESTION] do you want to change driver of selected pci ");
        puts_size(wanted_pci_name, 12);
        puts_literal(" ? (bool): ");
        flush_print();
        bool b = utility_get_stdin_bool_repeat();
        if(b == true){
          {
            sint32_t km = IO_LoadDefaultKernelModule_cstr("uio/uio_pci_generic", "");
            if(km){
              _abort();
            }
          }

          {
            const char bun0[] = "/sys/bus/pci/devices/";
            const char bun1[] = "/driver_override";

            uint8_t path[sizeof(bun0) - 1 + 12 + sizeof(bun1) - 1 + 1];

            uint8_t *p = path;
            _memcpy_cstr_sumret(p, bun0);
            _memcpy_size_sum(p, wanted_pci_name, 12);
            _memcpy_cstr_sumret(p, bun1, +1);

            IO_QuickExistingFileWriteCSTR_cstr(path, "uio_pci_generic",
              _abort();
            );
          }
          {
            const char bun0[] = "/sys/bus/pci/devices/";
            const char bun1[] = "/driver/unbind";

            uint8_t path[sizeof(bun0) - 1 + 12 + sizeof(bun1) - 1 + 1];

            uint8_t *p = path;
            _memcpy_cstr_sumret(p, bun0);
            _memcpy_size_sum(p, wanted_pci_name, 12);
            _memcpy_cstr_sumret(p, bun1, +1);

            IO_QuickExistingFileWriteData_cstr(path, wanted_pci_name, 12,
              _abort();
            );
          }
          {
            IO_QuickExistingFileWriteData_cstr("/sys/bus/pci/drivers/uio_pci_generic/bind", wanted_pci_name, 12,
              _abort();
            );
          }

          goto gt_retry_dpdk_interface_search;
        }

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

      puts_literal("[INFORMATION] started.\n");
      flush_print();

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
