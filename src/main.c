#define set_program_name "t51"

#ifndef set_Verbose
  #define set_Verbose 0
#endif

#ifndef set_libc
  #if defined(set_use_dpdk)
    #define set_libc 1
  #else
    #define set_libc 0
  #endif
#endif


#define FUNC static

#if !set_libc
  #define __platform_nostdlib
#endif
#define WITCH_PRE_is_not_allowed
#define __WITCH_IO_allow_sigpipe

#include <WITCH/WITCH.h>
#include <WITCH/PR/PR.h>
#include <WITCH/IO/IO.h>
#include <WITCH/T/T.h>

#include "utility.h"

#include <WITCH/NET/NET.h>

#ifdef set_use_dpdk
  #include <rte_eal.h>
  #include <rte_ethdev.h>
  #include <rte_mbuf.h>
  #include <rte_bus.h>
#endif

typedef struct{
  NET_addr4prefix_t target_addr;

  uint32_t current_thread; /* starts with 0 always */
  uint32_t threads;

  uint64_t threshold;
  uint32_t payload_size;
  
  uint64_t prepeat;
  
  uint64_t ppspersrcip;
  struct{
    uint64_t current;
    uint64_t last_refill_at;
  }rate_limit_ppspersrcip;

  bool kernel_bypass;
  #ifdef set_use_dpdk
    bool use_dpdk;
    struct{
      uint16_t i_dpdk_interface;
      struct rte_mempool *mempool;

      /* increases atomically */
      uint32_t given_worker_queues;

      uint8_t *worker_packet_counters;

      uint32_t *worker_stop_value;
    }dpdk;
  #endif

  bool force_gateway32;
  uint32_t gateway32;
  bool force_dst_mac;
  uint8_t dst_mac[6];

  NET_addr4prefix_t source;

  const uint8_t *difacename;
  const uint8_t *dst_mac_from_ifname;
  const uint8_t *pci_name;
  NET_addr4prefix_t difaceip;

  bool rand_sport;
  bool rand_dport;
  uint16_t sport;
  uint16_t dport;

  NET_socket_t s;
}pile_t;
pile_t pile;

#include "run.h"

FUNC void print_small_help(){
  puts_literal(
    "Usage: " set_program_name " [OPTIONS]\n"
    "\n"
    "For more information, try '--help'.\n"
  );
}

FUNC void print_help(){
  puts_literal(
    "Usage: " set_program_name " [OPTIONS]\n"
    "\n"
    "Options:\n"
    "      --threads NUM         how many threads there gonna be  (default 1)\n"
    "      --threshold NUM       Threshold of packets to send     (default 1000)\n"
    "      --flood               Makes threshold infinite\n"
    "      --prepeat NUM         packet repeat amount             (default 1)\n"
    "      --ppspersrcip NUM     pps per srcip                    (default -1)\n"
    "      --kernel_bypass BOOL                                   (default true)\n"
    #ifdef set_use_dpdk
    "      --use_dpdk BOOL                                        (default true)\n"
    #endif
    "  -h, --help                Print help\n"
    "\n"
    "IP Options:\n"
    "  -s, --saddr ADDR                IP source IP address             (default 0.0.0.0/0)\n"
    "      --difaceip ADDR             Destination interface IP address (default target_addr)\n"
    "      --diface NAME               Destination interface name\n"
    "      --pci ADDR                  PCI address to send packets from\n"
    "      --dst_mac_from_ifname NAME  Get destination MAC address from specific interface\n"
    "\n"
    "DCCP/TCP/UDP Options:\n"
    "      --sport NUM  source port                      (default RANDOM)\n"
    "      --dport NUM  destination port                 (default RANDOM)\n"
    "      --psize NUM  payload size                     (default 32)\n"
  );

  _exit(0);
}

FUNC uintptr_t param_func_threads(const uint8_t **arg){
  uintptr_t index = 0;
  pile.threads = STR_psu64_iguess_abort(arg[0], &index);

  return 1;
}

FUNC uintptr_t param_func_threshold(const uint8_t **arg){
  uintptr_t index = 0;
  pile.threshold = STR_psu64_iguess_abort(arg[0], &index);

  return 1;
}

