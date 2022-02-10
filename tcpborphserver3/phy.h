/*
*Notes:
*   1) initialize register addresses either by name or by index before using other functions
*
*/


#ifndef PHY_H
#define PHY_H

//REGISTER INDICES
#define INDEX_SFP_GPIO_DATA_IN  0
#define INDEX_SFP_GPIO_DATA_OUT 1
#define INDEX_SFP_GPIO_DATA_OE  2
#define INDEX_SFP_GPIO_DATA_DED 3
#define INDEX_SFP_MDIO_SEL      4
#define INDEX_SFP_OP_ISSUE      5
#define INDEX_SFP_OP_TYPE       6
#define INDEX_SFP_OP_ADDR       7
#define INDEX_SFP_OP_DATA       8
#define INDEX_SFP_OP_RESULT     9
#define INDEX_SFP_OP_DBG        10
#define INDEX_SFP_OP_DBG1       11
#define MAX_REG_INDEX           12

//PHY MDIO INSTRUCTION FRAME PORT ADDRESS DEFINITIONS
//bit[7:5] = don't care ::: bit[4:1] = phy pin addr pins[4:1] ::: bit[0] = 0 for channel 0 or = 1 for ch 1
#define PORTADDR_PHY0_CH0   0x00
#define PORTADDR_PHY0_CH1   0x01
#define PORTADDR_PHY1_CH0   0x1E
#define PORTADDR_PHY1_CH1   0x1F

 //PHY MDIO INSTRUCTION FRAME DEVICE ADDRESS DEFINITIONS
#define DEVADDR_MCU         0x1E
#define DEVADDR_RAM         0x1F

#define MEZZ0   0
#define MEZZ1   1
#define PHY0    0
#define PHY1    1

int mezz_phy_reset_op(int mezz_card, int phy_num);

void fpga_mdio_sw_config();

int fpga_reg_addr_init_by_name(const char *reg_name, uint32_t *addr);

int fpga_reg_addr_init_by_index(uint8_t reg_index, uint32_t *addr);

void phy_write_op(uint8_t port_addr, uint8_t device_addr, uint16_t reg_addr, uint16_t data);

uint32_t phy_read_op(uint8_t port_addr, uint8_t device_addr, uint16_t reg_addr);

const char * fpga_reg_name_lookup(uint8_t reg_index);

int mezz_select(int mezz_card);

#endif
