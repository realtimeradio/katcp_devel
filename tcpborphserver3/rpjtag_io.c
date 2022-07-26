#include "rpjtag_io.h"
#include <stdlib.h>
#include <sys/mman.h>
#include <stdio.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
//Credit for GPIO setup goes to http://elinux.org/RPi_Low-level_peripherals
//
// Set up a memory regions to access GPIO
//
int  mem_fd;
void *gpio_map;

// I/O access
volatile unsigned *gpio;

void tick_clk()
{
    GPIO_SET(JTAG_TCK);
    //nop_sleep(WAIT); // throttle jtag for fast RPIs
    GPIO_CLR(JTAG_TCK);
}

void close_io()
{
    munmap(gpio_map, BLOCK_SIZE);
}

int setup_io()
{
    return 0;
}

int read_jtag_tdo()
{
    return 0;
}

int tdi=-1;

void send_cmd_no_tms(int iTDI)
{
}

//void set_pin(int pin, int val)
//{
//    if (val == 0)
//    {
//        GPIO_CLR(pin);
//    }
//    else
//    {
//        GPIO_SET(pin);
//    }
//}

void send_cmd(int iTDI,int iTMS)
{
}


void reset_clk()
{
}

//Mainly used for command words (CFG_IN)
void send_cmdWord_msb_first(unsigned int cmd, int lastBit, int bitoffset) //Send data, example 0xFFFF,0,20 would send 20 1's, with not TMS
{
}

//Mainly used for IR Register codes
void send_cmdWord_msb_last(unsigned int cmd, int lastBit, int bitoffset) //Send data, example 0xFFFF,0,20 would send 20 1's, with not TMS
{
}

void send_byte(unsigned char byte, int lastbyte) //Send single byte, example from fgetc
{
}

void send_byte_no_tms(unsigned char byte)
{
}

//Does a NOP call in BCM2708, and is meant to be run @ 750 MHz
void nop_sleep(long x)
{
}

void jtag_read_data(char* data,int iSize)
{
}