FUNC uintptr_t param_func_prepeat(const uint8_t **arg){
  uintptr_t index = 0;
  pile.prepeat = STR_psu64_iguess_abort(arg[0], &index);

  return 1;
}

FUNC uintptr_t param_func_ppspersrcip(const uint8_t **arg){
  uintptr_t index = 0;
  pile.ppspersrcip = STR_psu64_iguess_abort(arg[0], &index);

  return 1;
}

FUNC uintptr_t param_func_kernel_bypass(const uint8_t **arg){
  pile.kernel_bypass = STR_ParseCStringAsBool_abort(arg[0]);

  return 1;
}

#ifdef set_use_dpdk
  FUNC uintptr_t param_func_use_dpdk(const uint8_t **arg){
    pile.use_dpdk = STR_ParseCStringAsBool_abort(arg[0]);

    return 1;
  }
#endif

FUNC uintptr_t param_func_gateway32(const uint8_t **arg){
  pile.force_gateway32 = true;

  /* TODO NET_ipv4_from_string is not safe function */
  pile.gateway32 = NET_ipv4_from_string(arg[0]);

  return 1;
}

FUNC uintptr_t param_func_dstmac(const uint8_t **arg){
  pile.force_dst_mac = true;

  NET_mac6_from_string(pile.dst_mac, arg[0]);

  return 1;
}

FUNC uintptr_t param_func_port(const uint8_t **arg, bool s_or_d){
  uintptr_t index = 0;
  if(s_or_d){
    pile.rand_sport = 0;
    pile.sport = STR_psu16_iguess_abort(arg[0], &index);
  }
  else{
    pile.rand_dport = 0;
    pile.dport = STR_psu16_iguess_abort(arg[0], &index);
  }
  return 1;
}

FUNC uintptr_t param_func_psize(const uint8_t **arg){
  uintptr_t index = 0;
  pile.payload_size = STR_psu32_iguess_abort(arg[0], &index);

  return 1;
}

FUNC uintptr_t param_func_saddr(const uint8_t **arg){
  if(NET_addr4prefix_from_string(arg[0], &pile.source)){
    _abort();
  }

  return 1;
}

FUNC uintptr_t param_func_diface(const uint8_t **arg){
  pile.difacename = arg[0];

  return 1;
}

FUNC uintptr_t param_func_dst_mac_from_ifname(const uint8_t **arg){
  pile.dst_mac_from_ifname = arg[0];

  return 1;
}

FUNC uintptr_t param_func_pci(const uint8_t **arg){
  pile.pci_name = arg[0];

  return 1;
}

FUNC uintptr_t param_func_difaceip(const uint8_t **arg){
  if(NET_addr4prefix_from_string(arg[0], &pile.difaceip)){
    _abort();
  }

  return 1;
}

