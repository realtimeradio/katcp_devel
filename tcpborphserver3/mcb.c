#include <stdio.h>

#include <katcp.h>
#include "mcb.h"

int mcb_ok_check_cmd(struct katcp_dispatch *d, int argc) {

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "saw ok mcb check with %d arguments", argc);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "my first parameter is %s", arg_string_katcp(d,0));

  return KATCP_RESULT_OK; /* have the system send a status message for us */

}

int mcb_own_check_cmd(struct katcp_dispatch *d, int argc)
{
  if(argc > 1){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "my first parameter is %s", arg_string_katcp(d, 1));
  }

  send_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!mcb-own-check", KATCP_FLAG_BUFFER, "\0\n\r ", 4, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, 42UL);

  return KATCP_RESULT_OWN; /* we send our own return codes */
}

int mcb_send_value_cmd(struct katcp_dispatch *d, int argc) {

  int v = 0;
  if (argc > 1) {
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "saw ok mcb check with %d arguments", argc);
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "my first parameter is %s", arg_string_katcp(d, 1));
    v = arg_unsigned_long_katcp(d, 1) + 1;
  }

  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "%s %s %s %s", "I", "am", "a", "teapot");

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING , KATCP_OK);
  append_unsigned_long_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, v);

  return KATCP_RESULT_OWN;
}

void destroy_mcb_nx(struct katcp_dispatch *d, struct mcb_nx *rnx) { free(rnx); }

struct mcb_nx *create_mcb_nx(struct katcp_dispatch *d) {
  struct mcb_nx *rnx;
  rnx = malloc(sizeof(struct mcb_nx));
  rnx->a =1;
  rnx->b =2;
  return rnx;
}

int mcb_notice_c(struct katcp_dispatch *d, struct katcp_notice *n, void *data) {
  struct mcb_nx *rnx;
  rnx = data;
  if(rnx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic problem: no port data given to handler\n");
    abort();
#endif
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "made it noitce c");

  prepend_inform_katcp(d);
  append_args_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "%d %d", rnx->a, rnx->b);

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, KATCP_OK);

  //send_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "#mcb-notices", KATCP_FLAG_BUFFER, "\0\n\r ", 4, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, 42UL);
  resume_katcp(d);
  return 0;
}

int mcb_notice_a(struct katcp_dispatch *d, struct katcp_notice *n, void *data) {
  struct mcb_nx *rnx;
  rnx = data;
  if(rnx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic problem: no port data given to handler\n");
    abort();
#endif
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "made it noitce a");
  rnx->a = 10;
  return 0;
}

int mcb_notice_b(struct katcp_dispatch *d, struct katcp_notice *n, void *data) {
  struct katcp_notice *nc;
  struct mcb_nx *rnx;
  struct katcp_job *j;
  char *arguments[3] = { "sleep", "2", NULL };
  rnx = data;
  if(rnx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "logic problem: no port data given to handler\n");
    abort();
#endif
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "made it noitce b");
  rnx->b = 30;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "tryiing to make notice c");
  /********* attempt to chain notices ****************/

  nc = find_notice_katcp(d, "RFSOC_NOTICE_C");
  if(nc){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "another upload already seems in progress, halting this attempt");
    return 0;
    //return KATCP_RESULT_FAIL;
  }

  nc = create_notice_katcp(d, "RFSOC_NOTICE_C", 0); // is same tag an issue?
  if(nc == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create notification logic to trigger when upload completes");
    return 0;
    //return KATCP_RESULT_FAIL;
  }

  if(add_notice_katcp(d, nc, &mcb_notice_c, rnx) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register callback for upload completion"); 
    destroy_mcb_nx(d, rnx);
    return 0;
    //return KATCP_RESULT_FAIL;
  }

  /* create a job, something which isn't a timer or a client issuing commands, give it the notice so that it can trigger it when it needs to */
  j = process_name_create_job_katcp(d, arguments[0], arguments, nc, NULL);
  if(j == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create a subprocess handling job");
    return 0;
    //return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "notice c should be registerd...");
  return 0;
}

int mcb_notices_cmd(struct katcp_dispatch *d, int argc) {

  //struct katcp_dispatch *dl;
  struct katcp_notice *na;
  struct katcp_notice *nb;
  struct mcb_nx *rnx;
  struct katcp_job *j;
  char *arguments[3] = { "sleep", "5", NULL };
  char* NOTICE_A = "RFSOC_NOTICE_A";
  char* NOTICE_B = "RFSOC_NOTICE_B";

  //dl = template_shared_katcp(d);
  //if(dl == NULL){
  //  return KATCP_RESULT_FAIL;
  //}

  na = find_notice_katcp(d, NOTICE_A);
  if(na){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "another upload already seems in progress, halting this attempt");
    return KATCP_RESULT_FAIL;
  }

  na = create_notice_katcp(d, NOTICE_A, 0);
  if(na == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create notification logic to trigger when upload completes");
    return KATCP_RESULT_FAIL;
  }

  nb = find_notice_katcp(d, NOTICE_B);
  if(nb){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "another upload already seems in progress, halting this attempt");
    return KATCP_RESULT_FAIL;
  }

  nb = create_notice_katcp(d, NOTICE_B, 0); // is same tag an issue?
  if(nb == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create notification logic to trigger when upload completes");
    return KATCP_RESULT_FAIL;
  }

  rnx = create_mcb_nx(d);

  if(add_notice_katcp(d, na, &mcb_notice_a, rnx) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register callback for upload completion"); 
    destroy_mcb_nx(d, rnx);
    return KATCP_RESULT_FAIL;
  }

  if(add_notice_katcp(d, nb, &mcb_notice_b, rnx) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register callback for upload completion");
    return KATCP_RESULT_FAIL;
  }

  /* create a job, something which isn't a timer or a client issuing commands, give it the notice so that it can trigger it when it needs to */
  j = process_name_create_job_katcp(d, arguments[0], arguments, na, nb);
  if(j == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create a subprocess handling job");
    return KATCP_RESULT_FAIL;
  }

  //return KATCP_RESULT_OK;
  return KATCP_RESULT_PAUSE;

}
