#include <stdio.h>
#include <string.h>

#include <stdint.h> // uint8_t, uint16_t
#include <unistd.h> // write

#include <katcp.h>
#include <avltree.h>

#include "tcpborphserver3.h"
#include "rfsoc.h"

#include "alpaca_rfclks.h"

int rfdc_program_pll_cmd(struct katcp_dispatch *d, int argc) {
  int result;
  struct tbs_raw *tr;
  char* pll;
  int pll_type;
  char* tcsfile;
  FILE* fileptr;
  char* buffer;
  int len;
  struct stat st;
  uint32_t* clkconfig;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  // get target rfpll type
  if (argc <= 1) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify rfpll, lmk|lmx");
    return KATCP_RESULT_INVALID;
  }

  pll = arg_string_katcp(d, 1);
  if (pll == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to resolve pll type");
    return KATCP_RESULT_FAIL;
  }

  if (strcmp(pll, "lmk") == 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "will program lmk");
    pll_type = 0;
  } else if (strcmp(pll, "lmx") == 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "will program lmx2594");
    pll_type = 1;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unrecognized rfpll option %s", pll);
    pll_type = -1;
    return KATCP_RESULT_INVALID;
  }

  // use requested file or substitute for the default
  if (argc > 2) {
    tcsfile = arg_string_katcp(d, 2);
    if (tcsfile == NULL) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to resolve file name to program");
      return KATCP_RESULT_FAIL;
    }
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "requested clock file %s", tcsfile);

    if (strchr(tcsfile, '/') != NULL) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "file name %s may not contain a path component", tcsfile);
      return KATCP_RESULT_FAIL;
    }
  } else {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "no clock file specified defaulting to %s", TBS_RFCLK_FILE);
    tcsfile = TBS_RFCLK_FILE;
  }

  len = strlen(tcsfile) + 1 + strlen(tr->r_bof_dir) + 1;
  buffer = malloc(len);
  if (buffer == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes", len);
    return KATCP_RESULT_FAIL;
  }

  snprintf(buffer, len, "%s/%s", tr->r_bof_dir, tcsfile);
  buffer[len - 1] = '\0';

  if (stat(buffer, &st) != 0) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "clock config file %s does not exist", buffer);
    extra_response_katcp(d, KATCP_RESULT_FAIL, "clock config file %s does not exist", buffer);
    free(buffer);
    return KATCP_RESULT_OWN;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "attempting to program %s with %s", pll, buffer);
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%s", buffer);

  fileptr = fopen(buffer, "r");
  free(buffer);
  if (fileptr == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to open %s", tcsfile);
    return KATCP_RESULT_FAIL;
  }

  int prg_cnt = (pll_type == 0) ? LMK_REG_CNT : LMX2594_REG_CNT;
  int pkt_len = (pll_type == 0) ? LMK_PKT_SIZE: LMX_PKT_SIZE;

  clkconfig = readtcs(fileptr, prg_cnt, pll_type);
  if (clkconfig == NULL) {
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "problem parsing clock file. Wrong file for requested pll chip, corrupt file, or could not allocate array");
    return KATCP_RESULT_FAIL;
  }

  /* An alternate approach within katcp/tbs is to use katcp jobs to run a system
   * executable and each rfsoc platform can have their own i2c_util binary built
   *
   * Another idea is to create something similar to the alpaca_i2c_utils with an
   * rclk struct managing the rfpll's present
   */

#if PLATFORM == ZCU216 // || PLATFORM == ZCU208
  // init i2c
  init_i2c_bus();
  init_i2c_dev(I2C_DEV_CLK104);
  // TODO: 510 is when base platform fabric design is in the base tree, but
  // this will change if jasper fully adopts the device tree overlay separating
  // the PL from the device tree when the kernel is build. This will then change
  // as the overlay is applied. Would need to know how to deterministly set or
  // receive the kernel message stating what it was applied as.
  init_clk104_gpio(510);

  // set sdo mux to lmk
  result = set_sdo_mux(LMK_MUX_SEL);
  usleep(0.5e6);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "clk104 gpio sdo mux not set correctly");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // configure spi bridge operation mode, clk frequency
  uint8_t spi_config[2] = {0xf0, 0x03};
  result = i2c_write(I2C_DEV_CLK104, spi_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure clk104 spi bridge");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // program pll
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "programming zcu216 pll\n");
  if (pll_type == 0) {
    result = prog_pll(I2C_DEV_CLK104, LMK_SDO_SS, clkconfig, prg_cnt, pkt_len);
  } else {
    // configure clk104 adc lmx2594 to tile 225
    result = prog_pll(I2C_DEV_CLK104, LMX_SDO_SS224_225, clkconfig, prg_cnt, pkt_len);
  }

  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program pll");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // close devices
  close_i2c_dev(I2C_DEV_CLK104);
  close_i2c_bus();
#elif PLATFORM == ZRF16
  // init i2c
  init_i2c_dev(I2C_DEV_LMK_SPI_BRIDGE);
  init_i2c_dev(I2C_DEV_LMX_SPI_BRIDGE);
  init_i2c_dev(I2C_DEV_IOX);

  // configure spi bridges operation mode, clk frequency
  uint8_t spi_config[2] = {0xf0, 0x03};
  result = i2c_write(I2C_DEV_LMK_SPI_BRIDGE, spi_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure lmk spi bridge");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  result = i2c_write(I2C_DEV_LMX_SPI_BRIDGE, spi_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure lmx spi bridge");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // configure io expander power-on defaults are inputs, set zero for outputs
  uint8_t iox_config[2] = {IOX_CONF_REG, (0xff & ~MUX_SEL_BASE)};
  result = i2c_write(I2C_DEV_IOX, iox_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure iox");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // init iox slection, power-on defaults are high, set low instead
  uint8_t iox_gpio[2] = {IOX_GPIO_REG, iox_config[1]};
  result = i2c_write(I2C_DEV_IOX, iox_gpio, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure iox");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // program pll
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "programming zrf16 pll\n");
  if (pll_type == 0) {
    result = prog_pll(I2C_DEV_LMK_SPI_BRIDGE, LMK_SDO_SS, clkconfig, prg_cnt, pkt_len);
  } else {
    // lmx for tile 224/225
    result = prog_pll(I2C_DEV_LMX_SPI_BRIDGE, LMX_SDO_SS224_225, clkconfig, prg_cnt, pkt_len);
    if (result == XRFDC_FAILURE) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program first adc lmk pll");
      free(clkconfig);
      return KATCP_RESULT_FAIL;
    }
    // lmx for tile 226/227
    result = prog_pll(I2C_DEV_LMX_SPI_BRIDGE, LMX_SDO_SS226_227, clkconfig, prg_cnt, pkt_len);
  }

  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program second adc lmk pll");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  //close devices
  close_i2c_dev(I2C_DEV_LMK_SPI_BRIDGE);
  close_i2c_dev(I2C_DEV_LMX_SPI_BRIDGE);
  close_i2c_dev(I2C_DEV_IOX);
  close_i2c_bus();
#elif PLATFORM == ZCU111
  // init i2c
  init_i2c_bus();
  init_i2c_dev(I2C_DEV_PLL_SPI_BRIDGE);
  init_i2c_dev(I2C_DEV_IOX);

  // configure spi bridge operation mode, clk frequency
  uint8_t spi_config[2] = {0xf0, 0x03};
  result = i2c_write(I2C_DEV_PLL_SPI_BRIDGE, spi_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure pll spi bridge");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // configure io expander power-on defaults are inputs, set zero for outputs
  uint8_t iox_config[2] = {IOX_CONF_REG, (0xff & ~MUX_SEL_BASE)};
  result = i2c_write(I2C_DEV_IOX, iox_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure iox");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // init iox slection, power-on defaults are high, set low instead
  uint8_t iox_gpio[2] = {IOX_GPIO_REG, iox_config[1]};
  result = i2c_write(I2C_DEV_IOX, iox_gpio, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure iox");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // program pll
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "programming zcu111 pll\n");
  if (pll_type == 0) {
    result = prog_pll(I2C_DEV_PLL_SPI_BRIDGE, LMK_SDO_SS, clkconfig, prg_cnt, pkt_len);
  } else {
    // lmx for adc tile 224/225
    result = prog_pll(I2C_DEV_PLL_SPI_BRIDGE, LMX_SDO_SS224_225, clkconfig, prg_cnt, pkt_len);
    if (result == XRFDC_FAILURE) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program adc tile 224/225 lmx pll");
      free(clkconfig);
      return KATCP_RESULT_FAIL;
    }
    // lmx for adc tile 226/227
    result = prog_pll(I2C_DEV_PLL_SPI_BRIDGE, LMX_SDO_SS226_227, clkconfig, prg_cnt, pkt_len);
    if (result == XRFDC_FAILURE) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program adc tile 226/227 lmx pll");
      free(clkconfig);
      return KATCP_RESULT_FAIL;
    }
    // lmx for dac tile 228/229
    result = prog_pll(I2C_DEV_PLL_SPI_BRIDGE, LMX_SDO_SS228_229, clkconfig, prg_cnt, pkt_len);
  }

  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program dac tile 228/229 lmx pll");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  close_i2c_dev(I2C_DEV_PLL_SPI_BRIDGE);
  close_i2c_dev(I2C_DEV_IOX);
  close_i2c_bus();
