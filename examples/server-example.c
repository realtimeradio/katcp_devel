/* a simple server example which registers a couple of sensors and commands, most
 * of the functions defined here are callbacks which are registered in the main()
 * function and called by the mainloop when needed
 */

#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <limits.h>
#include <ctype.h>
#include <sysexits.h>

#include <errno.h>
#include <unistd.h>
#include <fcntl.h>

#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <sys/select.h>

#include <katcp.h>

#include <katpriv.h>

/* simple sensor functions ***************************************************/
/* these functions return the value immediately. This approach is acceptable */
/* when it is cheap to query a sensor value                                  */

int simple_integer_check_sensor(struct katcp_dispatch *d, struct katcp_acquire *a)
{
#if 0
  set_status_sensor_katcp(s, KATCP_STATUS_NOMINAL);
#endif

  return ((((int)time(NULL)) / 10) % 7) - 3;
}

/* simple sensor functions ***************************************************/
/* these functions return the value immediately. This approach is acceptable */
/* when it is cheap to query a sensor value                                  */

#ifdef KATCP_ENABLE_LLINT
long long big_integer_check_sensor(struct katcp_dispatch *d, struct katcp_acquire *a)
{
  long long r;
#if 0
  set_status_sensor_katcp(s, KATCP_STATUS_NOMINAL);
#endif

  r = time(NULL);

  r *= 64;

  return r;
}
#endif

/* more complex sensor function **********************************************/
/* this code allows one to perform an expensive single operation and attach  */
/* multiple sensors to it. The multiple_value_acquire() function here        */
/* collects the data point, and the two extract functions transform it into  */
/* something which ends up as the sensor value. Note that these function     */
/* reach into the katcp data structures, hence the include of katpriv.h      */

int multiple_value_acquire(struct katcp_dispatch *d, struct katcp_acquire *a)
{
  return ((int)time(NULL));
}

int extract_high_field(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_integer_acquire *ia;
  struct katcp_integer_sensor *is;
  struct katcp_acquire *a;
  int high;

  a = sn->s_acquire;

  if(a->a_type != KATCP_SENSOR_INTEGER){
    set_status_sensor_katcp(sn, KATCP_STATUS_UNKNOWN);
    return 0;
  }

  ia = a->a_more;
  is = sn->s_more;

  if(ia == NULL){
    set_status_sensor_katcp(sn, KATCP_STATUS_UNKNOWN);
    return 0;
  }

  high = (ia->ia_current >> 16) & 0xffff;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "extracting high field of 0x%08x as 0x%04x", ia->ia_current, high);

  is->is_current = high;

  return 0;
}

int extract_low_field(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_integer_acquire *ia;
  struct katcp_integer_sensor *is;
  struct katcp_acquire *a;
  int low;

  a = sn->s_acquire;

  if(a->a_type != KATCP_SENSOR_INTEGER){
    set_status_sensor_katcp(sn, KATCP_STATUS_UNKNOWN);
    return 0;
  }

  ia = a->a_more;
  is = sn->s_more;

  if(ia == NULL){
    set_status_sensor_katcp(sn, KATCP_STATUS_UNKNOWN);
    return 0;
  }

  low = ia->ia_current & 0xffff;

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "extracting low field of 0x%08x as 0x%04x", ia->ia_current, low);

  is->is_current = low;

  return 0;
}

/* Several command functions to service a particular katcp request **********/
/* check command 1: has the infrastructure generate its reply */

int ok_check_cmd(struct katcp_dispatch *d, int argc)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "saw ok check with %d arguments", argc);

  return KATCP_RESULT_OK; /* have the system send a status message for us */
}

/* check command 2: generates its own reply, with binary and integer output */

int own_check_cmd(struct katcp_dispatch *d, int argc)
{
  if(argc > 1){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "my first parameter is %s", arg_string_katcp(d, 1));
  }

  send_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!check-own", KATCP_FLAG_BUFFER, "\0\n\r ", 4, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, 42UL);

  return KATCP_RESULT_OWN; /* we send our own return codes */
}

#ifdef KATCP_USE_FLOATS
int float_check_cmd(struct katcp_dispatch *d, int argc)
{
  double value;

  if(argc > 1){
    value = arg_double_katcp(d, 1);
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "floating point number is %f", value);
  }

  return KATCP_RESULT_OK; /* have the system send a status message for us */
}
#endif

int fail_check_cmd(struct katcp_dispatch *d, int argc)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "saw fail check with %d arguments", argc);

  return KATCP_RESULT_FAIL; /* have the system send a status message for us */
}

int pause_check_cmd(struct katcp_dispatch *d, int argc)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "ran pause check, will not return");

  /* not useful on its own, but can use resume() to restart */

  return KATCP_RESULT_PAUSE;
}

#ifdef KATCP_SUBPROCESS
int subprocess_check_callback(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "was woken by child process exit");

  /* a callback can not use KATCP_RESULT_* codes, it has to generate its messages by hand */
  send_katcp(d, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!check-subprocess", KATCP_FLAG_LAST | KATCP_FLAG_STRING, KATCP_OK);

  /* unpause the client instance, so that it can parse new commands */
  resume_katcp(d);

  return 0;
}

