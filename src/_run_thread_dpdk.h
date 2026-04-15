static int _run_thread_dpdk(void *p_0){
  uint16_t i_dpdk_interface = pile.dpdk.i_dpdk_interface;
  struct rte_mempool *mempool = pile.dpdk.mempool;

  uint32_t *worker_stop_value = pile.dpdk.worker_stop_value;

  struct rte_mbuf *mbuf[32];

  run_thread_common_get_macs();

  uint32_t _ipv4check_pre;
  uint32_t _udpcheck_pre;

  for(uint32_t i = 0; i < sizeof(mbuf) / sizeof(mbuf[0]); i++){
    mbuf[i] = rte_pktmbuf_alloc(mempool);
    if(mbuf[i] == NULL){
      _abort();
    }

    uint8_t *data = rte_pktmbuf_mtod(mbuf[i], uint8_t *);

    run_thread_common_set_packet_initial(data, 1024);

    /* i know this assigns same value multiple times */
    /* but its short code and uses less execution memory */
    _ipv4check_pre = ipv4check_pre;
    _udpcheck_pre = udpcheck_pre;
  }

  uint64_t local_packet_bucket_capacity;
  uint64_t local_packet_bucket;
  if(pile.threshold != (uint64_t)-1){
    local_packet_bucket_capacity = pile.dpdk.packet_bucket / 1024;
  }
  local_packet_bucket = local_packet_bucket_capacity;

  uint32_t tx_queue_index = __atomic_fetch_add(&pile.dpdk.given_worker_queues, 1, __ATOMIC_SEQ_CST);

  uint64_t *counter_ptr = (uint64_t *)&pile.dpdk.worker_packet_counters[tx_queue_index * 64];

  while(*worker_stop_value == 0){
    if(
      __atomic_load_n(&pile.dpdk.wanted_thread_count, __ATOMIC_SEQ_CST) ==
      __atomic_load_n(&pile.dpdk.given_worker_queues, __ATOMIC_SEQ_CST)
    ){
      break;
    }
    __processor_relax();
  }

  uint32_t total_iteration = 0;
  uint32_t to = sizeof(mbuf) / sizeof(mbuf[0]);
  while(*worker_stop_value == 0){
    for(uint32_t i = 0; i < to; i++){

      uint8_t *data = rte_pktmbuf_mtod(mbuf[i], uint8_t *);

      run_thread_common_get_packet_ptrs(data);

      uint32_t ipv4check_pre_current = _ipv4check_pre;
      /* TODO doesnt iterate fields */
      ipv4hdr->check = checksum_final(ipv4check_pre_current);

      uint32_t udpcheck_pre_current = _udpcheck_pre;

      /* TODO doesnt check some fields */

      if(pile.rand_sport){
        udphdr->source = total_iteration;
        udpcheck_pre_current += checksum_pre_single16(udphdr->source);
      }
      if(pile.rand_dport){
        udphdr->dest = total_iteration;
        udpcheck_pre_current += checksum_pre_single16(udphdr->dest);
      }

      udphdr->check = checksum_final(udpcheck_pre_current);

      uint32_t final_size = sizeof(NET_machdr_t) + sizeof(NET_ipv4hdr_t) + sizeof(NET_udphdr_t) + pile.payload_size;

      mbuf[i]->data_len = final_size;
      mbuf[i]->pkt_len = final_size;

      total_iteration++;
    }

    to = rte_eth_tx_burst(i_dpdk_interface, tx_queue_index, mbuf, sizeof(mbuf) / sizeof(mbuf[0]));

    if(pile.threshold != (uint64_t)-1){
      if(__builtin_expect(to >= local_packet_bucket, false)){
        uint64_t diff = to - local_packet_bucket;
        local_packet_bucket = local_packet_bucket_capacity - diff;
        sint64_t packet_bucket = __atomic_sub_fetch(&pile.dpdk.packet_bucket, local_packet_bucket_capacity, __ATOMIC_SEQ_CST);
        if(packet_bucket <= 0){
          goto gt_end;
        }
      }
      else{
        local_packet_bucket -= to;
      }
    }

    *counter_ptr += to;
    __flush_compiler_variable_rw(*counter_ptr);
  }

  gt_end:;

  for(uint32_t i = 0; i < sizeof(mbuf) / sizeof(mbuf[0]); i++){
    rte_pktmbuf_free(mbuf[i]);
  }

  __atomic_add_fetch(&pile.dpdk.finished_worker_queues, 1, __ATOMIC_SEQ_CST);

  return 0;
}