#elif PLATFORM == RFSoC2x2
  // init i2c
  init_i2c_bus();
  init_i2c_dev(I2C_DEV_PLL_SPI_BRIDGE);
  init_i2c_dev(I2C_DEV_IOX);

  // configure spi bridge operation mode, clk frequency
  uint8_t spi_config[2] = {0xf0, 0x03};
  result = i2c_write(I2C_DEV_PLL_SPI_BRIDGE, spi_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure pll spi bridge");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // configure io expander power-on defaults are inputs, set zero for outputs
  uint8_t iox_config[2] = {IOX_CONF_REG, (0xff & ~MUX_SEL_BASE)};
  result = i2c_write(I2C_DEV_IOX, iox_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure iox");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // init iox slection, power-on defaults are high, set low instead
  uint8_t iox_gpio[2] = {IOX_GPIO_REG, iox_config[1]};
  result = i2c_write(I2C_DEV_IOX, iox_gpio, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure iox");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  //program pll
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "programming rfsoc2x2 pll\n");
  if (pll_type == 0) {
    result = prog_pll(I2C_DEV_PLL_SPI_BRIDGE, LMK_SDO_SS, clkconfig, prg_cnt, pkt_len);
  } else {
    // rfsoc2x2 only supports two inputs with one lmx2594 driving adc tiles 224/226
    // despite the SDO slave select macro, this configures the one lmx2594's for both tiles
    result = prog_pll(I2C_DEV_PLL_SPI_BRIDGE, LMX_SDO_SS224_225, clkconfig, prg_cnt, pkt_len);
  }

  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program pll");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // close devices
  close_i2c_dev(I2C_DEV_PLL_SPI_BRIDGE);
  close_i2c_dev(I2C_DEV_IOX);
  close_i2c_bus();
#elif PLATFORM == RFSoC4x2
  // init spi device
  spi_dev_t spidev;
  spidev.mode = SPI_MODE_0 | SPI_CS_HIGH;
  spidev.bits = 8;
  spidev.speed = 500000;
  spidev.delay = 0;

  if (pll_type == 0) {
    // configure lmk
    strcpy(spidev.device, LMK_SPIDEV);
    init_spi_dev(&spidev);
    result = prog_pll(&spidev, clkconfig, prg_cnt, pkt_len);
    close_spi_dev(&spidev);
  } else {
    // rfsoc4x2 has one adc rfpll and one dac rfpll
    strcpy(spidev.device, ADC_RFPLL_SPIDEV);
    init_spi_dev(&spidev);
    result = prog_pll(&spidev, clkconfig, prg_cnt, pkt_len);
    close_spi_dev(&spidev);
    if (result == XRFDC_FAILURE) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program pll");
      free(clkconfig);
      return KATCP_RESULT_FAIL;
    }

    // now reinit spi to program dac rfpll
    strcpy(spidev.device, DAC_RFPLL_SPIDEV);
    init_spi_dev(&spidev);
    result = prog_pll(&spidev, clkconfig, prg_cnt, pkt_len);
    close_spi_dev(&spidev);
  }

  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program pll");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

#endif

  // release memory from pll tcs config
  free(clkconfig);

  return KATCP_RESULT_OK;
}

/***************************************************************************************/
struct tbs_rfdc *create_tbs_rfdc() {

  struct tbs_rfdc *rfdc;

  rfdc = malloc(sizeof(struct tbs_rfdc));

  //XRFdc RFdcInst;
  rfdc->xrfdc = malloc(sizeof(XRFdc));
  rfdc->metal_dev = malloc(sizeof(*(rfdc->metal_dev)));

  // TODO: rfdc driver successfully completed, found rfdc driver has a built-in
  // member called `IsReady`, should move to using that.
  rfdc->initialized = 0;

  return rfdc;

}

void destroy_tbs_rfdc(struct katcp_dispatch *d, struct tbs_rfdc *rfdc) {

  if (rfdc->xrfdc) {
    // TODO: what is in the driver to free remove the xrfdc and metal device instances
    // because right now destroy would leave metal hanging
    rfdc->xrfdc = NULL;
    rfdc->metal_dev = NULL;
    rfdc->initialized = 0;
  }

  free(rfdc);
  return;

}

/***************************************************************************************/

int init_rfdc(struct katcp_dispatch *d, struct tbs_rfdc *rfdc) {

  int result;
  XRFdc_Config* rfdc_config;

  // init metal
  struct metal_init_params init_param = METAL_INIT_DEFAULTS;
  // TODO is it worth wrapping metal log up somehow in katcp log?
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "current metal loglevel=%d\n", init_param.log_level);
  init_param.log_level = METAL_LOG_DEBUG;
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "updated metal loglevel=%d\n", init_param.log_level);

  int metal_error = metal_init(&init_param);
  if (metal_error) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to initialize metal, errno: %d", metal_error);
    return XRFDC_FAILURE;
  }

  // Initialize the RFDC
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "looking up rfdc device configuration");
  rfdc_config = XRFdc_LookupConfig(RFDC_DEVICE_ID);
  if (rfdc_config == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "rfdc device configuration not found");
    return XRFDC_FAILURE;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "registering rfdc with metal");
  // TODO do we need to check that rfdc->xrfdc is not dangling?
  result= XRFdc_RegisterMetal(rfdc->xrfdc, RFDC_DEVICE_ID, rfdc->metal_dev);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "rfdc could not register with metal");
    return XRFDC_FAILURE;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "starting xrfdc driver initialization");
  result = XRFdc_CfgInitialize(rfdc->xrfdc, rfdc_config);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to initialize rfdc driver from config");
    return XRFDC_FAILURE;
  }

  if (rfdc->xrfdc == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "the rfdc instance is NULL");
    return XRFDC_FAILURE;
  }

  return XRFDC_SUCCESS;
}

int rfdc_init_cmd(struct katcp_dispatch *d, int argc) {
  int result;
  // state info
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  // checking rfdc design meta info
  struct avl_node *an;
  struct meta_entry *rfdc_meta_data;
  int i, middle;


  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  /* TODO: could check if already initialized and exit
  if () {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "rfdc driver is already initialized");
    return KATCP_RESULT_OK;
  }*/

  // use tbs_raw to check that the fpga has been programmed
  switch(tr->r_fpga){
    case TBS_FPGA_MAPPED :
    case TBS_FPGA_READY :
    case TBS_FPGA_PROGRAMMED :
      break;
    default:
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "fpga not programmed");
      return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "about to check metadata");
  // make sure an rfdc exists within the design by checking the meta data 
  an = find_name_node_avltree(tr->r_meta, RFDC_META_KEY);
  if (an == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "rfdc key not present in design meta information");
    return KATCP_RESULT_FAIL;
  }

  rfdc_meta_data = get_node_data_avltree(an);
  if (rfdc_meta_data == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "empty data for rfdc");
    return KATCP_RESULT_FAIL;
  }

  // temporary debug display of rfdc fpg config info
  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "displaying meta entry for rfdc");
  while(rfdc_meta_data != NULL){
    prepend_inform_katcp(d);
    if(rfdc_meta_data->m_size > 0){
      append_string_katcp(d, KATCP_FLAG_STRING, RFDC_META_KEY);
      middle = rfdc_meta_data->m_size - 1;
      for(i = 0; i < middle; i++){
        if(rfdc_meta_data->m[i]){
          append_string_katcp(d, KATCP_FLAG_STRING, rfdc_meta_data->m[i]);
        }
      }
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, rfdc_meta_data->m[i]);
    } else {
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, RFDC_META_KEY);
    }

    rfdc_meta_data = rfdc_meta_data->m_next;
  }

  // fpga programmed, rfdc is seen in the design, not yet setup so begin to initialize
  rfdc = tr->r_rfdc;

  //rfdc->dto_loaded = 1;
  //if (rfdc->dto_loaded == 0) {
  //  log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "device tree overlay for rfdc not present");
  //  return KATCP_RESULT_FAIL;
  //}

  /* initialize rfdc driver */
  result = init_rfdc(d, rfdc);
  if (result == XRFDC_FAILURE) {
    return KATCP_RESULT_FAIL;
  }
  rfdc->initialized = 1;
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "rfdc configured successfuly");

  return KATCP_RESULT_OK;
}

/************************************************************************************************/

/*
 * Manage tbs dto (?dto apply|remove)
 *
 * 'apply' looks for the tbs dtbo file (previously uploaded) and will apply it.
 * This will remove any currently applied tbs overlay as the assumption is that
 * the primary use case will be followed by a reprogramming of the PL and the
 * user will want to apply one instead of having first remove any active
 * overlays and then apply
 *
 * 'remove' will look for any active tbs dto and remove it, nothing otherwise.
 *
 * ideally, applying the dto is the preferred approach to programming the PL but
 * not yet the programming entry need to develop towards that.
 *
 */
int tbs_dto_cmd(struct katcp_dispatch *d, int argc) {

  struct tbs_raw *tr;
  char* dto_action;
  // for dto
  struct stat st;
  char overlay_path[128];
  int result, fd_dto, rd, wr;
  char overlay_status[16] = {0};

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  if (argc <= 1) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify option, apply|remove");
    return KATCP_RESULT_INVALID;
  }

  dto_action = arg_string_katcp(d, 1);
  if (dto_action == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to resolve pll type");
    return KATCP_RESULT_FAIL;
  }

  // check for dto path in configfs
  int dto_dirpath_exist = 0;
  sprintf(overlay_path, TBS_DTBO_BASE, TBS_DTBO_NAME);
  if(stat(overlay_path, &st) == 0) {
    if (S_ISDIR(st.st_mode)) {
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "overlay path already exists");
      dto_dirpath_exist = 1;
    }
  }

  // perform dto action
  if (strcmp(dto_action, "apply") == 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "will apply overlay");
    if(dto_dirpath_exist) {
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "removing active overlay first");
      result = rmdir(overlay_path);
      if (result < 0) {
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "error removing overlay");
        return KATCP_RESULT_FAIL;
      }
    }

    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "creating dto %s", TBS_DTBO_NAME);
    result = mkdir(overlay_path, 0755);
    if (result < 0) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not create device tree overlay");
      return KATCP_RESULT_FAIL;
    }

    sprintf(overlay_path, TBS_DTBO_PATH, TBS_DTBO_NAME);
    fd_dto = open(overlay_path, O_RDWR);
    if (fd_dto < 0) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open dto path for writing new overlay");
      return KATCP_RESULT_FAIL;
    }

    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "writing dto");
    wr = write(fd_dto, TBS_DTBO_FILE, sizeof(TBS_DTBO_FILE));

    // check status of applying dto
    // note: `applied` does not always mean everything went well. Reporting
    // status to user but we assume no errors if result is `applied`
    sprintf(overlay_path, TBS_DTBO_STAT, TBS_DTBO_NAME);
    fd_dto = open(overlay_path, O_RDONLY);
    if (fd_dto < 0) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open dto status file for reading");
      return KATCP_RESULT_FAIL;
    }

    rd = read(fd_dto, overlay_status, 16); // 16, as it seems to be the longest str to return
    close(fd_dto);

    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%s", overlay_status);

    if (strncmp(overlay_status, "applied", 7) != 0) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "error applying dto");
      return KATCP_RESULT_FAIL;
    }

  } else if (strcmp(dto_action, "remove") == 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "will remove overlay");
    if (dto_dirpath_exist) {
      result = rmdir(overlay_path);
      if (result < 0) {
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "error removing overlay");
      }
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "removed");
    } else {
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "no dto to remove");
    }
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unrecognized dto action option %s", dto_action);
    return KATCP_RESULT_INVALID;
  }

  return KATCP_RESULT_OK;
}

/*
 * creates tbs device tree overlay
 *
 * check for tbs dto, if one exists and the status is applied inform user must
 * remove before applying. Otherwise, creates a the tbs overlay.
 *
 * not used, an aux. implementation should we need it
 */
