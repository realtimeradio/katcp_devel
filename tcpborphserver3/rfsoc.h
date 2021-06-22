#ifndef RFSOC_H_
#define RFSOC_H_

#include <stdlib.h>

#include "xrfdc.h"

#define RFDC_DEVICE_ID  0

//#define TBS_RFCLK_FILE "/lib/firmware/rfpll.txt"           // lmk/lmx configuration file
#define TBS_RFCLK_FILE "rfpll.txt"           // lmk/lmx configuration file
#define TBS_RFCLK_UPLOAD "rfdc-upload-rfclk"

#define TBS_DTBO_NAME "casper"
#define TBS_DTBO_FILE "tcpborphserver.dtbo" //"/lib/firmware/tcpborphserver.dtbo"  // device tree binary overlay
#define TBS_DTBO_BASE "/configfs/device-tree/overlays/%s"  // path to create device tree overlay
#define TBS_DTBO_STAT "/configfs/device-tree/overlays/%s/status"  // path to create device tree overlay
#define TBS_DTBO_PATH "/configfs/device-tree/overlays/%s/path"  // path to create device tree overlay
#define RFDC_META_KEY  "rfdc"

#define RFDC_UPLOAD_DTO 0
#define RFDC_UPLOAD_LMK 1
#define RFDC_UPLOAD_LMX 2

// TODO: need to be part/gen smart should look at driver stuff, I want to
// rememebr it is built-in
#define NUM_TILES 4

struct tbs_rfdc {

  XRFdc *xrfdc;
  struct metal_device **metal_dev;
  int initialized; // rfdc driver successfully completed, found rfdc driver has a built-in member called `IsReady`, should move to using that.

  // was thinking to have a staging variable for clock programming?
  int clk_staged; //clk_programmed?
};

struct tbs_rfdc *create_tbs_rfdc();
void destroy_tbs_rfdc(struct katcp_dispatch *d, struct tbs_rfdc *rfdc);

int init_rfdc(struct katcp_dispatch *d, struct tbs_rfdc *rfdc);

// katcp api commands
int rfdc_init_cmd(struct katcp_dispatch *d, int argc);
int rfdc_status_cmd(struct katcp_dispatch *d, int argc);
int rfdc_run_mts_cmd(struct katcp_dispatch *d, int argc);
int rfdc_update_nco_cmd(struct katcp_dispatch *d, int argc);
int rfdc_program_pll_cmd(struct katcp_dispatch *d, int argc);

#endif // RFSOC_H_
