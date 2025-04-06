#define set_program_name "t51"

#ifndef set_Verbose
  #define set_Verbose 0
#endif


#define FUNC static

#define __platform_nostdlib
#define WITCH_PRE_is_not_allowed
#define __WITCH_IO_allow_sigpipe

#include <WITCH/WITCH.h>
#include <WITCH/PR/PR.h>
#include <WITCH/IO/IO.h>
#include <WITCH/NET/NET.h>

#include "utility.h"

typedef struct{
  uint32_t target_ipv4;
  uint64_t threshold;
  uint32_t payload_size;

  bool rand_sport;
  bool rand_dport;

  uint16_t sport;
  uint16_t dport;
}pile_t;

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
    "  -h, --help           Print help\n"
    "      --threshold NUM  Threshold of packets to send     (default 1000)\n"
    "      --flood          This option supersedes the 'threshold'\n"
    "\n"
    "DCCP/TCP/UDP Options:\n"
    "      --sport NUM            source port                (default RANDOM)\n"
    "      --dport NUM            destination port           (default RANDOM)\n"
    "      --psize NUM            payload size               (default 32)\n"
  );

  _exit(0);
}

FUNC uintptr_t param_func_threshold(const uint8_t **arg, pile_t *pile){
  uintptr_t index = 0;
  pile->threshold = STR_psu64_iguess_abort(arg[0], &index);

  return 1;
}

FUNC uintptr_t param_func_port(const uint8_t **arg, pile_t *pile, bool s_or_d){
  uintptr_t index = 0;
  if(s_or_d){
    pile->rand_sport = 0;
    pile->sport = STR_psu16_iguess_abort(arg[0], &index);
  }
  else{
    pile->rand_dport = 0;
    pile->dport = STR_psu16_iguess_abort(arg[0], &index);
  }
  return 1;
}

FUNC uintptr_t param_func_psize(const uint8_t **arg, pile_t *pile){
  uintptr_t index = 0;
  pile->payload_size = STR_psu32_iguess_abort(arg[0], &index);

  return 1;
}

__attribute__((noreturn))
FUNC void main(uintptr_t argc, const uint8_t **argv){

  #include <WITCH/PlatformOpen.h>

  utility_init_print();

  pile_t pile;

  pile.target_ipv4 = 0;
  pile.threshold = 1000;
  pile.payload_size = 32;

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
      else if(!STR_n0cmp("flood", pstr)){ pile.threshold = (uint64_t)-1; }
      else if(!STR_n0cmp("threshold", pstr)){ iarg += param_func_threshold(&argv[iarg], &pile); }
      else if(!STR_n0cmp("sport", pstr)){ iarg += param_func_port(&argv[iarg], &pile, 1); }
      else if(!STR_n0cmp("dport", pstr)){ iarg += param_func_port(&argv[iarg], &pile, 0); }
      else if(!STR_n0cmp("psize", pstr)){ iarg += param_func_psize(&argv[iarg], &pile); }
      else{
        puts_literal("error: unexpected argument '");
        puts_size(pstr, MEM_cstreu(pstr));
        puts_literal("' found\n\n");
        print_small_help();
        _exit(1);
      }
    }
    else{
      if(pile.target_ipv4 != 0){
        puts_literal("multiple targets are not allowed\n");
        _exit(1);
      }

      pile.target_ipv4 = NET_ipv4_from_string(arg);
    }
  }

  if(pile.target_ipv4 == 0){
    puts_literal("need target ipv4 address\n");
    _exit(1);
  }

  utility_print_setfd(STDOUT);

  run(&pile);

  _exit(0);
}

#include <WITCH/include/_start.h>