int create_overlay_tbs(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  // for dto
  struct stat st;
  char overlay_path[128];
  int result, fd_dto, rd, wr;
  char overlay_status[16] = {0};

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  // check for dto path in configfs
  sprintf(overlay_path, TBS_DTBO_BASE, TBS_DTBO_NAME);
  if(stat(overlay_path, &st) == 0){
    if (S_ISDIR(st.st_mode)) {
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "overlay path already exists");
    }
  } else {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "creating dto %s", TBS_DTBO_NAME);
    result = mkdir(overlay_path, 0755);

    if (result < 0) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not create device tree overlay");
      return KATCP_RESULT_FAIL;
    }
  }

  // check if a dto has been applied, apply one if not
  sprintf(overlay_path, TBS_DTBO_STAT, TBS_DTBO_NAME);
  fd_dto = open(overlay_path, O_RDONLY);
  if (fd_dto < 0) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open dto status file for reading");
    return KATCP_RESULT_FAIL;
  }

  rd = read(fd_dto, overlay_status, 16); // 16, as it seems to be the longest str to return
  close(fd_dto);

  // note, dto status `applied` does not always mean everything went well, we just assume it had 
  if (strncmp(overlay_status, "applied", 7) == 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "a dto already exists, should remove before apply");
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "dto already applied, should remove before apply", overlay_status);
  } else {
    sprintf(overlay_path, TBS_DTBO_PATH, TBS_DTBO_NAME);
    fd_dto = open(overlay_path, O_RDWR);
    if (fd_dto < 0) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not open dto path for writing new overlay");
      return KATCP_RESULT_FAIL;
    }

    wr = write(fd_dto, TBS_DTBO_FILE, sizeof(TBS_DTBO_FILE));
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "new dto applied");
  }

  return KATCP_RESULT_OK;

}

/************************************************************************************************/

int rfdc_status_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  XRFdc_IPStatus ip_status;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  XRFdc_GetIPStatus(rfdc->xrfdc, &ip_status);

  // TODO: will need to be smart between dual-/quad-tile
  unsigned int enabled;
  for (int i = 0; i < NUM_TILES; i++) {
    enabled = ip_status.ADCTileStatus[i].IsEnabled;

    // TODO: should just append_args w/ FLAG_STRING, without LAST
    if (enabled==1) {
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "ADC%d: Enabled %d, State %d, PLL %d", i,
                 ip_status.ADCTileStatus[i].IsEnabled,
                 ip_status.ADCTileStatus[i].TileState,
                 ip_status.ADCTileStatus[i].PLLState);
    } else {
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "ADC%d: Enabled 0", i);
    }
  }

  // TODO: should just append_args w/ FLAG_STRING, without LAST
  for (int i=0; i<NUM_TILES; i++) {
    enabled = ip_status.DACTileStatus[i].IsEnabled;

    if (enabled==1) {
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "DAC%d: Enabled %d, State %d, PLL %d", i,
                 ip_status.DACTileStatus[i].IsEnabled,
                 ip_status.DACTileStatus[i].TileState,
                 ip_status.DACTileStatus[i].PLLState);
    } else {
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "DAC%d: Enabled 0", i);
    }
  }

  return KATCP_RESULT_OK;
}

int rfdc_shutdown_tile_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile;
  char* type;
  unsigned int converter_type;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile and converter type
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 2);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckTileEnabled(rfdc->xrfdc, converter_type, tile) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  result = XRFdc_Shutdown(rfdc->xrfdc, XRFDC_ADC_TILE, tile);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "tile shutdown fail, this should not happen, fatal error");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int rfdc_startup_tile_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile;
  char* type;
  unsigned int converter_type;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile and converter type
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 2);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckTileEnabled(rfdc->xrfdc, converter_type, tile) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  result = XRFdc_StartUp(rfdc->xrfdc, XRFDC_ADC_TILE, tile);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "tile sartup fail, this should not happen, fatal error");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int rfdc_get_block_status_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  char* type;
  unsigned int converter_type;
  XRFdc_BlockStatus blk_stat;
  unsigned int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if (tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired converter type
  if (argc < 4) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify tile idx, block idx, and converter type");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "getting block status for %s tile:%d blk:%d", type, tile, blk);

  if (!XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%s: tile %d, blk %d, disabled", type, tile, blk);
  } else {
    result = XRFdc_GetBlockStatus(rfdc->xrfdc, converter_type, tile, blk, &blk_stat);
    if (result != XRFDC_SUCCESS) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get output current");
      return KATCP_RESULT_FAIL;
    }

    // format and send status
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "SamplingFreq %f, AnalogDataPathStatus %u, DigitalDataPathStatus %u, DataPathClocksStatus %hu, "
        "IsFIFOFlagsEnabled %hu, IsFIFOFlagsAsserted %hu",
        blk_stat.SamplingFreq, blk_stat.AnalogDataPathStatus, blk_stat.DigitalDataPathStatus, blk_stat.DataPathClocksStatus,
        blk_stat.IsFIFOFlagsEnabled, blk_stat.IsFIFOFlagsAsserted);
  }

  return KATCP_RESULT_OK;
}

/*
  ?rfdc-run-mts tile-mask TODO: could add a [verbose] option for report
    execute multi-tile synchronization for the selected tiles indicated by
    `tile-mask`. Inform with verbose dump of mts status.
*/
int rfdc_run_mts_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tilemask;
  unsigned int factor;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile mask
  if (argc < 2) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify tile mask for tiles to sync");
    return KATCP_RESULT_INVALID;
  }

  tilemask = arg_unsigned_long_katcp(d, 1);
  if (tilemask > 0xf) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile mask must be in range (0-%d)", 0xf);
    return KATCP_RESULT_INVALID;
  }

  // run mts, report results
  int tmp = XRFDC_MTS_DAC_MARKER_LOC_MASK(3);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "macro defined, result=%d", tmp);
  // TODO: this init of sync config should probably live where rfdc driver is
  // init to allow for calling report mts status
  XRFdc_MultiConverter_Init(&rfdc->sync_config, 0, 0); //pl_codes and t1_codes args set to `0` when not used
  rfdc->sync_config.Tiles = tilemask;
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "tile mask set to: 0x%08x", rfdc->sync_config.Tiles);
  result = XRFdc_MultiConverter_Sync(rfdc->xrfdc, XRFDC_ADC_TILE, &rfdc->sync_config);
  if(result != XRFDC_MTS_OK) {
    extra_response_katcp(d, KATCP_RESULT_FAIL,"mts sync fail, error code 0x%08x", result);
    return KATCP_RESULT_OWN;
  }

  // inform with detailed report TODO: could make report a verbose cmd option
  if(result == XRFDC_MTS_OK) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "mts sync report");
    for(int tile=0; tile<NUM_TILES; tile++) {
      if( (1<<tile) & rfdc->sync_config.Tiles) {
        XRFdc_GetDecimationFactor(rfdc->xrfdc, tile, 0, &factor);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: Latency(T1) = %3d, Adjusted Delay Offset(T%d) = %3d, Marker Delay = %d",
          tile, rfdc->sync_config.Latency[tile], factor,
          rfdc->sync_config.Offset[tile], rfdc->sync_config.Marker_Delay);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: PLL DTC Code = %d", tile, rfdc->sync_config.DTC_Set_PLL.DTC_Code[tile]);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: PLL Num Windows = %d", tile, rfdc->sync_config.DTC_Set_PLL.Num_Windows[tile]);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: PLL Max Gap = %d", tile, rfdc->sync_config.DTC_Set_PLL.Max_Gap[tile]);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: PLL Min Gap = %d", tile, rfdc->sync_config.DTC_Set_PLL.Min_Gap[tile]);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: PLL Max Overlap = %d", tile, rfdc->sync_config.DTC_Set_PLL.Max_Overlap[tile]);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: T1 DTC Code = %d", tile, rfdc->sync_config.DTC_Set_T1.DTC_Code[tile]);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: T1 Num Windows = %d", tile, rfdc->sync_config.DTC_Set_T1.Num_Windows[tile]);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: T1 Max Gap = %d", tile, rfdc->sync_config.DTC_Set_T1.Max_Gap[tile]);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: T1 Min Gap = %d", tile, rfdc->sync_config.DTC_Set_T1.Min_Gap[tile]);
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
          "ADC%d: T1 Max Overlap = %d", tile, rfdc->sync_config.DTC_Set_T1.Max_Overlap[tile]);
      }
    }
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "mts complete");
  }

  return KATCP_RESULT_OK;
}

/*
  ?rfdc-mts-report
*/

int rfdc_mts_report_cmd(struct katcp_dispatch *d, int argc) {

  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int factor;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "mts sync report");
  for(int tile=0; tile<NUM_TILES; tile++) {
    if( (1<<tile) & rfdc->sync_config.Tiles) {
      XRFdc_GetDecimationFactor(rfdc->xrfdc, tile, 0, &factor);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: Latency(T1) = %3d, Adjusted Delay Offset(T%d) = %3d, Marker Delay = %d",
        tile, rfdc->sync_config.Latency[tile], factor,
        rfdc->sync_config.Offset[tile], rfdc->sync_config.Marker_Delay);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: PLL DTC Code = %d", tile, rfdc->sync_config.DTC_Set_PLL.DTC_Code[tile]);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: PLL Num Windows = %d", tile, rfdc->sync_config.DTC_Set_PLL.Num_Windows[tile]);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: PLL Max Gap = %d", tile, rfdc->sync_config.DTC_Set_PLL.Max_Gap[tile]);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: PLL Min Gap = %d", tile, rfdc->sync_config.DTC_Set_PLL.Min_Gap[tile]);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: PLL Max Overlap = %d", tile, rfdc->sync_config.DTC_Set_PLL.Max_Overlap[tile]);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: T1 DTC Code = %d", tile, rfdc->sync_config.DTC_Set_T1.DTC_Code[tile]);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: T1 Num Windows = %d", tile, rfdc->sync_config.DTC_Set_T1.Num_Windows[tile]);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: T1 Max Gap = %d", tile, rfdc->sync_config.DTC_Set_T1.Max_Gap[tile]);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: T1 Min Gap = %d", tile, rfdc->sync_config.DTC_Set_T1.Min_Gap[tile]);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: T1 Max Overlap = %d", tile, rfdc->sync_config.DTC_Set_T1.Max_Overlap[tile]);
    }
  }

  return KATCP_RESULT_OK;
}

/*

  ?rfdc-report-mts-latency
    use informs to report mts tile T1 (sample clock) latency and offsets (units
    of PL word) added to each fifo
*/
int rfdc_report_mts_latency_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  // cmd variables
  unsigned int factor;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // report status
  for(int tile=0; tile<NUM_TILES; tile++) {
    if( (1<<tile) & rfdc->sync_config.Tiles) {
      XRFdc_GetDecimationFactor(rfdc->xrfdc, tile, 0, &factor);
      prepend_inform_katcp(d);
      append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
        "ADC%d: Latency(T1) =%3d, Adjusted Delay Offset(T%d) =%3d",
        tile, rfdc->sync_config.Latency[tile], factor, rfdc->sync_config.Offset[tile]);
    }
  }

  return KATCP_RESULT_OK;
}

/*
  ?rfdc-report-mixer tile-idx blk-idx [adc|dac]
  get mixer information
*/

