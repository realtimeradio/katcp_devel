#ifndef _GPIO_SMAP_H
#define _GPIO_SMAP_H

//FIXME This has no reason to be here (but was previously provided [stupidly] by the rpjtag code)
#define FPGA_BIN_SIZE 3011324

//AM335x GPIO register addresses
//See http://vabi-robotics.blogspot.com/2013/10/register-access-to-gpios-of-beaglebone.html
#define GPIO0_ADDR 0x44E07000
#define GPIO1_ADDR 0x4804C000
#define GPIO2_ADDR 0x481AC000
#define GPIO3_ADDR 0x481AF000
#define OE_ADDR 0x134
#define GPIO_DATAOUT 0x13C
#define GPIO_DATAIN 0x138

#define GPIO_SMAP_ADDR GPIO1_ADDR

#define SMAP_INIT_DELAY_NS 10000000
#define SMAP_CLK_DELAY_NS 0

#define PIN_FPGA_CCLK        29
#define PIN_FPGA_INIT_B      28
#define PIN_FPGA_DONE        27
#define PIN_FPGA_PROG_B      26
#define PIN_FPGA_SMAP_CSI    25
#define PIN_FPGA_SMAP_RDRW_B 24
#define PIN_FPGA_SMAP_D0     16
#define PIN_FPGA_SMAP_D1     17
#define PIN_FPGA_SMAP_D2     18
#define PIN_FPGA_SMAP_D3     19
#define PIN_FPGA_SMAP_D4     20
#define PIN_FPGA_SMAP_D5     21
#define PIN_FPGA_SMAP_D6     22
#define PIN_FPGA_SMAP_D7     23

int init_fpga();
int init_mm();
int deinit_mm();
int activate_smap();
int write_smap(unsigned char *buf, int nbytes);

#endif
