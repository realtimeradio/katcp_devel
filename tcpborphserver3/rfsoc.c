#include <stdio.h>
#include <string.h>

#include <stdint.h> // uint8_t, uint16_t
#include <unistd.h> // write

#include <katcp.h>
#include <avltree.h>

#include "tcpborphserver3.h"
#include "rfsoc.h"

#include "alpaca_platform.h"
#include "alpaca_i2c_utils.h"
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

  // init i2c
  init_i2c_bus();
#if PLATFORM == ZCU216 // || PLATFORM == ZCU208
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
#elif PLATFORM == ZRF16
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
#elif PLATFORM == ZCU111
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
    // lmk for tile 224/225
    result = prog_pll(I2C_DEV_PLL_SPI_BRIDGE, LMX_SDO_SS224_225, clkconfig, prg_cnt, pkt_len);
    if (result == XRFDC_FAILURE) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program first adc lmk pll");
      free(clkconfig);
      return KATCP_RESULT_FAIL;
    }
    // lmk for tile 226/227
    result = prog_pll(I2C_DEV_PLL_SPI_BRIDGE, LMX_SDO_SS226_227, clkconfig, prg_cnt, pkt_len);
  }

  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program second adc lmk pll");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  close_i2c_dev(I2C_DEV_PLL_SPI_BRIDGE);
  close_i2c_dev(I2C_DEV_IOX);
#elif PLATFORM == RFSoC2x2
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
#endif
  close_i2c_bus();

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

  // TODO: Just random state variables we I should probably not really work in
  // to the implementation unless I need to
  rfdc->initialized = 0;
  rfdc->clk_staged = 0;

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
  for (int i = 0; i < NUM_TILES; i++) {
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "ADC%d: Enabled %d, State: %d PLL: %d", i,
             ip_status.ADCTileStatus[i].IsEnabled,
             ip_status.ADCTileStatus[i].TileState,
             ip_status.ADCTileStatus[i].PLLState);
  }

  return KATCP_RESULT_OK;

}

int rfdc_run_mts_cmd(struct katcp_dispatch *d, int argc) {

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "mts not implemented");

  return KATCP_RESULT_OK;
}

int rfdc_update_nco_cmd(struct katcp_dispatch *d, int argc) {

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "nco update not implemented");

  return KATCP_RESULT_OK;
}
