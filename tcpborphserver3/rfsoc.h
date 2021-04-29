#ifndef RFSOC_H_
#define RFSOC_H_

#include <stdlib.h>
#define TBS_RFCLK_FILE "/lib/firmware/rfpll.txt"
#define RFDC_META_KEY  "rfdc"

int rfdc_init_cmd(struct katcp_dispatch *d, int argc);

#endif // RFSOC_H_
