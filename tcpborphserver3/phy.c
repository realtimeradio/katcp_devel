#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>
#include "phy.h"

/***SFP operation types***/
//MDIO opcodes
#define ADDRESS     0
#define WRITE       2
#define RD_ADDR_INC 4
#define READ        6

//EMAC configurations
#define CONF_WRITE  3
#define CONF_READ   5

#define UNKNOWN_ADDR    NULL

struct fpga_reg {
    const char      *name;          //name of the mapped fpga register
    uint32_t        *abs_addr;      //absolute addresses of fpga register = start of memory map + register position offset
};

static struct fpga_reg fpga_reg_map[] = {
    [INDEX_SFP_GPIO_DATA_IN]    =   {"sfp_gpio_data_in" , UNKNOWN_ADDR},
    [INDEX_SFP_GPIO_DATA_OUT]   =   {"sfp_gpio_data_out", UNKNOWN_ADDR},
    [INDEX_SFP_GPIO_DATA_OE]    =   {"sfp_gpio_data_oe" , UNKNOWN_ADDR},
    [INDEX_SFP_GPIO_DATA_DED]   =   {"sfp_gpio_data_ded", UNKNOWN_ADDR},
    [INDEX_SFP_MDIO_SEL]        =   {"sfp_mdio_sel"     , UNKNOWN_ADDR},
    [INDEX_SFP_OP_ISSUE]        =   {"sfp_op_issue"     , UNKNOWN_ADDR},
    [INDEX_SFP_OP_TYPE]         =   {"sfp_op_type"      , UNKNOWN_ADDR},
    [INDEX_SFP_OP_ADDR]         =   {"sfp_op_addr"      , UNKNOWN_ADDR},
    [INDEX_SFP_OP_DATA]         =   {"sfp_op_data"      , UNKNOWN_ADDR},
    [INDEX_SFP_OP_RESULT]       =   {"sfp_op_result"    , UNKNOWN_ADDR},
    [INDEX_SFP_OP_DBG]          =   {"sfp_op_dbg"       , UNKNOWN_ADDR},
    [INDEX_SFP_OP_DBG1]         =   {"sfp_op_dbg1"      , UNKNOWN_ADDR},
    [MAX_REG_INDEX]             =   {NULL               , NULL        }
};


/*****perform a basic register access set operation on the FPGA (into memory mapped registers)*****/
static int fpga_set_reg(unsigned int index, uint32_t val){
    if (fpga_reg_map[index].abs_addr != NULL){
        *(fpga_reg_map[index].abs_addr) = val;
        return 0;
    }
    else{
        fprintf(stderr, "Could not write to register %s : address not initialized\n", fpga_reg_map[index].name);
        return 1;
    }
}


/*****perform a basic register access get operation on the FPGA (out of memory mapped registers)*****/
static uint32_t fpga_get_reg(unsigned int index){
    if (fpga_reg_map[index].abs_addr != NULL){
        return *(fpga_reg_map[index].abs_addr);
    }
    else{
        fprintf(stderr, "Could not read from register %s : address not initialized\n", fpga_reg_map[index].name);
        return 0;
    }
}


/*****perform a basic MDIO operation on FPGA*****/
/*LSB signifies EMAC configuration or not.
  Bits 2:1 are the MDIO opcode
  MDIO opcodes
ADDRESS:      0, # 0b000 - 0 << 1
WRITE:        2, # 0b010 - 1 << 1
RD_ADDR_INC:  4, # 0b100 - 2 << 1
READ:         6, # 0b110 - 3 << 1
EMAC configuration
CONF_WRITE:   3, # 0b011
CONF_READ:    5, # 0b101
 */
static uint32_t fpga_mdio_op(uint32_t op_type, uint32_t addr, uint32_t payload){   //addr = mdio frame address, payload = mdio frame payload
    fpga_set_reg( INDEX_SFP_OP_TYPE, op_type);
    fpga_set_reg( INDEX_SFP_OP_ADDR, addr);
    if ((op_type == READ) || (op_type == RD_ADDR_INC)){
        fpga_set_reg( INDEX_SFP_OP_ISSUE, 0x1);
        usleep(100); //by trial&error : delay required to allow MDIO I/F time to complete operation before reading sfp_op_result register
        return fpga_get_reg( INDEX_SFP_OP_RESULT );
    }
    else{
        fpga_set_reg( INDEX_SFP_OP_DATA, payload);
        fpga_set_reg( INDEX_SFP_OP_ISSUE,   0x1);
        usleep(100);
        return 0;
    }
}


/*****Reset a phy on a mezzanine card*****/
/*
   mgt_gpio[11]  Unused
   mgt_gpio[10]  SFP1: MDIO          MDIO data line
   mgt_gpio[9]   SFP1: MDC           MDIO clock line
   ENABLE SW RESET => mgt_gpio[8]   SFP1: PHY1 RESET    PHY reset when '1'

   ENABLE SW RESET => mgt_gpio[7]   SFP1: PHY0 RESET    PHY reset when '1'
   mgt_gpio[6]   SFP1: MDIO Enable   Enable MDIO mode when '1'
   mgt_gpio[5]   Unused
   mgt_gpio[4]   SFP0: MDIO          MDIO data line

   mgt_gpio[3]   SFP0: MDC           MDIO clock line
   ENABLE SW RESET => mgt_gpio[2]   SFP0: PHY1 RESET    PHY reset when '1'
   ENABLE SW RESET => mgt_gpio[1]   SFP0: PHY0 RESET    PHY reset when '1'
   mgt_gpio[0]   SFP0: MDIO Enable   Enable MDIO mode when '1'
 */