int rfdc_get_mixer_settings_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  // cmd variables
  int result;
  unsigned int tile, blk;
  XRFdc_Mixer_Settings mixer;
  char* type;
  unsigned int converter_type;

  // TODO: this header code parsing args/setup is the same as update-nco, make function for reuse
  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // use getter to populate mixer settings
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "getting mixer settings for %s: tile:%d blk:%d", type, tile, blk);
  result = XRFdc_GetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get mixer settings");
    return KATCP_RESULT_FAIL;
  }

  // verbosly report info here
  char* mixer_mode;
  switch (mixer.MixerMode) {
    case XRFDC_MIXER_MODE_OFF:
      mixer_mode = "off";
      break;
    case XRFDC_MIXER_MODE_C2C:
      mixer_mode = "c2c";
      break;
    case XRFDC_MIXER_MODE_C2R:
      mixer_mode = "c2r";
      break;
    case XRFDC_MIXER_MODE_R2C:
      mixer_mode = "r2c";
      break;
    case XRFDC_MIXER_MODE_R2R:
      mixer_mode = "r2r";
      break;
    default: mixer_mode = "mixer mode error";
  }

  char* mixer_type;
  switch (mixer.MixerType) {
    case XRFDC_MIXER_TYPE_COARSE:
      mixer_type = "coarse";
      break;
    case XRFDC_MIXER_TYPE_FINE:
      mixer_type = "fine";
      break;
    case XRFDC_MIXER_TYPE_OFF:
      mixer_type = "off";
      break;
    case XRFDC_MIXER_TYPE_DISABLED:
      mixer_type = "disabled";
      break;
    default: mixer_type = "mixer type error";
  }

  char* coarse_mix_freq;
  switch (mixer.CoarseMixFreq) {
    case XRFDC_COARSE_MIX_OFF:
      coarse_mix_freq = "off";
      break;
    case XRFDC_COARSE_MIX_SAMPLE_FREQ_BY_TWO:
      coarse_mix_freq = "fs/2";
      break;
    case XRFDC_COARSE_MIX_SAMPLE_FREQ_BY_FOUR:
      coarse_mix_freq = "fs/4";
      break;
    case XRFDC_COARSE_MIX_MIN_SAMPLE_FREQ_BY_FOUR:
      coarse_mix_freq = "-fs/4";
      break;
    case XRFDC_COARSE_MIX_BYPASS:
      coarse_mix_freq = "bypass";
      break;
    default: coarse_mix_freq = "mixer type error";
  }

  char* fine_mixer_scale;
  switch (mixer.MixerType) {
    case XRFDC_MIXER_SCALE_AUTO:
      fine_mixer_scale = "auto";
      break;
    case XRFDC_MIXER_SCALE_1P0:
      fine_mixer_scale = "1.0";
      break;
    case XRFDC_MIXER_SCALE_0P7:
      fine_mixer_scale = "0.7";
      break;
    default: fine_mixer_scale = "mixer scale type error";
  }

  // verbosly log info
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "mode: %s (%d)", mixer_mode, mixer.MixerMode);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "type: %s (%d)", mixer_type, mixer.MixerType);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "fine freq: %g", mixer.Freq);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "phase offset: %g", mixer.PhaseOffset);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "mixer scale: %s (%d)", fine_mixer_scale, mixer.FineMixerScale);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "coarse freq: %s (%d)", coarse_mix_freq, mixer.CoarseMixFreq);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "event source: %d", mixer.EventSource);

  // return params for client side parsing
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "MixerMode %u, MixerType %u, Freq %g, "
  "PhaseOffset %g, FineMixerScale %u, CoarseMixFreq %u, EventSource %u",
  mixer.MixerMode, mixer.MixerType, mixer.Freq, mixer.PhaseOffset, mixer.FineMixerScale,
  mixer.CoarseMixFreq, mixer.EventSource);

  return KATCP_RESULT_OK;
}

/*
  ?rfdc-update-nco tile-idx blk-idx nco-ghz [adc|dac]
    update complex mixer nco freq

    TODO:
    * for mts/mcs applications the event update needs to be implemented to
      handle that case. Here, just a simple update of the nco freq.
    * this method is for a single adc tile/blk a common use case will be to just
      set for all enabled tile/blk
*/
int rfdc_update_nco_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  // cmd variables
  int result;
  unsigned int tile, blk;
  XRFdc_Mixer_Settings mixer;
  char* type;
  int converter_type;
  double nco_freq;
  double nco_phase;
  unsigned int trigger_update = 1; // defaulat to force update event

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired parameters
  if (argc < 5) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac), freq [phase] [trigger]");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // TODO: validate values, I know the valid values are [-fs/2, fs/2] but irc
  // the driver will internally allow any value but should just have the effect
  // of spectral aliasing
  // get nco freq and phase. If phase not specified default to 0
  nco_freq = arg_double_katcp(d, 4);
  if (argc > 5) {
    nco_phase = arg_double_katcp(d, 5);
  } else {
    nco_phase = 0;
  }
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set nco freq : %.3f MHz", nco_freq);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set nco phase: %.3f deg", nco_phase);

  if (argc > 6) trigger_update = arg_unsigned_long_katcp(d, 6);

  // use getter to populate mixer settings
  result = XRFdc_GetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get mixer settings");
    return KATCP_RESULT_FAIL;
  }

  // change mixer freq settings, set, and call update
  mixer.Freq = nco_freq;
  mixer.PhaseOffset = nco_phase;
  result = XRFdc_SetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to set mixer settings");
    return KATCP_RESULT_FAIL;
  }

  if (trigger_update) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "trigger mixer update event");
    XRFdc_UpdateEvent(rfdc->xrfdc, converter_type, tile, blk, XRFDC_EVENT_MIXER);
  }

  return KATCP_RESULT_OK;
}


int rfdc_set_mixer_mode_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  // cmd variables
  int result;
  unsigned int tile, blk;
  XRFdc_Mixer_Settings mixer;
  char* type;
  unsigned int converter_type;
  unsigned int mixer_mode;
  unsigned int trigger_update = 1; // defaulat to force update event

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired parameters
  if (argc < 5) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac), mixer mode, trigger");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  mixer_mode = arg_unsigned_long_katcp(d, 4);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set mixer mode: %d", mixer_mode);

  if (argc > 5) trigger_update = arg_unsigned_long_katcp(d, 5);

  // use getter to populate mixer settings
  result = XRFdc_GetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get mixer settings");
    return KATCP_RESULT_FAIL;
  }

  // change mixer mode setting, set, and call update
  mixer.MixerMode = mixer_mode;
  result = XRFdc_SetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to set mixer settings");
    return KATCP_RESULT_FAIL;
  }

  if (trigger_update) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "trigger mixer update event");
    XRFdc_UpdateEvent(rfdc->xrfdc, converter_type, tile, blk, XRFDC_EVENT_MIXER);
  }

  return KATCP_RESULT_OK;
}


int rfdc_set_mixer_type_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  // cmd variables
  int result;
  unsigned int tile, blk;
  XRFdc_Mixer_Settings mixer;
  char* type;
  unsigned int converter_type;
  unsigned int mixer_type;
  unsigned int trigger_update = 1; // defaulat to force update event

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired parameters
  if (argc < 5) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac), mixer type, trigger");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  mixer_type = arg_unsigned_long_katcp(d, 4);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set mixer type: %d", mixer_type);

  if (argc > 5) trigger_update = arg_unsigned_long_katcp(d, 5);

  // use getter to populate mixer settings
  result = XRFdc_GetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get mixer settings");
    return KATCP_RESULT_FAIL;
  }

  // if poking the coarse mixer default to fs/4
  if (mixer_type==XRFDC_MIXER_TYPE_COARSE && mixer.CoarseMixFreq==XRFDC_COARSE_MIX_OFF) {
    mixer.CoarseMixFreq = XRFDC_COARSE_MIX_SAMPLE_FREQ_BY_FOUR;
  }

  // change mixer type setting, set, and call update
  mixer.MixerType = mixer_type;
  result = XRFdc_SetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to set mixer settings");
    return KATCP_RESULT_FAIL;
  }

  if (trigger_update) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "trigger mixer update event");
    XRFdc_UpdateEvent(rfdc->xrfdc, converter_type, tile, blk, XRFDC_EVENT_MIXER);
  }

  return KATCP_RESULT_OK;
}


int rfdc_set_coarse_mixer_freq_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  // cmd variables
  int result;
  unsigned int tile, blk;
  XRFdc_Mixer_Settings mixer;
  char* type;
  unsigned int converter_type;
  unsigned int coarse_mixer_freq;
  unsigned int trigger_update = 1; // defaulat to force update event

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired parameters
  if (argc < 5) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac), coarse mixer freq, trigger");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  coarse_mixer_freq = arg_double_katcp(d, 4);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set coarse mixer freq: %d", coarse_mixer_freq);

  if (argc > 5) trigger_update = arg_unsigned_long_katcp(d, 5);

  // use getter to populate mixer settings
  result = XRFdc_GetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get mixer settings");
    return KATCP_RESULT_FAIL;
  }

  // change mixer type setting, set, and call update
  mixer.CoarseMixFreq = coarse_mixer_freq;
  result = XRFdc_SetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to set mixer settings");
    return KATCP_RESULT_FAIL;
  }

  if (trigger_update) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "trigger mixer update event");
    XRFdc_UpdateEvent(rfdc->xrfdc, converter_type, tile, blk, XRFDC_EVENT_MIXER);
  }

  return KATCP_RESULT_OK;
}


int rfdc_set_mixer_scale_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  // cmd variables
  int result;
  unsigned int tile, blk;
  XRFdc_Mixer_Settings mixer;
  char* type;
  unsigned int converter_type;
  unsigned int mixer_scale;
  unsigned int trigger_update = 1; // defaulat to force update event

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired parameters
  if (argc < 5) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac), mixer scale, trigger");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  mixer_scale = arg_unsigned_long_katcp(d, 4);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set mixer scale: %d", mixer_scale);

  if (argc > 5) trigger_update = arg_unsigned_long_katcp(d, 5);

  // use getter to populate mixer settings
  result = XRFdc_GetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get mixer settings");
    return KATCP_RESULT_FAIL;
  }

  // change mixer type setting, set, and call update
  mixer.FineMixerScale = mixer_scale;
  result = XRFdc_SetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to set mixer settings");
    return KATCP_RESULT_FAIL;
  }

  if (trigger_update) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "trigger mixer update event");
    XRFdc_UpdateEvent(rfdc->xrfdc, converter_type, tile, blk, XRFDC_EVENT_MIXER);
  }

  return KATCP_RESULT_OK;
}

