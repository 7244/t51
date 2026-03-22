static int _run_thread_dpdk(void *p_0){
  uint16_t i_dpdk_interface = pile.dpdk.i_dpdk_interface;
  struct rte_mempool *mempool = pile.dpdk.mempool;

  unsigned lcore_id = rte_lcore_id();

  struct rte_mbuf *mbuf[32];

  run_thread_common_get_macs();

  while(1){
    for(uint32_t i = 0; i < sizeof(mbuf) / sizeof(mbuf[0]); i++){
      mbuf[i] = rte_pktmbuf_alloc(mempool);
      if(mbuf[i] == NULL){
        _abort();
      }

      uint8_t *data = rte_pktmbuf_mtod(mbuf[i], uint8_t *);

      run_thread_common_set_packet_initial(data, 1024);
      /* TODO doesnt iterate fields */
      ipv4hdr->check = checksum_final(ipv4check_pre);
      udphdr->check = checksum_final(udpcheck_pre);

      uint32_t final_size = sizeof(NET_machdr_t) + sizeof(NET_ipv4hdr_t) + sizeof(NET_udphdr_t) + pile.payload_size;

      mbuf[i]->data_len = final_size;
      mbuf[i]->pkt_len = final_size;
    }

    uint32_t sent = rte_eth_tx_burst(i_dpdk_interface, lcore_id, mbuf, sizeof(mbuf) / sizeof(mbuf[0]));

    for(uint32_t i = sent; i < sizeof(mbuf) / sizeof(mbuf[0]); i++){
      rte_pktmbuf_free(mbuf[i]);
    }
  }

  return 0;
}