int subprocess_check_cmd(struct katcp_dispatch *d, int argc)
{
  struct katcp_notice *n;
  struct katcp_job *j;
  char *arguments[3] = { "SLEEP", "10", NULL };

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "starting child process sleep 10");

  /* check if somebody else is busy */
  n = find_notice_katcp(d, "sleep-notice");
  if(n != NULL){ 
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "another instance already active");
    return KATCP_RESULT_FAIL;
  }
  
  /* create a notice, an entity which can invoke the callback when triggered */
  n = register_notice_katcp(d, "sleep-notice", 0, &subprocess_check_callback, NULL);
  if(n == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create notice object");
    return KATCP_RESULT_FAIL;
  }

  /* create a job, something which isn't a timer or a client issuing commands, give it the notice so that it can trigger it when it needs to */
  j = process_name_create_job_katcp(d, arguments[0], arguments, n, NULL);
  if(j == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "unable to create a subprocess handling job");
    return KATCP_RESULT_FAIL;
  }

  /* suspend, rely on the call back to resume this task */
  return KATCP_RESULT_PAUSE;
}
#endif

int main(int argc, char **argv)
{
  struct katcp_dispatch *d;
  struct katcp_sensor *sn;
  struct katcp_acquire *a;
#if 0
  struct cached_sensor_state local_data;
  struct fifo_sensor_state *fss;
#endif
  int status, result;

  if(argc <= 1){
    fprintf(stderr, "usage: %s [bind-ip:]listen-port\n", argv[0]);
    return 1;
  }

  /* create a state handle */
  d = startup_katcp();
  if(d == NULL){
    fprintf(stderr, "%s: unable to allocate state\n", argv[0]);
    return 1;
  }

  /* load up build and version information */
  add_version_katcp(d, "mylabel", 0, "myversion", "mybuildtime");

  /* example sensor */
  if(declare_integer_sensor_katcp(d, 0, "check.integer.simple", "integers where -1,0,1 is nominal, -2,2 is warning and the rest error", "none", &simple_integer_check_sensor, NULL, NULL, -1, 1, -2, 2, NULL)){
    fprintf(stderr, "server: unable to register sensors\n");
    return 1;
  }

#ifdef KATCP_ENABLE_LLINT
  /* big value example sensor */
  if(declare_bigint_sensor_katcp(d, 0, "check.integer.big", "large integer counter (time * 64)", "none", &big_integer_check_sensor, NULL, NULL, LLONG_MAX, LLONG_MIN, LLONG_MAX, LLONG_MIN, NULL)){
    fprintf(stderr, "server: unable to register sensors\n");
    return 1;
  }
#endif

#if 1
  /* custom output format for sensor */
  sn = find_sensor_katcp(d, "check.integer.simple");
  if(sn){
    if(set_format_sensor_katcp(d, sn, "0x%04x") < 0){
      fprintf(stderr, "server: unable to customise sensor output format\n");
    }
  } else {
    fprintf(stderr, "server: unable to retrieve sensor which was just declared\n");
  }
#endif

  /* a more complicated way of setting up sensors from one acquisition */

  a = setup_integer_acquire_katcp(d, &multiple_value_acquire, NULL, NULL);
  if(a == NULL){
    fprintf(stderr, "server: unable to initialise acquire logic\n");
    return 1;
  }

  if(declare_multi_integer_sensor_katcp(d, 0, "check.integer.multi.high", "high word", "none", 0, 16, 0, 32, a, &extract_high_field, NULL) < 0){
    fprintf(stderr, "server: unable to create high word part of multi-sensor\n");
    return 1;
  }
  if(declare_multi_integer_sensor_katcp(d, 0, "check.integer.multi.low", "low word", "none", 0, 16, 0, 32, a, &extract_low_field, NULL) < 0){
    fprintf(stderr, "server: unable to create low word part of multi-sensor\n");
    return 1;
  }

  /* register example commands */

  result = 0;

  result += register_katcp(d, "?check-ok",    "return ok", &ok_check_cmd);
  result += register_katcp(d, "?check-own",   "return self generated code", &own_check_cmd);
  result += register_katcp(d, "?check-fail",  "return fail", &fail_check_cmd);
  result += register_katcp(d, "?check-pause", "pauses", &pause_check_cmd);
#ifdef KATCP_SUBPROCESS
  result += register_katcp(d, "?check-subprocess", "runs sleep 10 as a subprocess and waits for completion", &subprocess_check_cmd);
#endif
#ifdef KATCP_USE_FLOATS
  result += register_katcp(d, "?check-float", "check floating point output", &float_check_cmd);
#endif

  if(result < 0){
    fprintf(stderr, "server: unable to register commands\n");
    return 1;
  }


#if 1
  /* alternative - run with more than one client */
  #define CLIENT_COUNT 3

  if(run_multi_server_katcp(d, CLIENT_COUNT, argv[1], 0) < 0){
    fprintf(stderr, "server: run failed\n");
  }
#else
  if(run_server_katcp(d, argv[1], 0) < 0){
    fprintf(stderr, "server: run failed\n");
  }
#endif

  status = exited_katcp(d);

  shutdown_katcp(d);
#if 0
  fifo_boolean_destroy_sensor(fss);
#endif

  return status;
}
