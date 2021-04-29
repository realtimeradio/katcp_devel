#include <stdio.h>

#include <katcp.h>
#include <avltree.h>

#include "tcpborphserver3.h"
#include "rfsoc.h"

int rfdc_init_cmd(struct katcp_dispatch *d, int argc) {
  struct tbs_raw *tr;
  struct avl_node *an;
  struct meta_entry *rfdc_meta_data;
  int i, middle;

  tr = get_mode_katcp(d, TBS_MODE_RAW);
  if(tr == NULL) {
    return KATCP_RESULT_FAIL;
  }

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

  // display rfdc config info
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

  // rfdc is seen, begin to initialize

  return KATCP_RESULT_OK;
}