#if set_libc
  int main(int _argc, const char **_argv){
    uintptr_t argc = _argc;
    const uint8_t **argv = (const uint8_t **)_argv;
#else
  __attribute__((noreturn))
  FUNC void main(uintptr_t argc, const uint8_t **argv){
#endif

  #include <WITCH/PlatformOpen.h>

  utility_init_print();

  pile.target_addr.ip = 0;
  pile.target_addr.prefix = 33;

  pile.current_thread = 0;
  pile.threads = 1;
  pile.threshold = 1000;
  pile.payload_size = 32;
  pile.prepeat = 1;

  pile.ppspersrcip = (uint64_t)-1;
  pile.rate_limit_ppspersrcip.current = 0;
  pile.rate_limit_ppspersrcip.last_refill_at = T_nowi();

  pile.kernel_bypass = true;
  #ifdef set_use_dpdk
    pile.use_dpdk = true;
  #endif

  pile.force_gateway32 = false;

  pile.force_dst_mac = false;

  pile.source.ip = 0;
  pile.source.prefix = 0;

  pile.difacename = NULL;
  pile.pci_name = NULL;

  pile.difaceip.ip = 0;
  pile.difaceip.prefix = 33;

  pile.rand_sport = 1;
  pile.rand_dport = 1;

  for(uintptr_t iarg = 1; iarg < argc;){
    const uint8_t *arg = argv[iarg];
    iarg++;

    if(arg[0] == '-' && arg[1] != '-'){
      if(arg[1] == 0 || arg[2] != 0){
        puts_literal(
          "error: - parameters supposed to be one letter.\n"
          "\n"
        );
        print_small_help();
        _exit(1);
      }

      if(arg[1] == 'h'){ print_help(); }
      else if(arg[1] == 's'){ iarg += param_func_saddr(&argv[iarg]); }
      else{
        puts_literal("error: unexpected argument '-");
        puts_char_repeat(arg[1], 1);
        puts_literal("' found\n\n");
        print_small_help();
        _exit(1);
      }
    }
    else if(arg[0] == '-' && arg[1] == '-'){
      const uint8_t *pstr = &arg[2];

      if(!STR_n0cmp("help", pstr)){ print_help(); }
      else if(!STR_n0cmp("threads", pstr)){ iarg += param_func_threads(&argv[iarg]); }
      else if(!STR_n0cmp("threshold", pstr)){ iarg += param_func_threshold(&argv[iarg]); }
      else if(!STR_n0cmp("flood", pstr)){ pile.threshold = (uint64_t)-1; }
      else if(!STR_n0cmp("prepeat", pstr)){ iarg += param_func_prepeat(&argv[iarg]); }
      else if(!STR_n0cmp("ppspersrcip", pstr)){ iarg += param_func_ppspersrcip(&argv[iarg]); }
      else if(!STR_n0cmp("kernel_bypass", pstr)){ iarg += param_func_kernel_bypass(&argv[iarg]); }
      #ifdef set_use_dpdk
        else if(!STR_n0cmp("use_dpdk", pstr)){ iarg += param_func_use_dpdk(&argv[iarg]); }
      #endif
      else if(!STR_n0cmp("gateway32", pstr)){ iarg += param_func_gateway32(&argv[iarg]); }
      else if(!STR_n0cmp("dstmac", pstr)){ iarg += param_func_dstmac(&argv[iarg]); }
      else if(!STR_n0cmp("saddr", pstr)){ iarg += param_func_saddr(&argv[iarg]); }
      else if(!STR_n0cmp("diface", pstr)){ iarg += param_func_diface(&argv[iarg]); }
      else if(!STR_n0cmp("dst_mac_from_ifname", pstr)){ iarg += param_func_dst_mac_from_ifname(&argv[iarg]); }
      else if(!STR_n0cmp("pci", pstr)){ iarg += param_func_pci(&argv[iarg]); }
      else if(!STR_n0cmp("difaceip", pstr)){ iarg += param_func_difaceip(&argv[iarg]); }
      else if(!STR_n0cmp("sport", pstr)){ iarg += param_func_port(&argv[iarg], 1); }
      else if(!STR_n0cmp("dport", pstr)){ iarg += param_func_port(&argv[iarg], 0); }
      else if(!STR_n0cmp("psize", pstr)){ iarg += param_func_psize(&argv[iarg]); }
      else{
        puts_literal("error: unexpected argument '");
        puts_size(pstr, MEM_cstreu(pstr));
        puts_literal("' found\n\n");
        print_small_help();
        _exit(1);
      }
    }
    else{
      if(pile.target_addr.prefix != 33){
        puts_literal("multiple targets are not allowed\n");
        _exit(1);
      }

      if(NET_addr4prefix_from_string(arg, &pile.target_addr)){
        _abort();
      }
    }
  }

  if(pile.target_addr.prefix == 33){
    puts_literal("need target address\n");
    _exit(1);
  }

  if(pile.difacename == NULL){
    if(pile.difaceip.prefix == 33){
      pile.difaceip = pile.target_addr;
    }
  
    if(pile.difaceip.prefix != 32){
      puts_literal("difaceip's prefix cant be not 32 yet\n");
      _exit(1);
    }
  }

  utility_print_setfd(STDOUT);

  run_entry(NULL);

  #if set_libc
    return 0;
  #else
    _exit(0);
  #endif
}

#if !set_libc
  #include <WITCH/include/_start.h>
#endif