int rfdc_set_mixer_event_source_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  // cmd variables
  int result;
  unsigned int tile, blk;
  XRFdc_Mixer_Settings mixer;
  char* type;
  unsigned int converter_type;
  unsigned int event_source;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired parameters
  if (argc < 5) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac), event source");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  event_source = arg_double_katcp(d, 4);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set mixer event source: %d", event_source);

  // use getter to populate mixer settings
  result = XRFdc_GetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get mixer settings");
    return KATCP_RESULT_FAIL;
  }

  // change mixer type setting, set, and call update
  mixer.EventSource = event_source;
  result = XRFdc_SetMixerSettings(rfdc->xrfdc, converter_type, tile, blk, &mixer);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to set mixer settings");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

/************************************************************************************************/

int rfdc_get_dsa_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  XRFdc_DSA_Settings dsa;
  int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // check that DSA is supported
  if (rfdc->xrfdc->RFdc_Config.IPType < XRFDC_GEN3) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DSA only supported on Gen 3 devices");
    return KATCP_RESULT_INVALID;
  }

  // parse target adc tile and block
  if (argc < 3) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3) and adc block idx (0-3)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get dsa for converter
  result = XRFdc_GetDSA(rfdc->xrfdc, tile, blk, &dsa);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get dsa");
    return KATCP_RESULT_FAIL;
  }
  // format and send status
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "dsa %1.f", dsa.Attenuation);

  return KATCP_RESULT_OK;
}

int rfdc_get_dsa_all_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  XRFdc_DSA_Settings dsa;
  int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // check that DSA is supported
  if (rfdc->xrfdc->RFdc_Config.IPType < XRFDC_GEN3) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DSA only supported on Gen 3 devices");
    return KATCP_RESULT_INVALID;
  }

  // get dsa for all converters
  // TODO: note that in this way the dsa info is sent back appended as multiple
  // args in the same inform rather than multiple informs in a single reply.
  // This was part of testing what is available on th tbs side as a way for a
  // client (casperfpga) to parse work with response.
  prepend_inform_katcp(d);
  for (int tile = 0; tile < NUM_TILES; tile++) {
    for (int blk = 0; blk < NUM_BLKS; blk++) {
      if (XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
        result = XRFdc_GetDSA(rfdc->xrfdc, tile, blk, &dsa);
        if (result != XRFDC_SUCCESS) {
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get dsa");
          return KATCP_RESULT_FAIL;
        }
        //prepend_inform_katcp(d);
        //append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "adc: tile %u, blk %u, dsa %1.f", tile, blk, dsa.Attenuation);
        //unsigned int katcp_flags = (tile==NUM_TILES-1 && blk==NUM_BLKS-1) ? KATCP_FLAG_STRING|KATCP_FLAG_LAST : KATCP_FLAG_STRING;
        append_args_katcp(d, KATCP_FLAG_STRING, "adc: tile %u, blk %u, dsa %1.f", tile, blk, dsa.Attenuation);
      } else {
        //prepend_inform_katcp(d);
        //append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "adc: tile %u, blk %u, (disabled)", tile, blk);
        //unsigned int katcp_flags = (tile==NUM_TILES-1 && blk==NUM_BLKS-1) ? KATCP_FLAG_STRING|KATCP_FLAG_LAST : KATCP_FLAG_STRING;
        append_args_katcp(d, KATCP_FLAG_STRING, "adc: tile %u, blk %u, (disabled)", tile, blk);
      }
    }
  }
  append_end_katcp(d);

  return KATCP_RESULT_OK;
}

/*
 * (?rfdc-set-dsa adc-tile adc-blk dsa-in-dB)
 *
 *
 */
int rfdc_set_dsa_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  float atten;
  float max_atten, min_atten;
  unsigned int tile, blk;
  XRFdc_DSA_Settings dsa;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // check that DSA is supported
  if (rfdc->xrfdc->RFdc_Config.IPType < XRFDC_GEN3) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "DSA only supported on Gen 3 devices");
    return KATCP_RESULT_INVALID;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3), adc block idx, and desired atten in dB");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  atten = arg_double_katcp(d, 3);

  // ES1 silicon has max atten of 11 dB in 0.5 dB steps, production silicon supports up to 27 dB in 1 dB steps
  max_atten = XRFDC_MAX_ATTEN(rfdc->xrfdc->RFdc_Config.SiRevision);
  min_atten = 0;
  if (atten > max_atten) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid attenuation setting %.1f, max supported atten is %.1f dB",
      atten, max_atten);
    return KATCP_RESULT_INVALID;
  }

  if (atten < min_atten) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid attenuation setting -%.1f, min supported atten is %.1f dB",
      atten, min_atten);
    return KATCP_RESULT_INVALID;
  }

  // set dsa
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set adc dsa tile: %d, blk: %d to %.1f dB", tile, blk, atten);
  dsa.Attenuation = atten;
  result = XRFdc_SetDSA(rfdc->xrfdc, tile, blk, &dsa);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set dsa");
    return KATCP_RESULT_FAIL;
  }

  // read back, format and send attenuation status
  result = XRFdc_GetDSA(rfdc->xrfdc, tile, blk, &dsa);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to read dsa after write");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "dsa %1.f", dsa.Attenuation);

  return KATCP_RESULT_OK;
}

/************************************************************************************************/

int rfdc_get_fabrdvldwords_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  char* type;
  int converter_type;
  unsigned int fab_rdvld_words;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and converter type
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), blk idx (0-3), and converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get converter fab rdvld words
  result = XRFdc_GetFabRdVldWords(rfdc->xrfdc, converter_type, tile, blk, &fab_rdvld_words);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get fab rdvld words");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%u", fab_rdvld_words);

  return KATCP_RESULT_OK;
}

int rfdc_get_fabwrvldwords_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  char* type;
  int converter_type;
  unsigned int fab_wrvld_words;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and converter type
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), blk idx (0-3), and converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get converter fab wrvld words
  result = XRFdc_GetFabWrVldWords(rfdc->xrfdc, converter_type, tile, blk, &fab_wrvld_words);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get fab wrvld words");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%u", fab_wrvld_words);

  return KATCP_RESULT_OK;
}

int rfdc_get_fabclkdiv_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile;
  char* type;
  int converter_type;
  short unsigned int fab_clk_div;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3) and converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 2);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckTileEnabled(rfdc->xrfdc, converter_type, tile) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get converter fabric clk out divider
  result = XRFdc_GetFabClkOutDiv(rfdc->xrfdc, converter_type, tile, &fab_clk_div);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get fab clkout div");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%u", fab_clk_div);

  return KATCP_RESULT_OK;
}

int rfdc_set_fabclkdiv_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile;
  char* type;
  int converter_type;
  short unsigned int fab_clk_div;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3) and converter type (adc|dac) fabclk div");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 2);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckTileEnabled(rfdc->xrfdc, converter_type, tile) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  fab_clk_div = arg_unsigned_long_katcp(d, 3);

  // set converter tile pl clk rate, format and send info
  result = XRFdc_SetFabClkOutDiv(rfdc->xrfdc, converter_type, tile, fab_clk_div);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set fab clk div out");
    return KATCP_RESULT_FAIL;
  }

  // readback and send info
  result = XRFdc_GetFabClkOutDiv(rfdc->xrfdc, converter_type, tile, &fab_clk_div);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get fab clkout div");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%u", fab_clk_div);

  return KATCP_RESULT_OK;
}

int rfdc_get_fabclkfreq_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile;
  char* type;
  int converter_type;
  double pl_clk_freq;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3) and converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 2);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckTileEnabled(rfdc->xrfdc, converter_type, tile) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get converter tile pl clk rate, format and send info
  pl_clk_freq = XRFdc_GetFabClkFreq(rfdc->xrfdc, converter_type, tile);
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%g", pl_clk_freq);

  return KATCP_RESULT_OK;
}

// 0 real, 1 complex
int rfdc_get_datatype_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  char* type;
  int converter_type;
  unsigned int pl_datatype;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and converter type
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), blk idx (0-3), and converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get converter tile data type, format and send info
  pl_datatype = XRFdc_GetDataType(rfdc->xrfdc, converter_type, tile, blk);
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%u", pl_datatype);

  return KATCP_RESULT_OK;
}

int rfdc_get_datawidth_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  char* type;
  int converter_type;
  unsigned int pl_datawidth;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired attenuation parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), blk idx (0-3), and converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get converter tile data type, format and send info
  pl_datawidth = XRFdc_GetDataWidth(rfdc->xrfdc, converter_type, tile, blk);
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%u", pl_datawidth);

  return KATCP_RESULT_OK;
}

// XRFDC_ODD_NYQUIST_ZONE 0x1U
// XRFDC_EVEN_NYQUIST_ZONE 0x2U
int rfdc_get_nyquist_zone_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  char* type;
  unsigned int converter_type;
  unsigned int nyquist_zone;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse tile, block and desired attenuation parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get converter nyquist zone value
  result = XRFdc_GetNyquistZone(rfdc->xrfdc, converter_type, tile, blk, &nyquist_zone);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get nyquist zone");
    return KATCP_RESULT_FAIL;
  }

  // format and send nyquist zone info
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "NyquistZone %u", nyquist_zone);

  return KATCP_RESULT_OK;
}

int rfdc_set_nyquist_zone_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  char* type;
  unsigned int converter_type;
  unsigned int nyquist_zone;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 5) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac), nyquist zone (1|2)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // parse nyquist zone value
  nyquist_zone = arg_unsigned_long_katcp(d, 4);
  if (nyquist_zone != XRFDC_ODD_NYQUIST_ZONE && nyquist_zone != XRFDC_EVEN_NYQUIST_ZONE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid nyquist zone setting");
    return KATCP_RESULT_FAIL;
  }

  // set nyquist zone for converter
  result = XRFdc_SetNyquistZone(rfdc->xrfdc, converter_type, tile, blk, nyquist_zone);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set nyquist zone");
    return KATCP_RESULT_FAIL;
  }

  // readback and send nyquist zone info
  result = XRFdc_GetNyquistZone(rfdc->xrfdc, converter_type, tile, blk, &nyquist_zone);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get nyquist zone");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "NyquistZone %u", nyquist_zone);

  return KATCP_RESULT_OK;
}

int rfdc_get_coarse_delay_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  char* type;
  unsigned int converter_type;
  XRFdc_CoarseDelay_Settings coarse_delay;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get coarse delay for converters
  result = XRFdc_GetCoarseDelaySettings(rfdc->xrfdc, converter_type, tile, blk, &coarse_delay);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get coarse delay settings");
    return KATCP_RESULT_FAIL;
  }

  // format and send nyquist zone info
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "CoarseDelay %u, EventSource %u",
    coarse_delay.CoarseDelay, coarse_delay.EventSource);

  return KATCP_RESULT_OK;
}

