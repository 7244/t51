static void run_thread_common_get_macs(uint8_t *src_mac_addr, uint8_t *dst_mac_addr){
  if(pile.difacename != NULL){
    get_ifname_src_mac_cstr(pile.difacename, src_mac_addr);
  }
  else if(pile.pci_name != NULL){
    if(0){}
    #ifdef set_use_dpdk
      else if(pile.use_dpdk){
        struct rte_ether_addr rte_ether_addr;
        rte_eth_macaddr_get(pile.dpdk.i_dpdk_interface, &rte_ether_addr);
        __builtin_memcpy(src_mac_addr, rte_ether_addr.addr_bytes, 6);
      }
    #endif
    else{
      /* TOOD */
      _abort();
    }
  }

  if(pile.force_dst_mac){
    __builtin_memcpy(dst_mac_addr, pile.dst_mac, sizeof(pile.dst_mac));
  }
  else if(pile.difacename != NULL){
    get_ifname_dst_mac_cstr(pile.difacename, dst_mac_addr);
  }
  else if(pile.dst_mac_from_ifname != NULL){
    get_ifname_dst_mac_cstr(pile.dst_mac_from_ifname, dst_mac_addr);
  }
  else if(pile.pci_name != NULL){
    /* TOOD convert pci to interface then get it */
    _abort();
  }
  else{
    _abort();
  }
}
#define run_thread_common_get_macs() \
  uint8_t src_mac_addr[6]; \
  uint8_t dst_mac_addr[6]; \
  run_thread_common_get_macs(src_mac_addr, dst_mac_addr);

#define run_thread_common_set_packet_initial(data, data_size) \
  NET_machdr_t *machdr = (NET_machdr_t *)(data); \
  NET_ipv4hdr_t *ipv4hdr = (NET_ipv4hdr_t *)&machdr[1]; \
  NET_udphdr_t *udphdr = (NET_udphdr_t *)&ipv4hdr[1]; \
  uint8_t *payload = (uint8_t *)&udphdr[1]; \
  \
  if((uintptr_t)payload - (uintptr_t)ipv4hdr + pile.payload_size > (data_size)){ \
    _abort(); \
  } \
  \
  __builtin_memcpy(machdr->src, src_mac_addr, sizeof(machdr->src)); \
  __builtin_memcpy(machdr->dst, dst_mac_addr, sizeof(machdr->dst)); \
  \
  machdr->prot = 0x0008; \
  \
  for(uint32_t i = 0; i < pile.payload_size; i++){ \
    payload[i] = 0; \
  } \
  \
  ipv4hdr->ihl = 5; \
  ipv4hdr->version = 4; \
  ipv4hdr->tos = 0; \
  ipv4hdr->tot_len = NET_hton16(sizeof(NET_ipv4hdr_t) + sizeof(NET_udphdr_t) + pile.payload_size); \
  ipv4hdr->id = NET_hton32(54321); \
  ipv4hdr->frag_off = 0; \
  ipv4hdr->ttl = 255; \
  ipv4hdr->protocol = NET_IPPROTO_UDP; \
  ipv4hdr->check = 0; \
  if(pile.source.prefix != 32){ \
    ipv4hdr->saddr = NET_hton32(0); \
  } \
  else{ \
    ipv4hdr->saddr = NET_hton32(pile.source.ip); \
  } \
  if(pile.target_addr.prefix != 32){ \
    ipv4hdr->daddr = NET_hton32(0); \
  } \
  else{ \
    ipv4hdr->daddr = NET_hton32(pile.target_addr.ip); \
  } \
  \
  uint32_t ipv4check_pre = checksum_pre(ipv4hdr, sizeof(*ipv4hdr)); \
  \
  if(pile.rand_sport){ \
    udphdr->source = NET_hton16(0); \
  } \
  else{ \
    udphdr->source = NET_hton16(pile.sport); \
  } \
  if(pile.rand_dport){ \
    udphdr->dest = NET_hton16(0); \
  } \
  else{ \
    udphdr->dest = NET_hton16(pile.dport); \
  } \
  udphdr->len = NET_hton16(sizeof(*udphdr) + pile.payload_size); \
  udphdr->check = 0; \
  \
  uint32_t udpcheck_pre = 0; \
  udpcheck_pre += checksum_pre(&ipv4hdr->saddr, sizeof(ipv4hdr->saddr)); \
  udpcheck_pre += checksum_pre(&ipv4hdr->daddr, sizeof(ipv4hdr->daddr)); \
  udpcheck_pre += NET_hton16(NET_IPPROTO_UDP); \
  udpcheck_pre += udphdr->len; \
  udpcheck_pre += checksum_pre(udphdr, sizeof(*udphdr) + pile.payload_size); \
  \
  ipv4hdr->saddr = NET_hton32(pile.source.ip); \
  ipv4hdr->daddr = NET_hton32(pile.target_addr.ip);
