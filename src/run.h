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

FUNC void run_thread(pile_t *pile){
  NET_socket_t s;
  sint32_t err = NET_socket2(NET_AF_INET, NET_SOCK_RAW, NET_IPPROTO_RAW, &s);
  if(err){
    puts_literal("NET_socket2 fail. need root probably\n");
    _exit(1);
  }

  {
    NET_addr4port_t difaceaddr4port;
    difaceaddr4port.ip = pile->difaceip.ip;
    difaceaddr4port.port = 0;

    if(NET_connect(&s, &difaceaddr4port)){
      _abort();
    }
  }

  uint8_t data[0x800];

  NET_ipv4hdr_t *ipv4hdr = (NET_ipv4hdr_t *)data;
  NET_udphdr_t *udphdr = (NET_udphdr_t *)&ipv4hdr[1];
  uint8_t *payload = (uint8_t *)&udphdr[1];

  if((uintptr_t)payload - (uintptr_t)ipv4hdr + pile->payload_size > sizeof(data)){
    _abort();
  }

  for(uint32_t i = 0; i < pile->payload_size; i++){
    payload[i] = 0;
  }

  ipv4hdr->ihl = 5;
  ipv4hdr->version = 4;
  ipv4hdr->tos = 0;
  ipv4hdr->tot_len = sizeof(NET_ipv4hdr_t) + sizeof(NET_udphdr_t) + pile->payload_size;
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

  for(uint64_t ithreshold = pile->threshold; ithreshold--;){
  
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


    for(uint64_t iprepeat = pile->prepeat; iprepeat--;){

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

      IO_ssize_t rsize = IO_write(&s.fd, data, ipv4hdr->tot_len);
      if((IO_size_t)rsize > (IO_size_t)-4096){
        _abort();
      }
    }
  }
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