int rfdc_set_coarse_delay_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  char* type;
  unsigned int converter_type;
  XRFdc_CoarseDelay_Settings coarse_delay;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 6) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac), coarse delay, update source");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // parse user coarse delay settings for converters
  coarse_delay.CoarseDelay = arg_unsigned_long_katcp(d, 4);
  coarse_delay.EventSource = arg_unsigned_long_katcp(d, 5);
  result = XRFdc_SetCoarseDelaySettings(rfdc->xrfdc, converter_type, tile, blk, &coarse_delay);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set coarse delay settings");
    return KATCP_RESULT_FAIL;
  }

  // readback delay settings, format and send info
  result = XRFdc_GetCoarseDelaySettings(rfdc->xrfdc, converter_type, tile, blk, &coarse_delay);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to readback coarse delay settings");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "CoarseDelay %u, EventSource %u",
    coarse_delay.CoarseDelay, coarse_delay.EventSource);

  return KATCP_RESULT_OK;
}

int rfdc_get_qmc_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  char* type;
  unsigned int converter_type;
  XRFdc_QMC_Settings qmc;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get qmc settings for the converter
  result = XRFdc_GetQMCSettings(rfdc->xrfdc, converter_type, tile, blk, &qmc);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get coarse delay settings");
    return KATCP_RESULT_FAIL;
  }

  // format and send nyquist zone info
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "EnablePhase %u, EnableGain %u, "
  "GainCorrectionFactor %g, PhaseCorrectionFactor %g, OffsetCorrectionFactor %d, EventSource %u",
  qmc.EnablePhase, qmc.EnableGain, qmc.GainCorrectionFactor, qmc.PhaseCorrectionFactor,
  qmc.OffsetCorrectionFactor, qmc.EventSource);

  return KATCP_RESULT_OK;
}

int rfdc_set_qmc_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  char* type;
  unsigned int converter_type;
  XRFdc_QMC_Settings qmc;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 10) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), converter type (adc|dac), qmc parameters");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // parse user qmc settings
  qmc.EnablePhase = arg_unsigned_long_katcp(d, 4);
  qmc.EnableGain = arg_unsigned_long_katcp(d, 5);
  qmc.GainCorrectionFactor = arg_double_katcp(d, 6);
  qmc.PhaseCorrectionFactor = arg_double_katcp(d, 7);
  qmc.OffsetCorrectionFactor = arg_unsigned_long_katcp(d, 8);
  qmc.EventSource = arg_unsigned_long_katcp(d, 9);

  // set qmc settings for the converter
  result = XRFdc_SetQMCSettings(rfdc->xrfdc, converter_type, tile, blk, &qmc);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set qmc settings");
    return KATCP_RESULT_FAIL;
  }

  result = XRFdc_GetQMCSettings(rfdc->xrfdc, converter_type, tile, blk, &qmc);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to readback qmc settings");
    return KATCP_RESULT_FAIL;
  }
  // readback, format and send qmc settings
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "EnablePhase %u, EnableGain %u, "
  "GainCorrectionFactor %g, PhaseCorrectionFactor %g, OffsetCorrectionFactor %d, EventSource %u",
  qmc.EnablePhase, qmc.EnableGain, qmc.GainCorrectionFactor, qmc.PhaseCorrectionFactor,
  qmc.OffsetCorrectionFactor, qmc.EventSource);

  return KATCP_RESULT_OK;
}

int rfdc_get_pll_config_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  char* type;
  unsigned int converter_type;
  unsigned int tile;
  XRFdc_PLL_Settings pll;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse converter tile, block, and parameters
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 2);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  // check if converter tile available
  if (XRFdc_CheckTileEnabled(rfdc->xrfdc, converter_type, tile) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get PLL config, format and send info
  memset(&pll, 0, sizeof(pll));
  result = XRFdc_GetPLLConfig(rfdc->xrfdc, converter_type, tile, &pll);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get pll configuration info");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "Enabled %u, RefClkFreq %g, SampleRate %g, "
  "RefClkDivider %u, FeedbackDivider %u, OutputDivider %u",
  pll.RefClkFreq, pll.SampleRate, pll.RefClkDivider, pll.FeedbackDivider, pll.OutputDivider);

  return KATCP_RESULT_OK;
}

int rfdc_dynamic_pll_config_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  char* type;
  unsigned int converter_type;
  unsigned int tile;
  unsigned char clk_source;
  double min_sample_rate, max_sample_rate;
  double refclkfreq, sampling_rate;
  //XRFdc_PLL_Settings pll;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse converter tile, block, and parameters
  if (argc < 6) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), converter type (adc|dac), clk source, ref freq., sampling freq.");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 2);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  // check if converter tile available
  if (XRFdc_CheckTileEnabled(rfdc->xrfdc, converter_type, tile) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // parse clk source, reference and sampling freq.
  clk_source = arg_unsigned_long_katcp(d, 3);
  if (clk_source != XRFDC_EXTERNAL_CLK && clk_source != XRFDC_INTERNAL_PLL_CLK) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid converter clock source setting");
    return KATCP_RESULT_FAIL;
  }

  refclkfreq = arg_double_katcp(d, 4);
  if (clk_source==XRFDC_INTERNAL_PLL_CLK) {
    if (refclkfreq<XRFDC_REFFREQ_MIN || refclkfreq>XRFDC_REFFREQ_MAX) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "requested pll rate out of bounds (%g-%g) MHz", XRFDC_REFFREQ_MIN, XRFDC_REFFREQ_MAX);
      return KATCP_RESULT_FAIL;
    }
  }

  sampling_rate = arg_double_katcp(d, 5);
  XRFdc_GetMinSampleRate(rfdc->xrfdc, converter_type, tile, &min_sample_rate);
  XRFdc_GetMaxSampleRate(rfdc->xrfdc, converter_type, tile, &max_sample_rate);
  if (sampling_rate < min_sample_rate || sampling_rate > max_sample_rate) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "requested sample rate out of bounds (%g-%g) MHz", min_sample_rate, max_sample_rate);
    return KATCP_RESULT_FAIL;
  }

  // dynamically configure PLL settings
  result = XRFdc_DynamicPLLConfig(rfdc->xrfdc, converter_type, tile, clk_source, refclkfreq, sampling_rate);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set pll configuration");
    return KATCP_RESULT_FAIL;
  }

  // TODO: readback PLL config, format and send info
  // would need to use GetClockSource to readback change to clock source
  //memset(&pll, 0, sizeof(pll));
  //result = XRFdc_GetPLLConfig(rfdc->xrfdc, converter_type, tile, blk, &pll);
  //if (result != XRFDC_SUCCESS)
  //  log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to readback pll configuration info");
  //  return KATCP_RESULT_FAIL;
  //}
  //prepend_inform_katcp(d);
  //append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "RefClkFreq %g, SampleRate %g", pll.RefClkFreq, pll.SampleRate);

  return KATCP_RESULT_OK;
}

// XRFDC_PLL_UNLOCKED 0x1U
// XRFDC_PLL_LOCKED   0x2U
int rfdc_pll_lock_status_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  char* type;
  unsigned int converter_type;
  unsigned int tile;
  unsigned int lock_status;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse converter tile, block, and parameters
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 2);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  // check if converter tile available
  if (XRFdc_CheckTileEnabled(rfdc->xrfdc, converter_type, tile) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get pll lock status, format and send info
  result = XRFdc_GetPLLLockStatus(rfdc->xrfdc, converter_type, tile, &lock_status);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get pll lock status");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "LockStatus %u", lock_status);

  return KATCP_RESULT_OK;
}

// XRFDC_EXTERNAL_CLK     0x0U
// XRFDC_INTERNAL_PLL_CLK 0x1U
int rfdc_get_clk_src_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  char* type;
  unsigned int converter_type;
  unsigned int tile;
  unsigned int clk_src;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse converter tile, block, and parameters
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), converter type (adc|dac)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 2);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  // check if converter tile available
  if (XRFdc_CheckTileEnabled(rfdc->xrfdc, converter_type, tile) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get clock source, format and send info
  result = XRFdc_GetClockSource(rfdc->xrfdc, converter_type, tile, &clk_src);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get clock source info");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "ClockSource %u", clk_src);

  return KATCP_RESULT_OK;
}

int rfdc_get_cal_freeze_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  XRFdc_Cal_Freeze_Settings cal_freeze;
  int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // todo: rfdc driver has a built-in `isready` to indicate driver
  // initialization. should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse target adc tile and block
  if (argc < 3) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3) and adc block idx (0-3)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get cal freeze for converter
  result = XRFdc_GetCalFreeze(rfdc->xrfdc, tile, blk, &cal_freeze);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get cal freeze info");
    return KATCP_RESULT_FAIL;
  }
  // format and send status
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "CalFrozen %u, DisableFreezePin %u, FreezeCalibration %u",
    cal_freeze.CalFrozen, cal_freeze.DisableFreezePin, cal_freeze.FreezeCalibration);
  return KATCP_RESULT_OK;
}

/*
 * (?rfdc-set-cal-freeze adc-tile adc-blk freeze)
 *  XRFDC_CAL_UNFREEZE_CALIB 0U
 *  XRFDC_CAL_FREEZE_CALIB   1U
 */
int rfdc_set_cal_freeze_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int disable_freeze_pin = 0;
  unsigned int freeze_calibration;
  unsigned int tile, blk;
  XRFdc_Cal_Freeze_Settings cal_freeze;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3), adc block idx, and cal freeze setting");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if tile is enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get freeze calibration value
  freeze_calibration = arg_unsigned_long_katcp(d, 3);
  if (freeze_calibration!=XRFDC_CAL_FREEZE_CALIB && freeze_calibration!=XRFDC_CAL_UNFREEZE_CALIB) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid cal freeze setting %u, must be 0 (unfreeze), 1 (freeze)");
    return KATCP_RESULT_INVALID;
  }

  // set cal freeze settings
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set cal freeze settings tile: %u, blk: %u to %u", tile, blk, freeze_calibration);
  cal_freeze.DisableFreezePin = disable_freeze_pin;
  cal_freeze.FreezeCalibration = freeze_calibration;
  result = XRFdc_SetCalFreeze(rfdc->xrfdc, tile, blk, &cal_freeze);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set cal freeze settings");
    return KATCP_RESULT_FAIL;
  }

  // read back, format and send freze status
  result = XRFdc_GetCalFreeze(rfdc->xrfdc, tile, blk, &cal_freeze);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get cal freeze info");
    return KATCP_RESULT_FAIL;
  }
  // format and send status
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "CalFrozen %u, DisableFreezePin %u, FreezeCalibration %u",
    cal_freeze.CalFrozen, cal_freeze.DisableFreezePin, cal_freeze.FreezeCalibration);

  return KATCP_RESULT_OK;
}

