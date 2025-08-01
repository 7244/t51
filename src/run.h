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

FUNC void get_dst_mac(NET_socket_t *sock, const void *ifname, uint32_t dstip, uint8_t *mac) {
  NET_arpreq_t areq = {0};
  _NET_sockaddr_in_t *sin = (_NET_sockaddr_in_t *)&areq.arp_pa;
  sin->sin_family = NET_AF_INET;
  sin->sin_addr = NET_hton32(dstip);
  __builtin_memcpy(areq.arp_dev, ifname, MEM_cstreu(ifname) + 1);

  if(NET_ctl3(sock, NET_SIOCGARP, &areq) < 0) {
    __builtin_memset(mac, 0, 6);
  }
  else{
    __builtin_memcpy(mac, areq.arp_ha.sa_data, 6);
  }
}

FUNC void run_thread(pile_t *pile){
  NET_socket_t s;
  sint32_t err = NET_socket2(NET_AF_PACKET, NET_SOCK_RAW, NET_ETH_P_ALL, &s);
  if(err){
    puts_literal("NET_socket2 fail. need root probably\n");
    _exit(1);
  }

  if(NET_setsockopt(&s, NET_SOL_PACKET, NET_PACKET_VERSION, NET_TPACKET_V2)) {
    _abort();
  }

  if(pile->difacename != NULL){
    NET_ifreq_t ifreq;
    if(MEM_cstreu(pile->difacename) + 1 > sizeof(ifreq.ifr_name)){
      _abort();
    }
    __builtin_memcpy(ifreq.ifr_name, pile->difacename, MEM_cstreu(pile->difacename) + 1);
    if(NET_ctl3(&s, NET_SIOCGIFINDEX, &ifreq)){
      _abort();
    }

    NET_sockaddr_ll_t bind_addr = {
      .sll_family = NET_AF_PACKET,
      .sll_protocol = NET_hton16(NET_ETH_P_ALL),
      .sll_ifindex = ifreq.ifr_ifindex
    };
    if(NET_bind_raw(&s, (struct sockaddr*)&bind_addr, sizeof(bind_addr))) {
      _abort();
    }
  }
  else{
    // implement this
    _abort();

    NET_addr4port_t difaceaddr4port;
    difaceaddr4port.ip = pile->difaceip.ip;
    difaceaddr4port.port = 0;
  
    if(NET_connect(&s, &difaceaddr4port)){
      _abort();
    }
  }

  const uintptr_t frame_size = 2048;

  NET_tpacket_req_t tpacket_req = {
    .tp_frame_size = frame_size,
    .tp_frame_nr = 2048,
    .tp_block_size = 4096,
    .tp_block_nr = (2048 / (4096 / frame_size))
  };
  if(NET_setsockopt_raw(&s, NET_SOL_PACKET, NET_PACKET_TX_RING, &tpacket_req, sizeof(tpacket_req))){
    _abort();
  }

  if(NET_setsockopt(&s, NET_SOL_PACKET, NET_PACKET_QDISC_BYPASS, 1) < 0){
    _abort();
  }

  uintptr_t ring_size = tpacket_req.tp_block_size * tpacket_req.tp_block_nr;
  void *ring = (void *)IO_mmap(NULL, ring_size, PROT_READ|PROT_WRITE, MAP_SHARED, s.fd.fd, 0);
  if((uintptr_t)ring > (uintptr_t)-4096) {
    _abort();
  }

  uint8_t data[0x800];

  NET_machdr_t *machdr = (NET_machdr_t *)data;
  NET_ipv4hdr_t *ipv4hdr = (NET_ipv4hdr_t *)&machdr[1];
  NET_udphdr_t *udphdr = (NET_udphdr_t *)&ipv4hdr[1];
  uint8_t *payload = (uint8_t *)&udphdr[1];

  if((uintptr_t)payload - (uintptr_t)ipv4hdr + pile->payload_size > sizeof(data)){
    _abort();
  }

  get_src_mac(&s, pile->difacename, machdr->src);
  get_dst_mac(&s, pile->difacename, pile->target_addr.ip, machdr->dst);

  machdr->prot = 0x0008;

  for(uint32_t i = 0; i < pile->payload_size; i++){
    payload[i] = 0;
  }

  ipv4hdr->ihl = 5;
  ipv4hdr->version = 4;
  ipv4hdr->tos = 0;
  ipv4hdr->tot_len = NET_hton16(sizeof(NET_ipv4hdr_t) + sizeof(NET_udphdr_t) + pile->payload_size);
  ipv4hdr->id = NET_hton32(54321);
  ipv4hdr->frag_off = 0;
  ipv4hdr->ttl = 255;
  ipv4hdr->protocol = NET_IPPROTO_UDP;
  ipv4hdr->check = 0;
  if(pile->source.prefix != 32){
    ipv4hdr->saddr = NET_hton32(0);
  }
  else{
    ipv4hdr->saddr = NET_hton32(pile->source.ip);
  }
  if(pile->target_addr.prefix != 32){
    ipv4hdr->daddr = NET_hton32(0);
  }
  else{
    ipv4hdr->daddr = NET_hton32(pile->target_addr.ip);
  }

  uint32_t ipv4check_pre = checksum_pre(ipv4hdr, sizeof(*ipv4hdr));

  if(pile->rand_sport){
    udphdr->source = NET_hton16(0);
  }
  else{
    udphdr->source = NET_hton16(pile->sport);
  }
  if(pile->rand_dport){
    udphdr->dest = NET_hton16(0);
  }
  else{
    udphdr->dest = NET_hton16(pile->dport);
  }
  udphdr->len = NET_hton16(sizeof(*udphdr) + pile->payload_size);
  udphdr->check = 0;

  uint32_t udpcheck_pre = 0;
  udpcheck_pre += checksum_pre(&ipv4hdr->saddr, sizeof(ipv4hdr->saddr));
  udpcheck_pre += checksum_pre(&ipv4hdr->daddr, sizeof(ipv4hdr->daddr));
  udpcheck_pre += NET_hton16(NET_IPPROTO_UDP);
  udpcheck_pre += udphdr->len;
  udpcheck_pre += checksum_pre(udphdr, sizeof(*udphdr) + pile->payload_size);

  ipv4hdr->saddr = NET_hton32(pile->source.ip);
  ipv4hdr->daddr = NET_hton32(pile->target_addr.ip);

  uint64_t tpacket_index = (uint64_t)-1;
  while(1){
    for(uintptr_t i = 0; i < tpacket_req.tp_frame_nr / 4; i++){
      tpacket_index++;
      if(tpacket_index == pile->threshold){
        goto gt_threshold_done;
      }

      uint32_t ipv4check_pre_current = ipv4check_pre;
  
      if(pile->source.prefix != 32){
        ipv4hdr->saddr = NET_ntoh32(ipv4hdr->saddr) - pile->source.ip;
        ipv4hdr->saddr += 1;
        if(pile->source.prefix != 0){
          ipv4hdr->saddr &= ((uint32_t)1 << 32 - pile->source.prefix) - 1;
        }
        ipv4hdr->saddr = NET_hton32(ipv4hdr->saddr + pile->source.ip);
        
        ipv4check_pre_current += checksum_pre_single32(ipv4hdr->saddr);
      }
      if(pile->target_addr.prefix != 32){
        ipv4hdr->daddr = NET_ntoh32(ipv4hdr->daddr) - pile->target_addr.ip;
        ipv4hdr->daddr += 1;
        if(pile->target_addr.prefix != 0){
          ipv4hdr->daddr &= ((uint32_t)1 << 32 - pile->target_addr.prefix) - 1;
        }
        ipv4hdr->daddr = NET_hton32(ipv4hdr->daddr + pile->target_addr.ip);
        
        ipv4check_pre_current += checksum_pre_single32(ipv4hdr->daddr);
      }
  
      ipv4hdr->check = checksum_final(ipv4check_pre_current);
  
  
      uint32_t udpcheck_pre_current = udpcheck_pre;
  
      if(pile->source.prefix != 32){
        udpcheck_pre_current += checksum_pre_single32(ipv4hdr->saddr);
      }
      if(pile->target_addr.prefix != 32){
        udpcheck_pre_current += checksum_pre_single32(ipv4hdr->daddr);
      }
      if(pile->rand_sport){
        udphdr->source++;
        udpcheck_pre_current += checksum_pre_single16(udphdr->source);
      }
      if(pile->rand_dport){
        udphdr->dest++;
        udpcheck_pre_current += checksum_pre_single16(udphdr->dest);
      }
  
  
      udphdr->check = checksum_final(udpcheck_pre_current);
  
  
      if(pile->ppspersrcip != (uint64_t)-1){
        while(fast_limiter(
          &pile->rate_limit_ppspersrcip.current,
          &pile->rate_limit_ppspersrcip.last_refill_at,
          1,
          pile->ppspersrcip * ((uint64_t)1 << 32 - pile->source.prefix),
          T_nowi()
        )){
          // TOOD relax
        }
      }

      uint64_t tpacket_mod = (tpacket_index + 1) % tpacket_req.tp_frame_nr;

      NET_tpacket2_hdr_t *tpacket2_hdr = (NET_tpacket2_hdr_t *)((uint8_t *)ring + tpacket_mod * tpacket_req.tp_frame_size);

      gt_tp_status:;
      uint32_t tp_status = __atomic_load_n(&tpacket2_hdr->tp_status, __ATOMIC_SEQ_CST);
      if(tp_status != NET_TP_STATUS_AVAILABLE){
          goto gt_tp_status;
      }

      void *pkt = (void *)((uint8_t *)tpacket2_hdr + NET_TPACKET_ALIGN(sizeof(NET_tpacket2_hdr_t)));

      tpacket2_hdr->tp_len = sizeof(NET_machdr_t) + sizeof(NET_ipv4hdr_t) + sizeof(NET_udphdr_t) + pile->payload_size;

      __builtin_memcpy(pkt, machdr, tpacket2_hdr->tp_len);

      __atomic_store_n(&tpacket2_hdr->tp_status, NET_TP_STATUS_SEND_REQUEST, __ATOMIC_SEQ_CST);
    }

    if(IO_write(&s.fd, NULL, 0) < 0) {
      _abort();
    }
  }

  gt_threshold_done:;

  /* TODO wait for packets to sent */
}

FUNC void run_entry(pile_t *pile){
  while(1){
    uint32_t ct = __atomic_add_fetch(&pile->current_thread, 1, __ATOMIC_SEQ_CST);
    if(ct >= pile->threads){
      run_thread(pile);

      syscall1(__NR_exit, 0);
      __unreachable();
    }
    if(TH_newthread_orphan((void (*)(void*))run_entry, pile) < 0){
      __abort();
    }
  }
}

FUNC void start_thingies(pile_t *pile){
  run_entry(pile);
}
