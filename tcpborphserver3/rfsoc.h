#ifndef RFSOC_H_
#define RFSOC_H_

#include <stdlib.h>

#include "xrfdc.h"
#include "xrfdc_mts.h"

#define RFDC_DEVICE_ID  0

//#define TBS_RFCLK_FILE "/lib/firmware/rfpll.txt"           // lmk/lmx configuration file
#define TBS_RFCLK_FILE "rfpll.txt"           // lmk/lmx configuration file
#define TBS_RFCLK_UPLOAD "rfdc-upload-rfclk"

#define TBS_DTBO_NAME "casper"                                    // name of the overlay to create at "TBS_DTBO_BASE"
#define TBS_DTBO_FILE "tcpborphserver.dtbo"                       // tbs device tree overlay filename
#define TBS_DTBO_BASE "/configfs/device-tree/overlays/%s"         // base dto `sysfs` path
#define TBS_DTBO_STAT "/configfs/device-tree/overlays/%s/status"  // read for overlay status
#define TBS_DTBO_PATH "/configfs/device-tree/overlays/%s/path"    // write the path of the overlay to apply to this location
#define RFDC_META_KEY  "rfdc"

#define RFDC_UPLOAD_DTO 0
#define RFDC_UPLOAD_LMK 1
#define RFDC_UPLOAD_LMX 2

// TODO: need to be part/gen smart
#define NUM_TILES 4
#define NUM_BLKS  4

struct tbs_rfdc {

  XRFdc *xrfdc;
  struct metal_device **metal_dev;
  XRFdc_MultiConverter_Sync_Config sync_config;

  // TODO: rfdc driver successfully completed, found rfdc driver has a built-in
  // member called `IsReady`, should move to using that.
  int initialized;
};

struct tbs_rfdc *create_tbs_rfdc();
void destroy_tbs_rfdc(struct katcp_dispatch *d, struct tbs_rfdc *rfdc);

int init_rfdc(struct katcp_dispatch *d, struct tbs_rfdc *rfdc);

// katcp api commands
int tbs_dto_cmd(struct katcp_dispatch *d, int argc);
int rfdc_init_cmd(struct katcp_dispatch *d, int argc);
int rfdc_driver_ver_cmd(struct katcp_dispatch *d, int argc);
int rfdc_status_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_block_status_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_master_tile_cmd(struct katcp_dispatch *d, int argc);
int rfdc_program_pll_cmd(struct katcp_dispatch *d, int argc);

// converter datapath commands
int rfdc_get_fabclkfreq_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_datatype_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_datawidth_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_nyquist_zone_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_nyquist_zone_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_coarse_delay_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_coarse_delay_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_qmc_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_qmc_cmd(struct katcp_dispatch *d, int argc);
int rfdc_report_mixer_cmd(struct katcp_dispatch *d, int argc);
int rfdc_update_nco_cmd(struct katcp_dispatch *d, int argc);

// converter pll commands
int rfdc_get_pll_config_cmd(struct katcp_dispatch *d, int argc);
int rfdc_dynamic_pll_config_cmd(struct katcp_dispatch *d, int argc);
int rfdc_pll_lock_status_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_clk_src_cmd(struct katcp_dispatch *d, int argc);

// adc MTS commands
int rfdc_run_mts_cmd(struct katcp_dispatch *d, int argc);
int rfdc_mts_report_cmd(struct katcp_dispatch *d, int argc);
int rfdc_report_mts_latency_cmd(struct katcp_dispatch *d, int argc);

// adc digital step attenuator commands
int rfdc_get_dsa_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_dsa_cmd(struct katcp_dispatch *d, int argc);

// adc calibration commands
int rfdc_get_cal_freeze_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_cal_freeze_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_cal_coeffs_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_cal_coeffs_cmd(struct katcp_dispatch *d, int argc);
int rfdc_disable_cal_override_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_cal_mode_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_cal_mode_cmd(struct katcp_dispatch *d, int argc);

// adc threshold commands
int rfdc_get_thresh_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_thresh_cmd(struct katcp_dispatch *d, int argc);

// dac commands
int rfdc_get_output_curr_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_vop_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_invsincfir_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_invsincfir_cmd(struct katcp_dispatch *d, int argc);
int rfdc_invsincfir_enabled_cmd(struct katcp_dispatch *d, int argc);
int rfdc_get_imr_mode_cmd(struct katcp_dispatch *d, int argc);
int rfdc_set_imr_mode_cmd(struct katcp_dispatch *d, int argc);

#endif // RFSOC_H_