int rfdc_get_cal_coeffs_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  unsigned int cal_block;
  XRFdc_Calibration_Coefficients cal_coeffs;
  int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // todo: rfdc driver has a built-in `isready` to indicate driver
  // initialization. should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse target adc tile and block
  if (argc < 4) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3) and adc block idx (0-3) and cal block id");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get calibration block id
  cal_block = arg_unsigned_long_katcp(d, 3);
  if (cal_block < 0 || cal_block > 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid valid cal block %u, must be in range 0-3");
    return KATCP_RESULT_INVALID;
  }

  // OCB1 block only available on gen3 devices
  if (cal_block==XRFDC_CAL_BLOCK_OCB1 && rfdc->xrfdc->RFdc_Config.IPType!=XRFDC_GEN3) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "OCB1 cal block only supported on Gen 3 devices");
    return KATCP_RESULT_INVALID;
  }

  // get calibration coeffs
  memset(&cal_coeffs, 0, sizeof(cal_coeffs));
  result = XRFdc_GetCalCoefficients(rfdc->xrfdc, tile, blk, cal_block, &cal_coeffs);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get cal coeff for block id %u", cal_block);
    return KATCP_RESULT_FAIL;
  }
  // format and send status
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST,
    "Coeff0 %u, Coeff1 %u, Coeff2 %u, Coeff3 %u, "
    "Coeff4 %u, Coeff5 %u, Coeff6 %u, Coeff7 %u",
    cal_coeffs.Coeff0, cal_coeffs.Coeff1, cal_coeffs.Coeff2, cal_coeffs.Coeff3,
    cal_coeffs.Coeff4, cal_coeffs.Coeff5, cal_coeffs.Coeff6, cal_coeffs.Coeff7);

  return KATCP_RESULT_OK;
}

int rfdc_set_cal_coeffs_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  unsigned int cal_block;
  XRFdc_Calibration_Coefficients cal_coeffs;
  int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // todo: rfdc driver has a built-in `isready` to indicate driver
  // initialization. should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse target adc tile and block
  if (argc < 12) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3), block idx (0-3), cal block id, and 8 coeffs");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get calibration block id
  cal_block = arg_unsigned_long_katcp(d, 3);
  if (cal_block < 0 || cal_block > 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid valid cal block %u, must be in range 0-3");
    return KATCP_RESULT_INVALID;
  }

  // OCB1 block only available on gen3 devices
  if (cal_block==XRFDC_CAL_BLOCK_OCB1 && rfdc->xrfdc->RFdc_Config.IPType!=XRFDC_GEN3) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "OCB1 cal block only supported on Gen 3 devices");
    return KATCP_RESULT_INVALID;
  }

  // parse user calibration coeffs
  cal_coeffs.Coeff0 = arg_unsigned_long_katcp(d, 4);
  cal_coeffs.Coeff1 = arg_unsigned_long_katcp(d, 5);
  cal_coeffs.Coeff2 = arg_unsigned_long_katcp(d, 6);
  cal_coeffs.Coeff3 = arg_unsigned_long_katcp(d, 7);
  cal_coeffs.Coeff4 = arg_unsigned_long_katcp(d, 8);
  cal_coeffs.Coeff5 = arg_unsigned_long_katcp(d, 9);
  cal_coeffs.Coeff6 = arg_unsigned_long_katcp(d, 10);
  cal_coeffs.Coeff7 = arg_unsigned_long_katcp(d, 11);

  // set calibration coeffs
  result = XRFdc_SetCalCoefficients(rfdc->xrfdc, tile, blk, cal_block, &cal_coeffs);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get cal coeff for block id %u", cal_block);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int rfdc_disable_cal_override_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  unsigned int cal_block;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3), adc block idx, and cal block id");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if tile is enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get calibration block id
  cal_block = arg_unsigned_long_katcp(d, 3);
  if (cal_block < 0 || cal_block > 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid valid cal block %u, must be in range 0-3");
    return KATCP_RESULT_INVALID;
  }

  // OCB1 block only available on gen3 devices
  if (cal_block==XRFDC_CAL_BLOCK_OCB1 && rfdc->xrfdc->RFdc_Config.IPType!=XRFDC_GEN3) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "OCB1 cal block only supported on Gen 3 devices");
    return KATCP_RESULT_INVALID;
  }

  // disable calibration coefficients set by the user
  result = XRFdc_DisableCoefficientsOverride(rfdc->xrfdc, tile, blk, cal_block);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to disable user calibration coeffs");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int rfdc_get_cal_mode_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  unsigned char cal_mode;
  int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // todo: rfdc driver has a built-in `isready` to indicate driver
  // initialization. should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse target adc tile and block
  if (argc < 3) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3) and adc block idx (0-3)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get cal mode for converter
  result = XRFdc_GetCalibrationMode(rfdc->xrfdc, tile, blk, &cal_mode);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get cal mode info");
    return KATCP_RESULT_FAIL;
  }
  // format and send status
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "CalibrationMode %u", cal_mode);

  return KATCP_RESULT_OK;
}

int rfdc_set_cal_mode_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  unsigned char cal_mode;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3), adc block idx, and cal mode (1 or 2)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if tile is enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get requested calibration mode
  cal_mode = arg_unsigned_long_katcp(d, 3);
  if (cal_mode > 2 || cal_mode < 1) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid cal mode setting %u, must be 1 (mode 1), 2 (mode 1)");
    return KATCP_RESULT_INVALID;
  }

  // set cal mode
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set cal mode tile: %u, blk: %u to %u", tile, blk, cal_mode);
  result = XRFdc_SetCalibrationMode(rfdc->xrfdc, tile, blk, cal_mode);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set cal freeze settings");
    return KATCP_RESULT_FAIL;
  }

  // must shutdown and startup tile to apply new calibration mode
  result = XRFdc_Shutdown(rfdc->xrfdc, XRFDC_ADC_TILE, tile);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "tile shutdown fail, this should not happen, fatal error");
    return KATCP_RESULT_FAIL;
  }

  result = XRFdc_StartUp(rfdc->xrfdc, XRFDC_ADC_TILE, tile);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "tile sartup fail, this should not happen, fatal error");
    return KATCP_RESULT_FAIL;
  }

  // read back, format and send mode info
  result = XRFdc_GetCalibrationMode(rfdc->xrfdc, tile, blk, &cal_mode);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get cal mode info");
    return KATCP_RESULT_FAIL;
  }
  // format and send status
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "CalibrationMode %u", cal_mode);

  return KATCP_RESULT_OK;
}

// TODO: Over/under threshold signals are sent to the PL. These need to be added
// to the PL interface for this software driver to do anything meaningful.
// Threshold number update macro's
// XRFDC_UPDATE_THRESHOLD_0 0x1U
// XRFDC_UPDATE_THRESHOLD_1 0x2U
// XRFDC_UPDATE_THRESHOLD_BOTH 0x4U
int rfdc_get_thresh_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  XRFdc_Threshold_Settings threshold;
  int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // todo: rfdc driver has a built-in `isready` to indicate driver
  // initialization. should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse target adc tile and block
  if (argc < 3) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3) and adc block idx (0-3)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if requested adc tile/blk enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get threshold settings for converter
  memset(&threshold, 0, sizeof(threshold));
  result = XRFdc_GetThresholdSettings(rfdc->xrfdc, tile, blk, &threshold);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get cal threshold info");
    return KATCP_RESULT_FAIL;
  }
  // format and send status
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "UpdateThreshold %u, ThresholdMode0 %u, ThresholdMode1 %u, "
    "ThresholdAvgVal0 %u, ThresholdAvgVal1 %u, ThresholdUnderVal0 %u, ThresholdUnderVal1 %u, "
    "ThresholdOverVal0 %u, ThresholdOverVal1 %u",
    threshold.UpdateThreshold, threshold.ThresholdMode[0], threshold.ThresholdMode[1],
    threshold.ThresholdAvgVal[0], threshold.ThresholdAvgVal[1], threshold.ThresholdUnderVal[0], threshold.ThresholdUnderVal[1],
    threshold.ThresholdOverVal[0], threshold.ThresholdOverVal[1]);
  return KATCP_RESULT_OK;
}

int rfdc_set_thresh_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  XRFdc_Threshold_Settings threshold;
  int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // todo: rfdc driver has a built-in `isready` to indicate driver
  // initialization. should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse target adc tile and block
  if (argc < 12) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify adc tile idx (0-3), adc block idx (0-3), and threshold settings value");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "adc block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if requested adc tile/blk enabled
  if (!XRFdc_IsADCBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // parse user threshold settings
  // entry 0 is for thershold0 flag, and entry 1 is for thereshold1 flag. The
  // two flags operate independently of eachother.
  memset(&threshold, 0, sizeof(threshold));
  threshold.UpdateThreshold      = arg_unsigned_long_katcp(d, 3);
  threshold.ThresholdMode[0]     = arg_unsigned_long_katcp(d, 4);
  threshold.ThresholdMode[1]     = arg_unsigned_long_katcp(d, 5);
  threshold.ThresholdAvgVal[0]   = arg_unsigned_long_katcp(d, 6);
  threshold.ThresholdAvgVal[1]   = arg_unsigned_long_katcp(d, 7);
  threshold.ThresholdUnderVal[0] = arg_unsigned_long_katcp(d, 8);
  threshold.ThresholdUnderVal[1] = arg_unsigned_long_katcp(d, 9);
  threshold.ThresholdOverVal[0]  = arg_unsigned_long_katcp(d, 10);
  threshold.ThresholdOverVal[1]  = arg_unsigned_long_katcp(d, 11);

  // TODO: validate threshold levels, these are 14-bit unsigned values in the
  // range (0,16383). The max value 16383 represents the absolute value of the
  // full-scale input of an ADC.

  result = XRFdc_SetThresholdSettings(rfdc->xrfdc, tile, blk, &threshold);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get cal threshold info");
    return KATCP_RESULT_FAIL;
  }

  // readback, format and send threshold settings
  result = XRFdc_GetThresholdSettings(rfdc->xrfdc, tile, blk, &threshold);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to readback new cal threshold info");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "UpdateThreshold %u, ThresholdMode0 %u, ThresholdMode1 %u, "
    "ThresholdAvgVal0 %u, ThresholdAvgVal1 %u, ThresholdUnderVal0 %u, ThresholdUnderVal1 %u, "
    "ThresholdOverVal0 %u, ThresholdOverVal1 %u",
    threshold.UpdateThreshold, threshold.ThresholdMode[0], threshold.ThresholdMode[1],
    threshold.ThresholdAvgVal[0], threshold.ThresholdAvgVal[1], threshold.ThresholdUnderVal[0], threshold.ThresholdUnderVal[1],
    threshold.ThresholdOverVal[0], threshold.ThresholdOverVal[1]);

  return KATCP_RESULT_OK;
}

/************************************************************************************************/
int rfdc_get_output_curr_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  unsigned int output_current_uA;
  unsigned int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if (tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse target dac tile and block
  if (argc < 3) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify dac tile idx (0-3) and dac block idx");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsDACBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get converter current
  result = XRFdc_GetOutputCurr(rfdc->xrfdc, tile, blk, &output_current_uA);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get output current");
    return KATCP_RESULT_FAIL;
  }
  // format and send status
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "current %u", output_current_uA);

  return KATCP_RESULT_OK;
}

