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
  char* tcsfile;
  FILE* fileptr;
  char* buffer;
  int len;
  struct stat st;
  uint32_t* clkconfig;

  //TODO: on most devices (zcu216/208 specifically) we will only get so far
  //without the device tree overlay should we stop if not loaded?
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

  // TODO: any interest in changing logic to set parameter types based on pll
  // selection here (reg cnt, lmk select, perhaps a poiner to a function call?)
  if (strcmp(pll, "lmk") == 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "will program lmk");
  } else if (strcmp(pll, "lmx") == 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "will program lmx2594");
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unrecognized rfpll option %s", pll);
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
  buffer = malloc(len); // TODO: there is no call free on this...
  if (buffer == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes", len);
    return KATCP_RESULT_FAIL;
  }

  snprintf(buffer, len, "%s/%s", tr->r_bof_dir, tcsfile);
  buffer[len - 1] = '\0';

  if (stat(buffer, &st) != 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "clock config file %s does not exist", buffer);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "attempting to program %s with %s", pll, buffer);
  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING|KATCP_FLAG_LAST, "%s", buffer);

  fileptr = fopen(buffer, "r");
  if (fileptr == NULL) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to open %s", tcsfile);
    return KATCP_RESULT_FAIL;
  }

  /* can now start to program pll, but there are different type var we need to collect */
  // target i2c device (I2C_DEV_CLK104, I2C_DEV_LMK_SPI_BRIDGE...)
  // programming regsiter count (LMK/LMX_REG_CNT)
  // sdo mux sel (LMK_SDO_SS, LMX_SDO..)
  // iox setup (most different and difficult to abstract...)

  // could parse and readtcs closer to call to program, that way `free(clkconfig)` isn't everywhere
  if (strcmp(pll, "lmk") == 0) {
    clkconfig = readtcs(fileptr, LMK_REG_CNT, 0);
  } else {
    clkconfig = readtcs(fileptr, LMX2594_REG_CNT, 1);
  }
  if (clkconfig == NULL) {
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "problem parsing clock file. Wrong file for requested pll chip, corrupt file, or could not allocate array");
    return KATCP_RESULT_FAIL;
  }

  // init i2c
  init_i2c_bus();
#if PLATFORM == ZCU216 // || PLATFORM == ZCU208
  // init spi bridge
  init_i2c_dev(I2C_DEV_CLK104);
  // init fabric gpio for SDIO readback (no IO Expander on zcu216/208)
  init_clk104_gpio(320);

  // set sdo mux to lmk
  result = set_sdo_mux(LMK_MUX_SEL);
  usleep(0.5e6);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "clk104 gpio sdo mux not set correctly");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // configure spi device
  uint8_t spi_config[2] = {0xf0, 0x03}; // spi bridge configuration packet
  result = i2c_write(I2C_DEV_CLK104, spi_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure clk104 spi bridge");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // program pll
  if (strcmp(pll, "lmk") == 0) {
    result = prog_pll(I2C_DEV_CLK104, LMK_SDO_SS, clkconfig, LMK_REG_CNT);
  } else {
    // configure clk104 adc lmx2594 to tile 225
    result = prog_pll(I2C_DEV_CLK104, LMX_ADC_SDO_SS, clkconfig, LMX2594_REG_CNT);
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

  // configure spi devices
  uint8_t spi_config[2] = {0xf0, 0x03}; // spi bridge configuration packet
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

  // configure io expander
  // set IOX_MUX_SEL bits 0 to configure as outputs, use lmx tile 224/225 macro since we know it is zero
  uint8_t iox_config[2] = {IOX_CONF_REG, (0xff & LMX_ADC_MUX_SEL_224_225)}; // iox configuration packet
  result = i2c_write(I2C_DEV_IOX, iox_config, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure iox");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // write to the GPIO reg to lower the outputs since power-on default is high
  // (use the same data from iox_config[1] packet to do this since already zeros)
  uint8_t iox_gpio[2] = {IOX_GPIO_REG, iox_config[1]};
  result = i2c_write(I2C_DEV_IOX, iox_gpio, 2);
  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not configure iox");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  // program pll
  if (strcmp(pll, "lmk") == 0) {
    result = prog_pll(I2C_DEV_CLMK_SPI_BRIDGE, LMK_SDO_SS, clkconfig, LMK_REG_CNT);
  } else {
    // lmx for tile 224/225
    result = prog_pll(I2C_DEV_LMX_SPI_BRIDGE, LMX_SDO_SS224_225, LMX_ARRAY, LMX2594_REG_CNT);
    if (result == XRFDC_FAILURE) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program pll");
      free(clkconfig);
      return KATCP_RESULT_FAIL;
    }
    // lmx for tile 226/227
    result = prog_pll(I2C_DEV_LMX_SPI_BRIDGE, LMX_SDO_SS226_227, LMX_ARRAY, LMX2594_REG_CNT);
  }

  if (result == XRFDC_FAILURE) {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "could not program pll");
    free(clkconfig);
    return KATCP_RESULT_FAIL;
  }

  //close devices
  close_i2c_dev(I2C_DEV_LMK_SPI_BRIDGE);
  close_i2c_dev(I2C_DEV_LMX_SPI_BRIDGE);
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
  // applying dtbo
  struct stat st;
  char overlay_path[128];
  int fd_dto, rd, wr;
  char overlay_status[16] = {0};

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

  /* apply the dtbo */
  // This logic is a pretty buggy, and possibly not a good idea to make a part
  // of the rfsoc code. But, at the very least as implemented now, short-circuiting and returning `!ok`
  // is not the correct thing to do for a few reason. First, consider starting
  // tcpborphserver with the dtbo already applied (like I do when building new
  // borph server executables) if we short circuit the xrfdc driver was mallo'd
  // but because we returned `!ok` before we called `init_rfdc` nothing
  // meaningful happened we wil hang on subsequent methods. Second, what if the
  // dto we load doesn't contain the rfdc? So just checking if a casper dtbo is
  // applied is not sufficeint. Instead, we woudl need to actually look in the
  // device tree by looking at the `of nodes`. The rfdc driver init code will
  // does this already and will fail out. And since it will fail out why not
  // just let the driver do the check? Instead, applying the dto should probably
  // just be an auxilliary command...
  // But it is worth thinking about the initial startup sequence (how can the
  // client know the case where the dto is applied)

  sprintf(overlay_path, TBS_DTBO_BASE, TBS_DTBO_NAME);
  if(stat(overlay_path, &st) == 0){
    if (S_ISDIR(st.st_mode)) {
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "device tree overlay already exists");
      //return KATCP_RESULT_OK; //buggy see line 398 comment
    }
  } else {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "creating device tree overlay %s", TBS_DTBO_NAME);
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

  rd = read(fd_dto, overlay_status, 16);
  close(fd_dto);

  // note, dto status `applied` does not always mean everything went well, we just assume it had 
  if (strncmp(overlay_status, "applied", 7) == 0) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "a dto is already applied");
    //return KATCP_RESULT_OK; //buggy see line 398 comment
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

  /*
     should potentially make the above implementation of applying a dto its own
     method for improving support for dto universally on 7series/mpsoc
  */
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

int apply_dto() {
  // implemented for now in `rfdc_init_cmd`
  return 0;
}

int apply_dto_cmd(struct katcp_dispatch *d, int argc) {

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "not implemented");

  return KATCP_RESULT_OK;
}

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
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "rfdc driver not initialized");
    return KATCP_RESULT_FAIL;
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
