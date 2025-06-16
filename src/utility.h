#define STDIN 0
#define STDOUT 1
#define STDERR 2

#include _WITCH_PATH(STR/STR.h)
#include _WITCH_PATH(TH/TH.h)

uint8_t _utility_print_fdint;
uint16_t _utility_print_index;
uint8_t _utility_print_buffer[0x1000];

FUNC void flush_print(){
  IO_fd_t fd;
  IO_fd_set(&fd, _utility_print_fdint);
  IO_write(
    &fd,
    _utility_print_buffer,
    _utility_print_index
  );
  _utility_print_index = 0;
}
FUNC void utility_init_print(){
  _utility_print_fdint = STDERR;
  _utility_print_index = 0;
}

#define _exit(val) \
  flush_print(); \
  PR_exit(val); \
  __unreachable();

FUNC void utility_print_setfd(uint32_t fdint){
  if(_utility_print_fdint != fdint){
    flush_print();
    _utility_print_fdint = fdint;
  }
}

FUNC void puts_size(const void *ptr, uintptr_t size){
  for(uintptr_t i = 0; i < size; i++){
    _utility_print_buffer[_utility_print_index] = ((const uint8_t *)ptr)[i];
    if(++_utility_print_index == sizeof(_utility_print_buffer)){
      flush_print();
    }
  }
}
#define puts_literal(literal) \
  puts_size(literal, sizeof(literal) - 1)

FUNC void puts_char_repeat(uint8_t ch, uintptr_t size){
  while(size--){
    puts_size(&ch, 1);
  }
}

FUNC void utility_puts_number(uint64_t num){
  uint8_t buf[64];
  uint8_t *buf_ptr;
  uintptr_t size;

  buf_ptr = buf;
  STR_uto64(num, 10, &buf_ptr, &size);
  puts_size(buf_ptr, size);
}

FUNC void _abort(const char *filename, uintptr_t filename_length, uintptr_t line){
  utility_print_setfd(STDERR);

  puts_literal("err at ");
  puts_size(filename, filename_length);
  puts_char_repeat(':', 1);
  utility_puts_number(line);
  puts_char_repeat('\n', 1);

  _exit(1);
}
#define _abort() \
  _abort(__FILE__, sizeof(__FILE__) - 1, __LINE__); \
  __unreachable();


FUNC uint32_t fast_limiter(
  uint64_t *p_current,
  uint64_t *p_last_refill_at,
  uint64_t value,
  uint64_t wanted,
  uint64_t ctime
){
  uint64_t delta = ctime - *p_last_refill_at;

  if((sint64_t)delta >= (sint64_t)10000000){
    uint64_t delta_scale = delta / 10000000;
    if (delta_scale > 100) {
        delta_scale = 100;
    }

    uint64_t tokens_to_sub = *p_current * delta_scale / 100;

    if (tokens_to_sub) {
      ctime -= delta % 10000000;

      uint64_t reverse_worth = delta_scale * 10000000 - delta_scale * 10000000 * 100 /
        (
          (*p_current * delta_scale * 100) / (tokens_to_sub * 100)
        )
      ;
      ctime -= reverse_worth;

      *p_current -= tokens_to_sub;
      *p_last_refill_at = ctime;
    }
  }

  if (*p_current + value >= wanted) {
      return 1;
  }
  
  *p_current += value;

  return 0;
}