int rfdc_get_output_curr_all_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int output_current_uA;
  unsigned int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if (tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // get current for all converters
  for (int tile = 0; tile < NUM_TILES; tile++) {
    for (int blk = 0; blk < NUM_BLKS; blk++) {
      if (XRFdc_IsDACBlockEnabled(rfdc->xrfdc, tile, blk)) {
        result = XRFdc_GetOutputCurr(rfdc->xrfdc, tile, blk, &output_current_uA);
        if (result != XRFDC_SUCCESS) {
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failure to get output current");
          return KATCP_RESULT_FAIL;
        }
        // format and send status
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "dac: tile %u, blk %u, current %u", tile, blk, output_current_uA);
      } else {
        prepend_inform_katcp(d);
        append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "dac: tile %u, blk %u, (disabled)", tile, blk);
      }
    }
  }

  return KATCP_RESULT_OK;
}

// current values in μA
// ES1 6425 to 32000, rounded to nearest increment
// Production silicon 2250 to 40500, rounded to nearest increment
int rfdc_set_vop_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  unsigned int max_current_uA;
  unsigned int min_current_uA;
  unsigned int current_uA;
  unsigned int output_current_uA;
  unsigned int result;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if (tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse target dac tile and block
  if (argc < 4) {
    // TODO: update help string for number of tiles for the device
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify dac tile idx (0-3), dac block idx, and current in uA");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  // TODO: update check for correct number of tiles for the device, should populate in tbs rfdc using api commands
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  // TODO: update check for correct number of blocks for the device, should populate in tbs rfdc using api commands
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsDACBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }


  current_uA = arg_unsigned_long_katcp(d, 3);

  // check requested vop value within range
  // ES1 6425 to 32000 uA, rounded to nearest step increments
  // Production silicon 2250 to 40500 uA, rounded to nearest step increments
  max_current_uA = XRFDC_MAX_I_UA(rfdc->xrfdc->RFdc_Config.SiRevision);
  min_current_uA = XRFDC_MIN_I_UA(rfdc->xrfdc->RFdc_Config.SiRevision);
  if (current_uA > max_current_uA) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "VOP setting %u is too high, max supported VOP current is %u uA",
      current_uA, max_current_uA);
    return KATCP_RESULT_INVALID;
  }

  if (current_uA < min_current_uA) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "VOP setting %u is too low, min supported VOP current is %u uA",
      current_uA, min_current_uA);
    return KATCP_RESULT_INVALID;
  }

  // set vop
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "request set dac tile: %u, blk: %u, vop: %u uA", tile, blk, current_uA);
  result = XRFdc_SetDACVOP(rfdc->xrfdc, tile, blk, current_uA);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set output current");
    return KATCP_RESULT_FAIL;
  }

  // read back, format and send output current status
  result = XRFdc_GetOutputCurr(rfdc->xrfdc, tile, blk, &output_current_uA);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to read output current after set");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "current %u", output_current_uA);

  return KATCP_RESULT_OK;
}

// InvSincFIR mode Valid values are: 0 (disable), 1 (first Nyquist zone), and
// for Gen 3 devices only, 2 (second Nyquist zone).
// In the driver API there is no macro for "disable" and the first/second nyquist
// zones have definition XRFDC_INV_SYNC_EN_MAX=1 and XRFDC_INV_SYNC_MODE_MAX=2, respectively
int rfdc_get_invsincfir_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  short unsigned int invsincfir_mode;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse converter tile and block parameters
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsDACBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get converter inv sinc fir value
  result = XRFdc_GetInvSincFIR(rfdc->xrfdc, tile, blk, &invsincfir_mode);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get inverse sinc filter status");
    return KATCP_RESULT_FAIL;
  }

  // format and send nyquist zone info
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "InvSincFIRMode %u", invsincfir_mode);

  return KATCP_RESULT_OK;
}

int rfdc_set_invsincfir_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  short unsigned int invsincfir_mode;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse converter tile and block parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), inv sinc FIR mode");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsDACBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // parse user inv sinc fir parameter, second nyquist block only available on gen3 devices
  invsincfir_mode = arg_unsigned_long_katcp(d, 3);
  if (invsincfir_mode==XRFDC_INV_SYNC_MODE_MAX && rfdc->xrfdc->RFdc_Config.IPType!=XRFDC_GEN3) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "second nyquist sinc fir operation only supported on Gen 3 devices");
    return KATCP_RESULT_INVALID;
  }

  // set converter inv sinc fir value
  result = XRFdc_SetInvSincFIR(rfdc->xrfdc, tile, blk, invsincfir_mode);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get inverse sinc filter status");
    return KATCP_RESULT_FAIL;
  }

  // readback and send inverse sinc fir mode
  result = XRFdc_GetInvSincFIR(rfdc->xrfdc, tile, blk, &invsincfir_mode);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get inverse sinc filter status");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "InvSincFIRMode %u", invsincfir_mode);

  return KATCP_RESULT_OK;
}

int rfdc_invsincfir_enabled_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int tile, blk;
  unsigned int invsincfir_en;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse converter tile and block parameters
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsDACBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get if inv sinc fir enabled, format and send info
  invsincfir_en = XRFdc_GetInverseSincFilter(rfdc->xrfdc, tile, blk);
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "InvSincFIREnabled %u", invsincfir_en);

  return KATCP_RESULT_OK;
}

int rfdc_get_imr_mode_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  unsigned int imr_mode;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // check that IMR is supported
  if (rfdc->xrfdc->RFdc_Config.IPType < XRFDC_GEN3) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "IMR mode settings only supported on Gen 3 devices");
    return KATCP_RESULT_INVALID;
  }

  // parse converter tile and block parameters
  if (argc < 3) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsDACBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // get IMR mode value
  result = XRFdc_GetIMRPassMode(rfdc->xrfdc, tile, blk, &imr_mode);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to get IMR mode value");
    return KATCP_RESULT_FAIL;
  }

  // format and send IMR mode info
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "IMRPassMode %u", imr_mode);

  return KATCP_RESULT_OK;
}

// XRFDC_DAC_IMR_MODE_LOWPASS 0U
// XRFDC_DAC_IMR_MODE_HIGHPASS 1U
int rfdc_set_imr_mode_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  unsigned int imr_mode;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // check that IMR mode is supported
  if (rfdc->xrfdc->RFdc_Config.IPType < XRFDC_GEN3) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "IMR settings only supported on Gen 3 devices");
    return KATCP_RESULT_INVALID;
  }

  // parse converter tile and block parameters
  if (argc < 4) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify: tile idx (0-3), block idx (0-3), IMR pass mode 0(lowpass)|1(highpass)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "dac block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // check if enabled
  if (!XRFdc_IsDACBlockEnabled(rfdc->xrfdc, tile, blk)) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // parse user IMR mode setting
  imr_mode = arg_unsigned_long_katcp(d, 3);
  if (imr_mode!=XRFDC_DAC_IMR_MODE_LOWPASS && imr_mode!=XRFDC_DAC_IMR_MODE_HIGHPASS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid IMR mode setting, %u", imr_mode);
    return KATCP_RESULT_INVALID;
  }

  // set IMR mode value
  result = XRFdc_SetIMRPassMode(rfdc->xrfdc, tile, blk, imr_mode);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to set IMR mode value");
    return KATCP_RESULT_FAIL;
  }

  // readback, format and send IMR mode info
  result = XRFdc_GetIMRPassMode(rfdc->xrfdc, tile, blk, &imr_mode);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to readback IMR mode");
    return KATCP_RESULT_FAIL;
  }
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "IMRPassMode %u", imr_mode);

  return KATCP_RESULT_OK;
}

// Use this function to trigger the update event for an event if the event
// source is Slice or Tile.
//
// 1 - update mixer
// 2 - update coarse delay
// 4 - update qmc
int rfdc_update_event_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  int result;
  unsigned int tile, blk;
  char* type;
  int converter_type;
  unsigned int update_event;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  // parse adc tile, block and desired attenuation parameters
  if (argc < 5) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "specify: tile idx (0-3), blk idx (0-3), converter type (adc|dac), update event (1-mixer, 2-coarse delay, 4-qmc)");
    return KATCP_RESULT_INVALID;
  }

  tile = arg_unsigned_long_katcp(d, 1);
  if (tile >= NUM_TILES) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "converter tile idx must be in the range 0-%d", NUM_TILES-1);
    return KATCP_RESULT_INVALID;
  }

  blk = arg_unsigned_long_katcp(d, 2);
  if (blk >= NUM_BLKS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "converter block idx must be in the range 0-%d", NUM_BLKS-1);
    return KATCP_RESULT_INVALID;
  }

  // parse converter type
  type = arg_string_katcp(d, 3);
  if (strcmp(type, "adc") == 0) {
    converter_type = XRFDC_ADC_TILE;
  } else if (strcmp(type, "dac") == 0) {
    converter_type = XRFDC_DAC_TILE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "must specify 'adc' or 'dac' converter type");
    return KATCP_RESULT_INVALID;
  }

  if (XRFdc_CheckBlockEnabled(rfdc->xrfdc, converter_type, tile, blk) != XRFDC_SUCCESS) {
    prepend_inform_katcp(d);
    append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "(disabled)");
    return KATCP_RESULT_OK;
  }

  // parse update event
  update_event = arg_unsigned_long_katcp(d, 4);
  if ((update_event != XRFDC_EVENT_MIXER) && (update_event != XRFDC_EVENT_QMC) && (update_event != XRFDC_EVENT_CRSE_DLY)) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid update event, must be 1 (Mixer), 2 (QMC), or 4 (Coarse Delay)");
    return KATCP_RESULT_INVALID;
  }

  // trigger update event
  result = XRFdc_UpdateEvent(rfdc->xrfdc, converter_type, tile, blk, update_event);
  if (result != XRFDC_SUCCESS) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "failed to trigger update event");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

/************************************************************************************************/
int rfdc_driver_ver_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  float driver_ver;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if (tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  driver_ver = XRFdc_GetDriverVersion();
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "version: %f", driver_ver);

  return KATCP_RESULT_OK;
}

int rfdc_get_master_tile_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct tbs_rfdc *rfdc;
  unsigned int master_tile;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if (tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

  rfdc = tr->r_rfdc;
  // TODO: rfdc driver has a built-in `IsReady` to indicate driver
  // initialization. Should use that instead.
  if (!rfdc->initialized) {
    extra_response_katcp(d, KATCP_RESULT_FAIL, "rfdc driver not initialized");
    return KATCP_RESULT_OWN;
  }

  master_tile = XRFdc_GetMasterTile(rfdc->xrfdc, XRFDC_ADC_TILE);
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "master tile: %d", master_tile);

  return KATCP_RESULT_OK;
}