int mezz_phy_reset_op(int mezz_card, int phy_num){
    uint32_t val = 0;
    int retval =0;

    if (mezz_card == 0){
        if (phy_num==0){
            val = 0x02;             //0b0000 0000 0010
        }
        else if(phy_num == 1){
            val = 0x04;             //0b0000 0000 0100
        }
        else{
            return 1;
        }
    }
    else if (mezz_card == 1){
        if (phy_num==0){
            val = 0x80;             //0b0000 1000 0000
        }
        else if(phy_num == 1){
            val = 0x100;            //0b0001 0000 0000
        }
        else{
            return 1;
        }
    }
    else{
        return 1;
    }
    retval += fpga_set_reg(INDEX_SFP_GPIO_DATA_OE,  val);
    retval += fpga_set_reg(INDEX_SFP_GPIO_DATA_OUT, val);
    usleep(1);
    retval += fpga_set_reg(INDEX_SFP_GPIO_DATA_OUT, 0x0);
    return retval;
}


/******Enable software control of SFP MDIOs.

  mgt_gpio[11]  Unused
  ENABLE SW CONTROL => mgt_gpio[10]  SFP1: MDIO          MDIO data line
  ENABLE SW CONTROL => mgt_gpio[9]   SFP1: MDC           MDIO clock line
  mgt_gpio[8]   SFP1: PHY1 RESET    PHY reset when '1'

  mgt_gpio[7]   SFP1: PHY0 RESET    PHY reset when '1'
  mgt_gpio[6]   SFP1: MDIO Enable   Enable MDIO mode when '1'
  mgt_gpio[5]   Unused
  ENABLE SW CONTROL => mgt_gpio[4]   SFP0: MDIO          MDIO data line

  ENABLE SW CONTROL => mgt_gpio[3]   SFP0: MDC           MDIO clock line
  mgt_gpio[2]   SFP0: PHY1 RESET    PHY reset when '1'
  mgt_gpio[1]   SFP0: PHY0 RESET    PHY reset when '1'
  mgt_gpio[0]   SFP0: MDIO Enable   Enable MDIO mode when '1'
 */

void fpga_mdio_sw_config(){
    //fpga_set_reg(INDEX_SFP_GPIO_DATA_DED, 0x618);  //0b0110 0001 1000 -> original
    fpga_set_reg(INDEX_SFP_GPIO_DATA_DED, 0x659);  //0b0110 0101 1001 -> original plus MDIO enable of sfp
    fpga_mdio_op(CONF_WRITE, 0x340, 0x7f);        //set EMAC MDIO configuration clock divisor and enable MDIO
}


/*****Vitesse VSC8488 phy operations*****/
/*****write to device registers on phy using FPGA-MDIO I/F (MDIO frame payload is only 16bits wide)*****/
void phy_write_op(uint8_t port_addr, uint8_t device_addr, uint16_t reg_addr, uint16_t data){
    //Each MDIO frame has address made up of port and device addr
    //NOTE: An address MDIO operation on FPGA structures the sfp_op_addr as follows to connect to phy: 0xUULL
    //      UU => upper byte contains the port addr of the frame constructed as follows
    //      UU[4:1]={PADDR_pin[4:1]} and UU[0]=channel_number{0 or 1}
    //      LL => lower byte contains the device address/number to be written to e.g 0x1E for MCU or 0x1F for RAM, etc.
    uint32_t p_d_addr = (((uint32_t) port_addr) << 8) + ((uint32_t) device_addr);
 
    //requires two MDIO instruction frame cycles:
    //1) set the address of the register on the phy-ch-device to be written to
    fpga_mdio_op(ADDRESS, p_d_addr, (uint32_t)reg_addr);
    //2) write to the register
    fpga_mdio_op(WRITE, p_d_addr, (uint32_t)data);
}


/*****read from device registers on phy using FPGA-MDIO I/F (MDIO frame payload is only 16bits wide)*****/
uint32_t phy_read_op(uint8_t port_addr, uint8_t device_addr, uint16_t reg_addr){
    //similar to phy_write_op -> 1st send address frame then 2nd a read frame
    uint32_t p_d_addr = (((uint32_t) port_addr) << 8) + ((uint32_t) device_addr);

    fpga_mdio_op(ADDRESS, (uint32_t)p_d_addr, (uint32_t)reg_addr);
    return fpga_mdio_op(READ, (uint32_t)p_d_addr, 0);
}


int fpga_reg_addr_init_by_name(const char *reg_name, uint32_t *addr){
    int i=0;

    while (fpga_reg_map[i].name != NULL){
        if ( strcmp(fpga_reg_map[i].name, reg_name) == 0 ){         //if strings equal
            fpga_reg_map[i].abs_addr = addr;
            return 0;
        }
        i++;
    }
    fprintf(stderr, "Register %s not valid\n", reg_name);
    return 1;
}


int fpga_reg_addr_init_by_index(uint8_t reg_index, uint32_t *addr){
    if (reg_index < MAX_REG_INDEX){
        fpga_reg_map[reg_index].abs_addr = addr;
        return 0;
    }
    else{
        fprintf(stderr, "Not a valid fpga_reg_map index\n");
        return 1;
    }
}


const char * fpga_reg_name_lookup(uint8_t reg_index){
    if (reg_index < MAX_REG_INDEX){
        return fpga_reg_map[reg_index].name;
    }
    else{
        fprintf(stderr, "Not a valid fpga_reg_map index\n");
        return NULL;
    }
}


int mezz_select(int mezz_card){
    if ((mezz_card == MEZZ0) || (mezz_card == MEZZ1)){
        return fpga_set_reg(INDEX_SFP_MDIO_SEL, mezz_card);
    }
    else{
        fprintf(stderr, "Not a valid mezzanine card number\n");
        return 1;
    }
}

