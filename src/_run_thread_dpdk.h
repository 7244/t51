static int _run_thread_dpdk(void *p_0){
  uint16_t i_dpdk_interface = *(uint16_t *)p_0;
  unsigned lcore_id = rte_lcore_id();

  while(1){
    __flush_compiler_memory();
  }
}