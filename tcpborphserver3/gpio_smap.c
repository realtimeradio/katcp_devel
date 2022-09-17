#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <time.h>
#include "gpio_smap.h"

struct timespec ts_clk, ts_init, ts_rem;

static int fd;
static uint32_t *pinmm;

//int set(char *fname) {
//  FILE* f = fopen(fname, "w");
//  if (!f) {
//      fprintf(stderr, "Unable to open path for writing\n");
//      return 1;
//  }
//  fprintf(f, "1\n");
//  fclose(f);
//  return 0;
//};
//
//int clear(char *fname) {
//  FILE* f = fopen(fname, "w");
//  if (!f) {
//      fprintf(stderr, "Unable to open path for writing\n");
//      return 1;
//  }
//  fprintf(f, "0\n");
//  fclose(f);
//  return 0;
//};
//

int set(unsigned int pin) {
  pinmm[GPIO_DATAOUT/4] |= (1<<pin);
  return 0;
}
int clear(unsigned int pin) {
  pinmm[GPIO_DATAOUT/4] &= ~(1<<pin);
  return 0;
}

int set_val(int pin, unsigned int v) {
  if (v==1) {
    return set(pin);
  } else if (v==0) {
    return clear(pin);
  } else {
    return 1;
  };
};

int smap_toggle_clock() {
  int rv = 0;
  rv += clear(PIN_FPGA_CCLK);
#if SMAP_CLK_DELAY_NS > 0
  nanosleep(&ts_clk, &ts_rem);
#endif
  rv += set(PIN_FPGA_CCLK);
#if SMAP_CLK_DELAY_NS > 0
  nanosleep(&ts_clk, &ts_rem);
#endif
  rv += clear(PIN_FPGA_CCLK);
  return rv;
};

int smap_set_byte(unsigned char x) {
  // This could be much neater if we assumed
  // that the SMAP pins were sequential
  // (which they are on the ADZE)
  uint32_t v = pinmm[GPIO_DATAOUT/4];
  v &= ~(1 << (PIN_FPGA_SMAP_D0));
  v &= ~(1 << (PIN_FPGA_SMAP_D1));
  v &= ~(1 << (PIN_FPGA_SMAP_D2));
  v &= ~(1 << (PIN_FPGA_SMAP_D3));
  v &= ~(1 << (PIN_FPGA_SMAP_D4));
  v &= ~(1 << (PIN_FPGA_SMAP_D5));
  v &= ~(1 << (PIN_FPGA_SMAP_D6));
  v &= ~(1 << (PIN_FPGA_SMAP_D7));

  // Bit swap (see XAPP 5i83)
  v |= (((x >> 0) & 1) << PIN_FPGA_SMAP_D7);
  v |= (((x >> 1) & 1) << PIN_FPGA_SMAP_D6);
  v |= (((x >> 2) & 1) << PIN_FPGA_SMAP_D5);
  v |= (((x >> 3) & 1) << PIN_FPGA_SMAP_D4);
  v |= (((x >> 4) & 1) << PIN_FPGA_SMAP_D3);
  v |= (((x >> 5) & 1) << PIN_FPGA_SMAP_D2);
  v |= (((x >> 6) & 1) << PIN_FPGA_SMAP_D1);
  v |= (((x >> 7) & 1) << PIN_FPGA_SMAP_D0);

  pinmm[GPIO_DATAOUT/4] = v;
  return 0;
};

int smap_send_byte(char x) {
  int rv = 0;
  rv += smap_set_byte(x);
  rv += smap_toggle_clock();
  return rv;
};

int read_pin(int pin) {
  return (pinmm[GPIO_DATAIN/4] >> pin) & 1;
}

int init_mm() {
  fprintf(stderr, "Opening /dev/mem\n");
  fd = open("/dev/mem", O_RDWR | O_SYNC);
  if (fd < 0) {
    fprintf(stderr, "Failed to open /dev/mem\n");
    return 1;
  };
  fprintf(stderr, "Mem-mapping\n");
  pinmm = (uint32_t *) mmap(NULL, 0x1000, PROT_READ | PROT_WRITE, MAP_SHARED, fd, GPIO_SMAP_ADDR);
  if (pinmm == NULL) {
    fprintf(stderr, "Failed to mmap file\n");
    return 1;
  };
  fprintf(stderr, "Done\n");
  return 0;
};

int init_fpga() {
  int rv = 0;
  int i;
  ts_clk.tv_sec = SMAP_CLK_DELAY_NS / 1000000000;
  ts_clk.tv_nsec = SMAP_CLK_DELAY_NS % 1000000000;
  ts_init.tv_sec = SMAP_INIT_DELAY_NS / 1000000000;
  ts_init.tv_nsec = SMAP_INIT_DELAY_NS % 1000000000;
  // deassert clock before messing with config pins
  rv += clear(PIN_FPGA_CCLK);
#if SMAP_CLK_DELAY_NS > 0
  nanosleep(&ts_clk, &ts_rem);
#endif
  // deassert chip select
  rv += set(PIN_FPGA_SMAP_RDRW_B);
  // Deprogram FPGA
  rv += clear(PIN_FPGA_PROG_B); // deprog FPGA
  // Wait for init pin to go low
  i=0;
  while(1) {
    if (read_pin(PIN_FPGA_INIT_B) == 0) {
      fprintf(stderr, "INIT_B low\n");
      break;
    };
    nanosleep(&ts_init, &ts_rem);
    i++;
    if (i>100) {
      fprintf(stderr, "Timed out waiting for INIT_B to go low\n");
      return 1;
    };
  };
  // Deassert Prog_B and wait for init_b to go high
  rv += set(PIN_FPGA_PROG_B);
  i=0;
  while(1) {
    if (read_pin(PIN_FPGA_INIT_B) == 1) {
      fprintf(stderr, "INIT_B high\n");
      break;
    };
    nanosleep(&ts_init, &ts_rem);
    i++;
    if (i==100) {
      fprintf(stderr, "Timed out waiting for INIT_B to go high\n");
      return 1;
    };
  };
  return rv;
};

int deinit_mm() {
  fprintf(stderr, "Unmapping /dev/mem\n");
  munmap(pinmm, 0x1000);
  close(fd);
  return 0;
}


int activate_smap() {
  int rv = 0;
  rv += clear(PIN_FPGA_SMAP_RDRW_B);
  nanosleep(&ts_init, &ts_rem);
  rv += clear(PIN_FPGA_SMAP_CSI);
  nanosleep(&ts_init, &ts_rem);
  return rv;
}

int write_smap(unsigned char *buf, int nbytes) {
  int rv = 0;
  int i;
  fprintf(stderr, "SMAP Writing %d bytes\n", nbytes);
  for(i=0; i<nbytes; i++) {
    rv += smap_send_byte(buf[i]);
  };
  // Per XAPP583, continue to apply clocks until DONE
  // is high. Then send 8 more.
  i=0;
  while(1) {
    if (read_pin(PIN_FPGA_DONE) == 1) {
      fprintf(stderr, "DONE high\n");
      break;
    };
    rv += smap_send_byte(0xff);
    i++;
    if (i==100000) {
      fprintf(stderr, "Timed out waiting for DONE to go high\n");
      return 1;
    };
  };

  for(i=0; i<8; i++){
    rv += smap_send_byte(0xff);
  }
  return rv;
}

