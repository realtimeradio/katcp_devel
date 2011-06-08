/* (c) 2010,2011 SKA SA */
/* Released under the GNU GPLv3 - see COPYING */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>

#ifdef KATCP_USE_FLOATS
#include <math.h>
#endif

#include "katcp.h"
#include "katpriv.h"
#include "netc.h"

#define SENSOR_MAGIC   0x0005e507
#define NONSENSE_MAGIC 0xffee3393

#define SENSOR_POLL_SECONDS              1  /* default is to poll once a second */
#define SENSOR_LIMIT_MICROSECONDS   250000  /* fastest rate */

#define SENSOR_LIMIT_FUDGE            2500  /* minor fudge factor */

/**********************************************************************************************/

static int run_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, int forced);

/**********************************************************************************************/

#ifdef PARANOID
static void sane_nonsense(struct katcp_nonsense *ns)
{
  int i, found;
  struct katcp_sensor *sn;

  if(ns == NULL){
    fprintf(stderr, "sane: null nonsense\n");
    abort();
  }

  if(ns->n_magic != NONSENSE_MAGIC){
    fprintf(stderr, "sane: bad nonsense magic\n");
    abort();
  }

  sn = ns->n_sensor;
  if(sn == NULL){
    fprintf(stderr, "sane: no sensor for nonsense %p\n", ns);
    abort();
  }

  if(sn->s_magic != SENSOR_MAGIC){
    fprintf(stderr, "sane: nonsense %p does not point at sensor (magic %x)\n", ns, sn->s_magic);
    abort();
  }

  found = (-1);
  for(i = 0; i < sn->s_refs; i++){
    if(sn->s_nonsense[i] == ns){
      found = i;
    }
  }
  if(found < 0){
    fprintf(stderr, "sane: nonsense %p not found in %p with %d refs\n", ns, sn, sn->s_refs);
    abort();
  }

}

static void sane_sensor(struct katcp_sensor *sn)
{
  if(sn == NULL){
    fprintf(stderr, "sane: null sensor\n");
    abort();
  }
  if(sn->s_magic != SENSOR_MAGIC){
    fprintf(stderr, "sane: bad sensor magic for %p\n", sn);
    abort();
  }
  if((sn->s_type < 0) || (sn->s_type >= KATCP_SENSORS_COUNT)){
    fprintf(stderr, "sane: broken type for %s\n", sn->s_name);
    abort();
  }
  if((sn->s_status < 0) || (sn->s_status >= KATCP_STATA_COUNT)){
    fprintf(stderr, "sane: broken status for %s\n", sn->s_name);
    abort();
  }
}

static void sane_acquire(struct katcp_acquire *a)
{
  if(a == NULL){
    fprintf(stderr, "sane: null acquire\n");
    abort();
  }
  if((a->a_type < 0) || (a->a_type >= KATCP_SENSORS_COUNT)){
    fprintf(stderr, "sane: broken type for acquire %p\n", a);
    abort();
  }
}
#else
#define sane_sensor(sn)
#define sane_nonsense(ns)
#define sane_acquire(ns)
#endif

/**********************************************************************************************/

#if 0
static struct katcp_acquire *create_acquire_katcp(struct katcp_dispatch *d, int type);
#endif

static int link_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, struct katcp_sensor *sn, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn));
static int del_acquire_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn);

static void destroy_sensor_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn);
static void destroy_nonsense_katcp(struct katcp_dispatch *d, struct katcp_nonsense *ns);
int generic_sensor_update_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn, char *name);

static int configure_sensor_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn, int strategy, int manual, char *extra);

/* structure allowing one to handle different types generically *******************************/

struct katcp_check_table{
  char *c_name;
  int (*c_extract)(struct katcp_dispatch *d, struct katcp_sensor *sn);
  int (*c_create_acquire)(struct katcp_dispatch *d, struct katcp_acquire *a, int type);
  int (*c_create_nonsense)(struct katcp_dispatch *d, struct katcp_nonsense *ns);
  int (*c_append_type)(struct katcp_dispatch *d, int flags, struct katcp_sensor *sn);
  int (*c_append_value)(struct katcp_dispatch *d, int flags, struct katcp_sensor *sn);
  int (*c_append_diff)(struct katcp_dispatch *d, int flags, struct katcp_nonsense *ns);
  int (*c_scan_diff)(struct katcp_nonsense *ns, char *extra);
  int (*c_scan_value)(struct katcp_acquire *a, char *value);
  int (*c_has_poll)(struct katcp_acquire *a);
  int (*c_checks[KATCP_STRATEGIES_COUNT])(struct katcp_nonsense *ns);
};

/* generic routines (used across multiple types) **********************************************/

int period_check_katcp(struct katcp_nonsense *ns)
{
  struct katcp_sensor *sn;
  struct katcp_dispatch *dx;

  dx = ns->n_client;
  sn = ns->n_sensor;

  if(cmp_time_katcp(&(ns->n_next), &(sn->s_recent)) > 0){
    log_message_katcp(dx, KATCP_LEVEL_TRACE, NULL, "period not yet valid as next %lu.%lus still greater than current %lu.%lus\n", ns->n_next.tv_sec, ns->n_next.tv_usec, sn->s_recent.tv_sec, sn->s_recent.tv_usec);
    return 0;
  }

  /* WARNING: is it necessary to deal with cases where recent is just a bit smaller next */

  add_time_katcp(&(ns->n_next), &(ns->n_next), &(ns->n_period));

  log_message_katcp(dx, KATCP_LEVEL_TRACE, NULL, "next valid time for period is %lu.%lus", ns->n_next.tv_sec, ns->n_next.tv_usec);

  return 1;
}

int force_check_katcp(struct katcp_nonsense *ns)
{
  return 1;
}

int status_check_katcp(struct katcp_nonsense *ns)
{
  struct katcp_sensor *sn;

  sn = ns->n_sensor;

  if(sn->s_status == ns->n_status){
    return 0;
  }

  ns->n_status = sn->s_status;
  return 1;
}

/* double routines *******************************************************/

#ifdef KATCP_USE_FLOATS
int extract_double_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_acquire *a;
  struct katcp_double_acquire *da;
  struct katcp_double_sensor *ds;

  a = sn->s_acquire;

  if(sn->s_type != KATCP_SENSOR_FLOAT){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem - double operation applied to type %d", sn->s_type);
    return -1;
  }

  ds = sn->s_more;
  da = a->a_more;

  if((da->da_current < ds->ds_min) || (da->da_current > ds->ds_max)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "extracted float (%f) for sensor %s not in advertised range", da->da_current, sn->s_name);
    return -1;
  }

  ds->ds_current = da->da_current;
  return 0;
}

int create_acquire_double_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, int type)
{
  struct katcp_double_acquire *da;

  switch(type){
    case KATCP_SENSOR_FLOAT :
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "double function incapable of creating type %d", type);
      return -1;
  }

  if((a == NULL) || (a->a_type >= 0)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "acquire structure empty or type already set");
    return -1;
  }

  da = malloc(sizeof(struct katcp_double_acquire));
  if(da == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for acquire structure", sizeof(struct katcp_double_acquire));
    return -1;
  }

  da->da_current = 0;
  da->da_get = NULL;

  a->a_more = da;
  a->a_type = type;

  return 0;
}

int set_value_double_katcp(struct katcp_acquire *a, double value)
{
  struct katcp_double_acquire *da;

  da = a->a_more;

  if(da == NULL){
    return -1;
  }

  da->da_current = value;

  return 0;
}

int scan_value_double_katcp(struct katcp_acquire *a, char *value)
{
  struct katcp_double_acquire *da;
  char *end;
  double got;

  da = a->a_more;

  if(da == NULL){
    return -1;
  }

  if(value == NULL){
    return -1;
  }

  got = strtod(value, &end);
#ifdef DEBUG
  fprintf(stderr, "scan double: value <%s> converts to <%f>\n", value, got);
#endif
  switch(end[0]){
    case ' '  :
    case '\0' :
    case '\r' :
    case '\n' :
      break;
    default  :
      return -1;
  }

  da->da_current = got;

  return 0;
}

int has_poll_double_katcp(struct katcp_acquire *a)
{
  struct katcp_double_acquire *da;

  da = a->a_more;

  if(da && da->da_get){
    return 1;
  }

  return 0;
}

int create_sensor_double_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn, double min, double max)
{
  struct katcp_double_sensor *ds;

  if((sn == NULL) || (sn->s_type != KATCP_SENSOR_FLOAT)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to layer double sensor over %p", sn);
    return -1;
  }

  ds = malloc(sizeof(struct katcp_double_sensor));
  if(ds == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for double sensor structure", sizeof(struct katcp_double_sensor));
    return -1;
  }

  ds->ds_current = 0;
  ds->ds_min = min;
  ds->ds_max = max;

  sn->s_more = ds;

  return 0;
}

int create_nonsense_double_katcp(struct katcp_dispatch *d, struct katcp_nonsense *ns)
{
  struct katcp_double_nonsense *dn;
  struct katcp_double_sensor *ds;
  struct katcp_sensor *sn;

  sn = ns->n_sensor;

  if(sn == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "null sensor given to double create function");
    return -1;
  }

  switch(sn->s_type){
    case KATCP_SENSOR_FLOAT :
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "incapable of creating sensor client of type %d", sn->s_type);
      return -1;
  }

  ds = sn->s_more;
  dn = malloc(sizeof(struct katcp_double_nonsense));
  if(dn == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for double sensor structure", sizeof(struct katcp_double_nonsense));
    return -1;
  }

  /* changed as per request from simon to always report initial value for event */
  dn->dn_previous = ds->ds_max + 1; 
  dn->dn_delta = 1; /* guessing at a reasonable default */

  ns->n_more = dn;

  return 0;
}

int append_type_double_katcp(struct katcp_dispatch *d, int flags, struct katcp_sensor *sn)
{
  struct katcp_double_sensor *ds;

  if(sn == NULL){
    return -1;
  }

  ds = sn->s_more;

  if(append_double_katcp(d, KATCP_FLAG_DOUBLE | (flags & KATCP_FLAG_FIRST), ds->ds_min) < 0){
    return -1;
  }

  return append_double_katcp(d, KATCP_FLAG_DOUBLE | (flags & KATCP_FLAG_LAST), ds->ds_max);
}

int append_value_double_katcp(struct katcp_dispatch *d, int flags, struct katcp_sensor *sn)
{
  struct katcp_double_sensor *ds;

  if(sn == NULL){
    return -1;
  }

  ds = sn->s_more;

  return append_double_katcp(d, KATCP_FLAG_DOUBLE | (flags & (KATCP_FLAG_FIRST | KATCP_FLAG_LAST)), ds->ds_current);
}

int append_diff_double_katcp(struct katcp_dispatch *d, int flags, struct katcp_nonsense *ns)
{
  struct katcp_double_nonsense *dn;

  if(ns == NULL){
    return -1;
  }

  dn = ns->n_more;

  return append_double_katcp(d, KATCP_FLAG_DOUBLE | (flags & (KATCP_FLAG_FIRST | KATCP_FLAG_LAST)), dn->dn_delta);
}

int scan_diff_double_katcp(struct katcp_nonsense *ns, char *extra)
{
  struct katcp_double_nonsense *dn;
  char *end;
  double value;

  if(ns == NULL){
    return -1;
  }

  if(extra == NULL){
    return -1;
  }

  dn = ns->n_more;

  value = strtod(extra, &end);
  switch(end[0]){
    case ' '  :
    case '\0' :
    case '\r' :
    case '\n' :
      break;
    default  :
      return -1;
  }

  dn->dn_delta = value;

  return 0;
}

int event_check_double_katcp(struct katcp_nonsense *ns)
{
  struct katcp_sensor *sn;
  struct katcp_double_sensor *ds;
  struct katcp_double_nonsense *dn;
  struct katcp_dispatch *dx;
  int result;

  sn = ns->n_sensor;
  if(sn == NULL){
    fprintf(stderr, "major logic problem: null sensor for client while checking for event\n");
    abort();
  }

  dx = ns->n_client;

  switch(sn->s_type){
    case KATCP_SENSOR_FLOAT :
      break;
    default :
      log_message_katcp(dx, KATCP_LEVEL_FATAL, NULL, "called double comparison on type %d", sn->s_type);
      return 0;
  }

  ds = sn->s_more;
  dn = ns->n_more;

  log_message_katcp(dx, KATCP_LEVEL_TRACE, NULL, "double event check had %f now %f", dn->dn_previous, ds->ds_current);

  result = status_check_katcp(ns); /* WARNING: status_check has the side effect of updating status */
  
  if(dn->dn_previous == ds->ds_current){
    return result;
  }

  dn->dn_previous = ds->ds_current;

  return 1;
}

int diff_check_double_katcp(struct katcp_nonsense *ns)
{
  struct katcp_sensor *sn;
  struct katcp_double_sensor *ds;
  struct katcp_double_nonsense *dn;
  double delta;

  sn = ns->n_sensor;

#ifdef DEBUG
  if((sn == NULL) || (sn->s_type != KATCP_SENSOR_FLOAT)){
    fprintf(stderr, "major logic problem: diff double check not run on double\n");
    abort();
  }
#endif

  ds = sn->s_more;
  dn = ns->n_more;

  delta = fabs(ds->ds_current - dn->dn_previous);

  if(delta < dn->dn_delta){
    /* still within range */
    return 0;
  }
  
  dn->dn_previous = ds->ds_current;

  return 1;
}

int set_double_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, double value)
{
  int result;

  result = set_value_double_katcp(a, value);

  propagate_acquire_katcp(d, a);

  return result;
}

#endif

/* integer routines, intbool common to integer and boolean code *******************************/

int extract_intbool_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_acquire *a;
  struct katcp_integer_acquire *ia;
  struct katcp_integer_sensor *is;

  a = sn->s_acquire;

  switch(sn->s_type){
    case KATCP_SENSOR_INTEGER :
      switch(a->a_type){
        case KATCP_SENSOR_INTEGER :
        case KATCP_SENSOR_BOOLEAN :
          break;
        default :
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem - integer sensor %s unable to acquire type %d", sn->s_name, a->a_type);
          return -1;
      }
      break;
    case KATCP_SENSOR_BOOLEAN :
      switch(a->a_type){
        case KATCP_SENSOR_BOOLEAN :
          break;
        default :
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem - boolean sensor %s unable to acquire type %d", sn->s_name, a->a_type);
          return -1;
      }
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem - intbool extract used on sensor neither integer nor boolean");
      return -1;
  }

  is = sn->s_more;
  ia = a->a_more;

  if((ia->ia_current < is->is_min) || (ia->ia_current > is->is_max)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "bad extracted integer (%d) for sensor %s", ia->ia_current, sn->s_name);
    return -1;
  }

  is->is_current = ia->ia_current;
  return 0;
}

int create_acquire_intbool_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, int type)
{
  struct katcp_integer_acquire *ia;

  switch(type){
    case KATCP_SENSOR_INTEGER :
    case KATCP_SENSOR_BOOLEAN :
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "function incapable of creating type %d", type);
      return -1;
  }

  if((a == NULL) || (a->a_type >= 0)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "acquire structure empty or type already set");
    return -1;
  }

  ia = malloc(sizeof(struct katcp_integer_acquire));
  if(ia == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for acquire structure", sizeof(struct katcp_integer_acquire));
    return -1;
  }

  ia->ia_current = 0;
  ia->ia_get = NULL;

  a->a_more = ia;
  a->a_type = type;

  return 0;
}

int set_value_intbool_katcp(struct katcp_acquire *a, int value)
{
  struct katcp_integer_acquire *ia;

  ia = a->a_more;

  if(ia == NULL){
    return -1;
  }

  ia->ia_current = value;

  return 0;
}

int scan_value_intbool_katcp(struct katcp_acquire *a, char *value)
{
  struct katcp_integer_acquire *ia;
  char *end;
  int got;

  ia = a->a_more;

  if(ia == NULL){
    return -1;
  }

  if(value == NULL){
    return -1;
  }

  got = strtol(value, &end, 0);
  switch(end[0]){
    case ' '  :
    case '\0' :
    case '\r' :
    case '\n' :
      break;
    default  :
      return -1;
  }

  ia->ia_current = got;

  return 0;
}

int has_poll_intbool_katcp(struct katcp_acquire *a)
{
  struct katcp_integer_acquire *ia;

  ia = a->a_more;

  if(ia && ia->ia_get){
    return 1;
  }

  return 0;
}

/*********************************************************************/

int create_sensor_integer_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn, int min, int max)
{
  struct katcp_integer_sensor *is;

  if((sn == NULL) || (sn->s_type != KATCP_SENSOR_INTEGER)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to layer integer sensor over %p", sn);
    return -1;
  }

  is = malloc(sizeof(struct katcp_integer_sensor));
  if(is == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for integer sensor structure", sizeof(struct katcp_integer_sensor));
    return -1;
  }

  is->is_current = 0;
  is->is_min = min;
  is->is_max = max;

  sn->s_more = is;

  return 0;
}

int create_nonsense_intbool_katcp(struct katcp_dispatch *d, struct katcp_nonsense *ns)
{
  struct katcp_integer_nonsense *in;
  struct katcp_integer_sensor *is;
  struct katcp_sensor *sn;

  sn = ns->n_sensor;

  if(sn == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "null sensor given to intbool create function");
    return -1;
  }

  switch(sn->s_type){
    case KATCP_SENSOR_INTEGER :
    case KATCP_SENSOR_BOOLEAN :
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "incapable of creating sensor client of type %d", sn->s_type);
      return -1;
  }

  is = sn->s_more;
  in = malloc(sizeof(struct katcp_integer_nonsense));
  if(in == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for integer sensor structure", sizeof(struct katcp_integer_nonsense));
    return -1;
  }

  /* changed as per request from simon to always report initial value for event */
  in->in_previous = is->is_max + 1; 
  in->in_delta = 1; /* guessing at a reasonable default */

  ns->n_more = in;

  return 0;
}

int append_type_integer_katcp(struct katcp_dispatch *d, int flags, struct katcp_sensor *sn)
{
  struct katcp_integer_sensor *is;

  if(sn == NULL){
    return -1;
  }

  is = sn->s_more;

  if(append_signed_long_katcp(d, KATCP_FLAG_SLONG | (flags & KATCP_FLAG_FIRST) , (unsigned long)(is->is_min)) < 0){
    return -1;
  }

  return append_signed_long_katcp(d, KATCP_FLAG_SLONG | (flags & KATCP_FLAG_LAST), (unsigned long)(is->is_max));
}

int append_value_intbool_katcp(struct katcp_dispatch *d, int flags, struct katcp_sensor *sn)
{
  struct katcp_integer_sensor *is;

  if(sn == NULL){
    return -1;
  }

  is = sn->s_more;

  return append_signed_long_katcp(d, KATCP_FLAG_SLONG | (flags & (KATCP_FLAG_FIRST | KATCP_FLAG_LAST)), (long)(is->is_current));
}

int append_diff_integer_katcp(struct katcp_dispatch *d, int flags, struct katcp_nonsense *ns)
{
  struct katcp_integer_nonsense *in;

  if(ns == NULL){
    return -1;
  }

  in = ns->n_more;

  return append_signed_long_katcp(d, KATCP_FLAG_SLONG | (flags & (KATCP_FLAG_FIRST | KATCP_FLAG_LAST)), (long)(in->in_delta));
}

int scan_diff_integer_katcp(struct katcp_nonsense *ns, char *extra)
{
  struct katcp_integer_nonsense *in;
  char *end;
  long value;

  if(ns == NULL){
    return -1;
  }

  if(extra == NULL){
    return -1;
  }

  in = ns->n_more;

  value = strtol(extra, &end, 0);
  switch(end[0]){
    case ' '  :
    case '\0' :
    case '\r' :
    case '\n' :
      break;
    default  :
      return -1;
  }

  in->in_delta = value;

  return 0;
}

/* no none_check() */
/* no period_check() */

int event_check_intbool_katcp(struct katcp_nonsense *ns)
{
  struct katcp_sensor *sn;
  struct katcp_integer_sensor *is;
  struct katcp_integer_nonsense *in;
  struct katcp_dispatch *dx;
  int result;

  sn = ns->n_sensor;
  if(sn == NULL){
    fprintf(stderr, "major logic problem: null sensor for client while checking for event\n");
    abort();
  }

  dx = ns->n_client;

  switch(sn->s_type){
    case KATCP_SENSOR_INTEGER :
    case KATCP_SENSOR_BOOLEAN :
      break;
    default :
      log_message_katcp(dx, KATCP_LEVEL_FATAL, NULL, "called intbool comparison on type %d", sn->s_type);
      return 0;
  }

  is = sn->s_more;
  in = ns->n_more;

  log_message_katcp(dx, KATCP_LEVEL_TRACE, NULL, "intbool event check had %d now %d", in->in_previous, is->is_current);

  result = status_check_katcp(ns); /* WARNING: status_check has the side effect of updating status */
  
  if(in->in_previous == is->is_current){
    return result;
  }

  in->in_previous = is->is_current;

  return 1;
}

/* integer specific logic *********************************************************************/

int diff_check_integer_katcp(struct katcp_nonsense *ns)
{
  struct katcp_sensor *sn;
  struct katcp_integer_sensor *is;
  struct katcp_integer_nonsense *in;
  int delta;

  sn = ns->n_sensor;

#ifdef DEBUG
  if((sn == NULL) || (sn->s_type != KATCP_SENSOR_INTEGER)){
    fprintf(stderr, "major logic problem: diff integer check not run on integer\n");
    abort();
  }
#endif

  is = sn->s_more;
  in = ns->n_more;

  delta = abs(is->is_current - in->in_previous);

  if(delta < in->in_delta){
    /* still within range */
    return 0;
  }
  
  in->in_previous = is->is_current;

  return 1;
}

int set_integer_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, int value)
{
  int result;

  result = set_value_intbool_katcp(a, value);

  propagate_acquire_katcp(d, a);

  return result;
}

/* boolean specific logic *********************************************************************/

int extract_direct_boolean_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{

  struct katcp_acquire *a;
  struct katcp_integer_acquire *ia;
  struct katcp_integer_sensor *is;

  a = sn->s_acquire;

  if((a == NULL) || (a->a_type != KATCP_SENSOR_BOOLEAN) || (sn->s_type != KATCP_SENSOR_BOOLEAN)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "type mismatch for putative boolean sensor %s", sn->s_name);
    return -1;
  }

  is = sn->s_more;
  ia = a->a_more;

  if((ia->ia_current < 0) || (ia->ia_current > 1)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "bad extracted integer (%d) for sensor %s", ia->ia_current, sn->s_name);
    return -1;
  }

  is->is_current = ia->ia_current;

  /* consider true ok, false an error */
  if(ia->ia_current){
    set_status_sensor_katcp(sn, KATCP_STATUS_NOMINAL);
  } else {
    set_status_sensor_katcp(sn, KATCP_STATUS_ERROR);
  }

  return 0;
}

int extract_invert_boolean_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{

  struct katcp_acquire *a;
  struct katcp_integer_acquire *ia;
  struct katcp_integer_sensor *is;

  a = sn->s_acquire;

  if((a == NULL) || (a->a_type != KATCP_SENSOR_BOOLEAN) || (sn->s_type != KATCP_SENSOR_BOOLEAN)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "type mismatch for putative boolean sensor %s", sn->s_name);
    return -1;
  }

  is = sn->s_more;
  ia = a->a_more;

  if((ia->ia_current < 0) || (ia->ia_current > 1)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "bad extracted integer (%d) for sensor %s", ia->ia_current, sn->s_name);
    return -1;
  }

  is->is_current = ia->ia_current;

  /* consider true an error, false ok */
  if(ia->ia_current){
    set_status_sensor_katcp(sn, KATCP_STATUS_ERROR);
  } else {
    set_status_sensor_katcp(sn, KATCP_STATUS_NOMINAL);
  }

  return 0;
}

int create_sensor_boolean_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_integer_sensor *is;

  if((sn == NULL) || (sn->s_type != KATCP_SENSOR_BOOLEAN)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to layer boolean sensor over %p", sn);
    return -1;
  }

  is = malloc(sizeof(struct katcp_integer_sensor));
  if(is == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %d bytes for boolean sensor structure", sizeof(struct katcp_integer_sensor));
    return -1;
  }

  is->is_current = 0;
  is->is_min = 0;
  is->is_max = 1;

  sn->s_more = is;

  return 0;
}

int set_boolean_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, int value)
{
  int result;

  result = set_value_intbool_katcp(a, value);

  propagate_acquire_katcp(d, a);

  return result;
}

/* populate type dispatch lookup **************************************************************/

static struct katcp_check_table type_lookup_table[] = {
  [KATCP_SENSOR_INTEGER] = 
    {  "integer",
      &extract_intbool_katcp,
      &create_acquire_intbool_katcp,
      &create_nonsense_intbool_katcp,
      &append_type_integer_katcp,
      &append_value_intbool_katcp,
      &append_diff_integer_katcp,
      &scan_diff_integer_katcp,
      &scan_value_intbool_katcp,
      &has_poll_intbool_katcp,
      { 
         NULL, /* never used, strategy none causes the deletion of the matching nonsense structure */
        &period_check_katcp,
        &event_check_intbool_katcp,
        &diff_check_integer_katcp,
        &force_check_katcp
      }
    }, 
  [KATCP_SENSOR_BOOLEAN] = 
    {  "boolean",
      &extract_intbool_katcp,
      &create_acquire_intbool_katcp,
      &create_nonsense_intbool_katcp,
       NULL,
      &append_value_intbool_katcp,
       NULL,
       NULL,
      &scan_value_intbool_katcp,
      &has_poll_intbool_katcp,
      { 
         NULL, /* never used, strategy none causes the deletion of the matching nonsense structure */
        &period_check_katcp,
        &event_check_intbool_katcp,
         NULL, /* diff is a tautology, same as event */
        &force_check_katcp
      }
    }, 
  [KATCP_SENSOR_DISCRETE] = /* currently unimplemented */
    {  NULL, 
       NULL, 
       NULL, 
       NULL, 
       NULL,
       NULL, 
       NULL,
       NULL,
       NULL,
       NULL,
      { 
         NULL,
        &period_check_katcp,
         NULL,
         NULL,
        &force_check_katcp
      }
    },
  [KATCP_SENSOR_LRU] = /* currently implemented */
    {  NULL, 
       NULL, 
       NULL, 
       NULL, 
       NULL,
       NULL, 
       NULL,
       NULL,
       NULL,
       NULL,
      { 
         NULL,
        &period_check_katcp,
         NULL,
         NULL,
        &force_check_katcp
      }
    }
#ifdef KATCP_USE_FLOATS
    ,
  [KATCP_SENSOR_FLOAT] = 
    {  "float",
      &extract_double_katcp,
      &create_acquire_double_katcp,
      &create_nonsense_double_katcp,
      &append_type_double_katcp,
      &append_value_double_katcp,
      &append_diff_double_katcp,
      &scan_diff_double_katcp,
      &scan_value_double_katcp,
      &has_poll_double_katcp,
      { 
         NULL, /* never used, strategy none causes the deletion of the matching nonsense structure */
        &period_check_katcp,
        &event_check_double_katcp,
        &diff_check_double_katcp,
        &force_check_katcp
      }
    }
#endif 
};

/* acquire routines: the stuff that actually gets the sensor data ********/

/* deallocate acquire instance, unlinking it from all its sensors */

void destroy_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a)
{
  int i;
  struct katcp_sensor *s;
  struct katcp_integer_acquire *ia;
  struct katcp_double_acquire *da;

  if(a->a_release){
    (*(a->a_release))(d, a);
    a->a_release = NULL;
  }
  a->a_local = NULL;
  for(i = 0; i < a->a_count; i++){
    s = a->a_sensors[i];
    if(s){
      s->s_acquire = NULL;
    }
  }

  if(a->a_sensors){
    free(a->a_sensors);
    a->a_sensors = NULL;
  }
  a->a_count = 0;

  if(a->a_periodics){
    discharge_timer_katcp(d, a);
  }
  a->a_periodics = 0;
  a->a_users = 0;

  if(a->a_more){
    switch(a->a_type){
      case KATCP_SENSOR_INTEGER :
      case KATCP_SENSOR_BOOLEAN :
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "destroying acquire %p type %d", a, a->a_type);

        ia = a->a_more;
        ia->ia_get = NULL;
        break;

#ifdef KATCP_USE_FLOATS
      case KATCP_SENSOR_FLOAT :
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "destroying acquire %p type %d", a, a->a_type);

        da = a->a_more;
        da->da_get = NULL;
        break;
#endif

      default : 
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "destroying unsupported sensor type %d", a->a_type);
        break;
    }
    free(a->a_more);
    a->a_more = NULL;
  }

  free(a);
}

/* create empty, unconnected acquire instance */

void adjust_acquire_katcp(struct katcp_acquire *a, struct timeval *defpoll, struct timeval *maxrate)
{
  if(defpoll){
    a->a_poll.tv_sec  = defpoll->tv_sec;
    a->a_poll.tv_usec = defpoll->tv_usec;
  } else {
    a->a_poll.tv_sec  = SENSOR_POLL_SECONDS; 
    a->a_poll.tv_usec = 0;
  }

  if(maxrate){
    a->a_limit.tv_sec  = maxrate->tv_sec;
    a->a_limit.tv_usec = maxrate->tv_usec;
  } else {
    a->a_limit.tv_sec  = 0;
    a->a_limit.tv_usec = SENSOR_LIMIT_MICROSECONDS;
  }
}

static struct katcp_acquire *extended_create_acquire_katcp(struct katcp_dispatch *d, int type, void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a), struct timeval *defpoll, struct timeval *maxrate)
{
  struct katcp_acquire *a;

  if((type < 0) || (type >= KATCP_SENSORS_COUNT)){
    return NULL;
  }

  a = malloc(sizeof(struct katcp_acquire));
  if(a == NULL){
    return NULL;
  }

  a->a_sensors = NULL;
  a->a_count = 0;

  a->a_type = KATCP_SENSOR_INVALID;
  a->a_users = 0;
  a->a_periodics = 0;

  adjust_acquire_katcp(a, defpoll, maxrate);

  /* a->a_current.tv_sec */
  /* a->a_current.tv_usec */

  a->a_last.tv_sec = 0;
  a->a_last.tv_usec = 0;

  a->a_local = local;
  a->a_release = release;

  a->a_more = NULL; 

  if((*(type_lookup_table[type].c_create_acquire))(d, a, type) < 0){
    destroy_acquire_katcp(d, a);
    return NULL;
  }
  
  return a;
}

#if 0
static struct katcp_acquire *create_acquire_katcp(struct katcp_dispatch *d, int type)
{
  return extended_create_acquire_katcp(d, type, NULL, NULL, NULL, NULL);
}
#endif

struct katcp_acquire *acquire_from_sensor_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  return sn ? sn->s_acquire : NULL;
}

/**************************************************************************/

struct katcp_acquire *setup_intbool_acquire_katcp(struct katcp_dispatch *d, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a), int type)
{
  struct katcp_acquire *a;
  struct katcp_integer_acquire *ia;

  a = extended_create_acquire_katcp(d, type, local, release, NULL, NULL);
  if(a == NULL){
    return NULL;
  }

  ia = a->a_more;

  ia->ia_current = 0;
  ia->ia_get = get;

#ifdef DEBUG
  fprintf(stderr, "intbool: acquire %p populated with get %p\n", ia, ia->ia_get);
#endif

  return a;
}

struct katcp_acquire *setup_integer_acquire_katcp(struct katcp_dispatch *d, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a))
{
  return setup_intbool_acquire_katcp(d, get, local, release, KATCP_SENSOR_INTEGER);
}

struct katcp_acquire *setup_boolean_acquire_katcp(struct katcp_dispatch *d, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a))
{
  return setup_intbool_acquire_katcp(d, get, local, release, KATCP_SENSOR_BOOLEAN);
}

struct katcp_acquire *setup_double_acquire_katcp(struct katcp_dispatch *d, double (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a))
{
  struct katcp_acquire *a;
  struct katcp_double_acquire *da;

  a = extended_create_acquire_katcp(d, KATCP_SENSOR_FLOAT, local, release, NULL, NULL);
  if(a == NULL){
    return NULL;
  }

  da = a->a_more;

  da->da_current = 0;
  da->da_get = get;

#ifdef DEBUG
  fprintf(stderr, "double: acquire %p populated with get %p\n", da, da->da_get);
#endif

  return a;
}


/* link acquire to sensor, have acquire adopt type of sensor if emtpy */

static int link_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, struct katcp_sensor *sn, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn))
{
  struct katcp_sensor **tmp;
  int (*e)(struct katcp_dispatch *d, struct katcp_sensor *s);

  if(extract){
    e = extract;
  } else {
    if(sn->s_type == a->a_type){
      if((sn->s_type < 0) || (sn->s_type >= KATCP_SENSORS_COUNT)){
        fprintf(stderr, "major logic problem: types are out of range\n");
        abort();
      }
      e = type_lookup_table[sn->s_type].c_extract; 
    } else {
      if(extract == NULL){
#ifdef DEBUG
        fprintf(stderr, "link: unable to link mismatched types implicitly (acquire=%d, sensor=%d)\n", a->a_type, sn->s_type );
        abort();
#endif
        return -1;
      }
    }
  }

  tmp = realloc(a->a_sensors, (a->a_count + 1) * sizeof(struct katcp_sensor *));
  if(tmp == NULL){
    return -1;
  }

  a->a_sensors = tmp;

  a->a_sensors[a->a_count] = sn;
  a->a_count++;

  sn->s_acquire = a;
  sn->s_extract = e;

  return 0;
}

/* remove acquire from sensor, delete acquire if no more references */

static int del_acquire_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_acquire *a;
  int i, match;

  if((sn == NULL) || (sn->s_acquire == NULL)){
    return 0;
  }

  a = sn->s_acquire;
  match = 0;

  i = 0;
  while(i < a->a_count){
    if(a->a_sensors[i] == sn){
      a->a_count--;
      if(i < a->a_count){
        a->a_sensors[i] = a->a_sensors[a->a_count];
      }
      match++;
    } else {
      i++;
    }
  }

#ifdef DEBUG
  if(match != 1){
    fprintf(stderr, "del: major logic problem: found %d matches for sensor in acquire logic\n", match);
    abort();
  }
#endif

  if(a->a_count == 0){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "collecting acquire routine for sensor %s", sn->s_name);
    destroy_acquire_katcp(d, a);
    return 1;
  }

  return 0;
}

/* core function invoked to emit sensor notifications ********************************/

int run_timer_acquire_katcp(struct katcp_dispatch *d, void *data)
{
  struct katcp_acquire *a;

  a = data;

  if(a == NULL){
    return -1;
  }

#ifdef DEBUG
  if(a->a_periodics <= 0){
    fprintf(stderr, "run: major logic failure: polling acquire %p from timer without any polling clients (%d users)\n", a, a->a_users);
    abort();
  }
  fprintf(stderr, "sensor: running acquire %p with %d users, %d periodic\n", a, a->a_users, a->a_periodics);
#endif

  return run_acquire_katcp(d, a, 0);
}

static int run_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a, int forced)
{
  struct katcp_integer_acquire *ia;
  struct katcp_double_acquire *da;
  struct timeval now, legal;
  struct katcp_shared *s;

  s = d->d_shared;

  /* TODO: deschedule sensor if we have left our mode: should be done before first acquire attempt (!) */
  /* should move mode to acquire, rather than sensor */

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "sensor: running acquire %p with %d sensors %d users of which %d periodic\n", a, a->a_count, a->a_users, a->a_periodics);

  gettimeofday(&now, NULL);

  add_time_katcp(&legal, &(a->a_last), &(a->a_limit));
  if(cmp_time_katcp(&now, &legal) < 0){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "refusing to run acquire now %lu.%06lu but can after %lu.%06lu", now.tv_sec, now.tv_usec, legal.tv_sec, legal.tv_usec);
  } else {
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "running acquire instance %p (type=%d) with %d sensors", a, a->a_type, a->a_count);

    switch(a->a_type){
      case KATCP_SENSOR_INTEGER :
      case KATCP_SENSOR_BOOLEAN :
        ia = a->a_more;
        if(ia->ia_get){
          ia->ia_current = (*(ia->ia_get))(d, a);
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "acquired integer result %d for %p", ia->ia_current, a);
        }
        break;
      case KATCP_SENSOR_FLOAT :
        da = a->a_more;
        if(da->da_get){
          da->da_current = (*(da->da_get))(d, a);
          log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "acquired floating point result %e for %p", da->da_current, a);
        }
        break;
      default :
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unsupported sensor type %d", a->a_type);
        break;
    }
    a->a_last.tv_sec  = now.tv_sec;
    a->a_last.tv_usec = now.tv_usec;
  }

  propagate_acquire_katcp(d, a);

  return 0;
}

int propagate_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a)
{
  int j, i;
  struct katcp_sensor *sn;
  struct katcp_nonsense *ns;
  struct katcp_dispatch *dx;
  struct timeval now;

  gettimeofday(&now, NULL);

  for(j = 0; j < a->a_count; j++){

    sn = a->a_sensors[j];
    sane_sensor(sn);

#ifdef DEBUG
    if(sn->s_extract == NULL){
      fprintf(stderr, "run: logic problem: null sensor acquire function\n");
      abort();
    }
#endif

    if((*(sn->s_extract))(d, sn) >= 0){ /* got a useful value */

      log_message_katcp(d, KATCP_LEVEL_TRACE | KATCP_LEVEL_LOCAL, NULL, "checking %d clients of %s", sn->s_refs, sn->s_name);

      for(i = 0; i < sn->s_refs; i++){
        ns = sn->s_nonsense[i];
        sane_nonsense(ns);
#ifdef DEBUG
        if((ns == NULL) || (ns->n_client == NULL)){
          fprintf(stderr, "run: logic problem: null nonsense fields\n");
          abort();
        }
#endif
        dx = ns->n_client;

#ifdef DEBUG
        if((ns->n_strategy < 0) || (ns->n_strategy >= KATCP_STRATEGIES_COUNT)){
          fprintf(stderr, "run: logic problem: invalid strategy %d\n", ns->n_strategy);
          abort();
        }
#endif

        sn->s_recent.tv_sec = now.tv_sec;
        sn->s_recent.tv_usec = now.tv_usec;

        log_message_katcp(d, KATCP_LEVEL_TRACE | KATCP_LEVEL_LOCAL, NULL, "calling check function %p (type %d, strategy %d)", type_lookup_table[sn->s_type].c_checks[ns->n_strategy], sn->s_type, ns->n_strategy);

        if(type_lookup_table[sn->s_type].c_checks[ns->n_strategy]){
          
          if((*(type_lookup_table[sn->s_type].c_checks[ns->n_strategy]))(ns)){
            log_message_katcp(d, KATCP_LEVEL_TRACE | KATCP_LEVEL_LOCAL, NULL, "strategy %d reports a match", ns->n_strategy);
            /* TODO: needs work for having tags in katcp messages */
            generic_sensor_update_katcp(dx, sn, (ns->n_strategy == KATCP_STRATEGY_FORCED) ? "#sensor-value" : "#sensor-status");
          }
        }
      }
    } else {
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "extract function for sensor %s failed", sn->s_name);
    }
  }

  return 0;
}

void *get_local_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a)
{
  if(a == NULL){
    return NULL;
  }

  return a->a_local;
}

void generic_release_local_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a)
{
  if(a == NULL){
    return;
  }

  if(a->a_local == NULL){
    return;
  }

  free(a->a_local);
}

int is_up_acquire_katcp(struct katcp_dispatch *d, struct katcp_acquire *a)
{
  if(a == NULL){
    return 0;
  }

  return a->a_users;
}

/*************************************************************************/

static struct katcp_sensor *create_sensor_katcp(struct katcp_dispatch *d, char *name, char *description, char *units, int preferred, int type, int mode)
{
  struct katcp_sensor *sn, **tmp;
  struct katcp_shared *s;

  s = d->d_shared;

  if(name == NULL){
    return NULL;
  }

  tmp = realloc(s->s_sensors, sizeof(struct katcp_sensor *) * (s->s_tally + 1));
  if(tmp == NULL){
    return NULL;
  }
  s->s_sensors = tmp;

  sn = malloc(sizeof(struct katcp_sensor));
  if(sn == NULL){
    return NULL;
  }

  sn->s_magic = SENSOR_MAGIC;
  sn->s_type = type;

  sn->s_name = NULL;
  sn->s_description = NULL;
  sn->s_units = NULL;

  sn->s_preferred = preferred;

  sn->s_status = KATCP_STATUS_UNKNOWN;
  sn->s_mode = mode;

  sn->s_recent.tv_sec = 0;
  sn->s_recent.tv_usec = 0;

  sn->s_refs = 0;
  sn->s_nonsense = NULL;

  sn->s_acquire = NULL;

  s->s_sensors[s->s_tally] = sn;
  s->s_tally++;

  sn->s_name = strdup(name);
  if(sn->s_name == NULL){
    destroy_sensor_katcp(d, sn);
    return NULL;
  }

  if(description){
    sn->s_description = strdup(description);
    if(sn->s_description == NULL){
      destroy_sensor_katcp(d, sn);
      return NULL;
    }
  }
  if(units){
    sn->s_units = strdup(units);
    if(sn->s_units == NULL){
      destroy_sensor_katcp(d, sn);
      return NULL;
    }
  }

  return sn;
}

static void destroy_sensor_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_shared *s;
  int i;

  if(sn == NULL){
    return;
  }

  s = d->d_shared;

#if 0
  if(s->s_more){
    if((s->s_type >= 0) && (s->s_type < KATCP_SENSORS_COUNT)){
      (*(sensor_type_table[s->s_type].sd_destroy))(s);
    }
    s->s_more = NULL;
  }
#endif

#ifdef DEBUG
  if(sn->s_magic != SENSOR_MAGIC){
    fprintf(stderr, "sensor: logic problem: bad magic\n");
    abort();
  }
#endif

  /* destroy all nonsensors referencing this sensor */
  if(sn->s_refs > 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "releasing sensor with %d clients", sn->s_refs);
    while(sn->s_refs > 0){
      destroy_nonsense_katcp(d, sn->s_nonsense[0]);
    }
  }

  /* remove timer */
  del_acquire_katcp(d, sn);

  /* remove sensor from shared */
  i = 0;
  while(i < s->s_tally){
    if(s->s_sensors[i] == sn){
      s->s_tally--;
      if(i < s->s_tally){
        s->s_sensors[i] = s->s_sensors[s->s_tally];
      }
    } else {
      i++;
    }
  }
  if(s->s_tally <= 0){
    if(s->s_sensors){
      free(s->s_sensors);
      s->s_sensors = NULL;
    }
  }

  sn->s_magic = 0;
  sn->s_type = KATCP_SENSOR_INVALID;

  if(sn->s_name){
    free(sn->s_name);
    sn->s_name = NULL;
  }
  if(sn->s_description){
    free(sn->s_description);
    sn->s_description = NULL;
  }
  if(sn->s_units){
    free(sn->s_units);
    sn->s_units = NULL;
  }

  /* MAYBE have a routine to clean out. Will be needed for discrete */
  if(sn->s_more){ 
    free(sn->s_more);
    sn->s_more = NULL;
  }

  free(sn);
}

static int reload_sensor_katcp(struct katcp_dispatch *d, struct katcp_sensor *sz)
{
  int i, j;
  struct katcp_nonsense *ns;
  struct katcp_sensor *sn;
  struct katcp_acquire *a;
  struct katcp_shared *s;
  int users, periodics, polling;

  s = d->d_shared;
  a = sz->s_acquire;

  if((a == NULL) || (s == NULL)){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "no acquisition logic");
    return -1;
  }

  sane_acquire(a);

  polling = (*(type_lookup_table[a->a_type].c_has_poll))(a);

  periodics = 0;
  users = 0;

  for(j = 0; j < a->a_count; j++){
    sn = a->a_sensors[j];
    sane_sensor(sn);
    if(sn == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "null sensor at %d during acquire", j);
      return -1;
    }
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "scheduling %s with %d references", sn->s_name, sn->s_refs);
    if(sn->s_refs > 0){
      for(i = 0; i < sn->s_refs; i++){
        ns = sn->s_nonsense[i];
        sane_nonsense(ns);

        switch(ns->n_strategy){
          case KATCP_STRATEGY_FORCED :
#ifdef DEBUG
            if(sn->s_refs > 1){
              log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "expected forced acquisition of sensor %s to only have one reference, not %d", sn->s_name, sn->s_refs);
            }
#endif
            break;

          case KATCP_STRATEGY_OFF   :
            log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "did not expect an off strategy while recomputing poll periods\n");
            break; /* WARNING: there wasn't a break here previously */

          case KATCP_STRATEGY_EVENT : 
            if(polling == 0){
              break;
            } /* WARNING */
          default :
            if(periodics){
              if(cmp_time_katcp(&(a->a_current), &(ns->n_period)) > 0){
                a->a_current.tv_sec = ns->n_period.tv_sec;
                a->a_current.tv_usec = ns->n_period.tv_usec;
              }
            } else {
              a->a_current.tv_sec = ns->n_period.tv_sec;
              a->a_current.tv_usec = ns->n_period.tv_usec;
            }
            periodics++;
            break;
        }
      }
      users += sn->s_refs;
    }
  }

  a->a_users = users;

  /* was running timers, but they are no longer needed */
  if((a->a_periodics > 0) && (periodics == 0)){
    a->a_periodics = 0;
    discharge_timer_katcp(d, a);
    return 0;
  }

  a->a_periodics = periodics;

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "acquire %p has %d sensors with %d users of which %d poll", a, a->a_count, a->a_users, a->a_periodics);

  if(a->a_periodics == 0){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not polling aquire %p so propagation needs to be triggered elsewhere", a->a_count);
    return 0;
  }

  if(register_every_tv_katcp(d, &(a->a_current), &run_timer_acquire_katcp, a) < 0){
    return -1;
  }

  return 0;
}

static struct katcp_nonsense *match_nonsense_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  int i;
  struct katcp_nonsense *ns;

  for(i = 0; i < d->d_size; i++){
    ns = d->d_nonsense[i];
    if(ns == NULL){
      fprintf(stderr, "nonsense: major logic failure - nonsensor entry is empty\n");
      abort();
    }

    if(ns->n_sensor == sn){
      return ns;
    }
  }

  return NULL;
}

struct katcp_sensor *find_sensor_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_shared *s;
  struct katcp_sensor *sn;
  int i;

  s = d->d_shared;
  if(s == NULL){
    abort();
  }

  for(i = 0; i < s->s_tally; i++){
    sn = s->s_sensors[i];
    sane_sensor(sn);

    if(sn == NULL){
      abort();
    }

    if(!strcmp(sn->s_name, name)){
      if(sn->s_mode && (s->s_mode != sn->s_mode)){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "sensor %s not available in current mode", name);
      } else {
        return sn;
      }
    }
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "no match for sensor name %s", name);

  return NULL;
}

struct katcp_acquire *find_acquire_katcp(struct katcp_dispatch *d, char *name)
{
  struct katcp_sensor *sn;

  sn = find_sensor_katcp(d, name);
  if(sn == NULL){
    return NULL;
  }

  return sn->s_acquire;
}

void destroy_sensors_katcp(struct katcp_dispatch *d)
{
  struct katcp_shared *s;

  s = d->d_shared;

  while(s->s_tally){
    destroy_sensor_katcp(d, s->s_sensors[0]);
  }

#ifdef DEBUG
  if(s->s_sensors){
    fprintf(stderr, "destroy: logic failure: bad sensor destroy\n");
  }
#endif
}

/* setup for shadow copies of sensor (nonsensors ;) **********************/

static struct katcp_nonsense *create_nonsense_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_nonsense *ns, *nt, **tmp;
  struct katcp_acquire *a;
  int result;

  if(sn == NULL){
    return NULL;
  }

  if((sn->s_type < 0) || (sn->s_type >= KATCP_SENSORS_COUNT)){
    fprintf(stderr, "logic failure: sensor %s reports type %d\n", sn->s_name, sn->s_type);
    return NULL;
  }

  a = sn->s_acquire;
  if(a == NULL){
    return NULL;
  }

  tmp = realloc(d->d_nonsense, sizeof(struct katcp_nonsense *) * (d->d_size + 1));
  if(tmp == NULL){
    return NULL;
  }
  d->d_nonsense = tmp;

  tmp = realloc(sn->s_nonsense, sizeof(struct katcp_nonsense *) * (sn->s_refs + 1));
  if(tmp == NULL){
    return NULL;
  }
  sn->s_nonsense = tmp;

  ns = malloc(sizeof(struct katcp_nonsense));
  if(ns == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate client state for sensor %s", sn->s_name);
    return NULL;
  }

  ns->n_magic = NONSENSE_MAGIC;
  ns->n_client = d;

  ns->n_sensor = sn;
  ns->n_strategy = KATCP_STRATEGY_OFF;
  ns->n_status = sn->s_status;

  ns->n_period.tv_sec  = a->a_current.tv_sec;
  ns->n_period.tv_usec = a->a_current.tv_usec;

  /* n_next always has to be plausible. If bad, eg 0, then it will update in increments of n_period until it reaches now */
  if(sn->s_refs > 0){
    nt = sn->s_nonsense[0];
    ns->n_next.tv_sec  = nt->n_next.tv_sec;
    ns->n_next.tv_usec = nt->n_next.tv_usec;
  } else {
    gettimeofday(&(ns->n_next), NULL);
  }

  ns->n_manual = 1;

  ns->n_more = NULL;

  result = (*(type_lookup_table[sn->s_type].c_create_nonsense))(d, ns);
  if(result < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate client type state for sensor %s", sn->s_name);
    free(ns); /* WARNING: bit iffy */
    return NULL;
  }

  sn->s_nonsense[sn->s_refs] = ns;
  sn->s_refs++;

  d->d_nonsense[d->d_size] = ns;
  d->d_size++;

  return ns;
}

static struct katcp_nonsense *find_nonsense_katcp(struct katcp_dispatch *d, char *name)
{
  int i;
  struct katcp_sensor *sn;
  struct katcp_nonsense *ns;
  struct katcp_shared *s;

  s = d->d_shared;

  for(i = 0; i < d->d_size; i++){
    ns = d->d_nonsense[i];
    sane_nonsense(ns);
    if(ns == NULL){
      fprintf(stderr, "nonsense: major logic failure - nonsensor entry is empty\n");
      abort();
    }
    sn = ns->n_sensor;
    if(sn == NULL){
      fprintf(stderr, "nonsense: bad sensor reference\n");
      abort();
    }

    if(!strcmp(sn->s_name, name)){
      if(sn->s_mode && (s->s_mode != sn->s_mode)){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "sensor %s not available in current mode", name);
      } else {
        return ns;
      }
    }
  }

  return NULL;
}

static void destroy_nonsense_katcp(struct katcp_dispatch *d, struct katcp_nonsense *ns)
{
  struct katcp_sensor *sn;
  struct katcp_dispatch *dx;
  int i;

  sane_nonsense(ns);

  sn = ns->n_sensor;

  /* exise nonsensor from sensor */
  i = 0;
  while(i < sn->s_refs){
    if(sn->s_nonsense[i] == ns){
      sn->s_refs--;
      if(i < sn->s_refs){
        sn->s_nonsense[i] = sn->s_nonsense[sn->s_refs];
      }
    } else {
      i++;
    }
  }
  if(sn->s_refs <= 0){
    if(sn->s_nonsense){
      free(sn->s_nonsense);
      sn->s_nonsense = NULL;
    }
  }
  ns->n_sensor = NULL;

  reload_sensor_katcp(d, sn);

  if(ns->n_client){
    dx = ns->n_client;

    /* remove from dispatch entry */
    i = 0;
    while(i < dx->d_size){
      if(dx->d_nonsense[i] == ns){
        dx->d_size--;
        if(i < dx->d_size){
          dx->d_nonsense[i] = dx->d_nonsense[dx->d_size];
        }
      } else {
        i++;
      }
    }
    if(dx->d_size <= 0){
      if(dx->d_nonsense){
        free(dx->d_nonsense);
        dx->d_nonsense = NULL;
      }
    }
  }

  /* MAYBE have a function to clean out type specific stuff */
  if(ns->n_more){
    free(ns->n_more);
  }

  free(ns);
}

void destroy_nonsensors_katcp(struct katcp_dispatch *d)
{
  while(d->d_size){
    destroy_nonsense_katcp(d, d->d_nonsense[0]);
  }
}

/* entry points for something wishing to register a sensor ***************/

/* plain vanillia integer registration ***********************************/

int register_integer_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a), int min, int max)
{
  struct katcp_sensor *sn;
  struct katcp_acquire *a;

  sn = create_sensor_katcp(d, name, description, units, KATCP_STRATEGY_EVENT, KATCP_SENSOR_INTEGER, mode);
  if(sn == NULL){
    return -1;
  }

  if(create_sensor_integer_katcp(d, sn, min, max) < 0){
    destroy_sensor_katcp(d, sn);
    return -1;
  }

  a = setup_integer_acquire_katcp(d, get, local, release);
  if(a == NULL){
    destroy_sensor_katcp(d, sn);
    return -1;
  }

  if(link_acquire_katcp(d, a, sn, NULL)){
    destroy_sensor_katcp(d, sn);
    destroy_acquire_katcp(d, a);
    return -1;
  }

  return 0;
}

/* integer registration with one acquire handling multiple sensors *******/

int register_multi_integer_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int min, int max, struct katcp_acquire *a, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn))
{
  struct katcp_sensor *sn;

  sn = create_sensor_katcp(d, name, description, units, KATCP_STRATEGY_EVENT, KATCP_SENSOR_INTEGER, mode);
  if(sn == NULL){
    return -1;
  }

  if(create_sensor_integer_katcp(d, sn, min, max) < 0){
    destroy_sensor_katcp(d, sn);
    return -1;
  }

  if(link_acquire_katcp(d, a, sn, extract)){
    destroy_sensor_katcp(d, sn);
    return -1;
  }

  return 0;
}

/* plain vanillia boolean registration ***********************************/

int register_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, int (*get)(struct katcp_dispatch *d, struct katcp_acquire *a), void *local, void (*release)(struct katcp_dispatch *d, struct katcp_acquire *a))
{
  struct katcp_sensor *sn;
  struct katcp_acquire *a;

  sn = create_sensor_katcp(d, name, description, units, KATCP_STRATEGY_EVENT, KATCP_SENSOR_BOOLEAN, mode);
  if(sn == NULL){
    return -1;
  }

  if(create_sensor_boolean_katcp(d, sn) < 0){
    destroy_sensor_katcp(d, sn);
    return -1;
  }

  a = setup_boolean_acquire_katcp(d, get, local, release);
  if(a == NULL){
    destroy_sensor_katcp(d, sn);
    return -1;
  }

  if(link_acquire_katcp(d, a, sn, NULL)){
    destroy_sensor_katcp(d, sn);
    destroy_acquire_katcp(d, a);
    return -1;
  }

  return 0;
}

/* fancy boolean registration, set up sensor off existing acquire ********/

int register_multi_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, struct katcp_acquire *a, int (*extract)(struct katcp_dispatch *d, struct katcp_sensor *sn))
{
  struct katcp_sensor *sn;

  sn = create_sensor_katcp(d, name, description, units, KATCP_STRATEGY_EVENT, KATCP_SENSOR_BOOLEAN, mode);
  if(sn == NULL){
    return -1;
  }

  if(create_sensor_boolean_katcp(d, sn) < 0){
    destroy_sensor_katcp(d, sn);
    return -1;
  }

  if(link_acquire_katcp(d, a, sn, extract)){
    destroy_sensor_katcp(d, sn);
    return -1;
  }

  return 0;
}

int register_direct_multi_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, struct katcp_acquire *a)
{
  return register_multi_boolean_sensor_katcp(d, mode, name, description, units, a, &extract_direct_boolean_katcp);
}

int register_invert_multi_boolean_sensor_katcp(struct katcp_dispatch *d, int mode, char *name, char *description, char *units, struct katcp_acquire *a)
{
  return register_multi_boolean_sensor_katcp(d, mode, name, description, units, a, &extract_invert_boolean_katcp);
}

/* type information *************************************************/

char *type_name_sensor_katcp(struct katcp_sensor *sn)
{
  sane_sensor(sn);

  return type_lookup_table[sn->s_type].c_name;
}

int type_code_sensor_katcp(char *name)
{
  int i;

  for(i = 0; i < KATCP_SENSORS_COUNT; i++){
    if(type_lookup_table[i].c_name){
      if(!strcmp(name, type_lookup_table[i].c_name)){
        return i;
      }
    }
  }

  return -1;

#if 0
  for(i = 0; type_lookup_table[i].c_name && strcmp(name, type_lookup_table[i].c_name); i++);

  return type_lookup_table[i].c_name ? i : (-1);
#endif
}

/* strategy information *********************************************/

static char *sensor_strategy_table[KATCP_STRATEGIES_COUNT] = { "none", "period", "event", "differential", "forced" };

char *strategy_name_sensor_katcp(struct katcp_nonsense *ns)
{
  return sensor_strategy_table[ns->n_strategy];
}

static int strategy_code_sensor_katcp(char *name)
{
  int i;

  for(i = 0; i < KATCP_STRATEGIES_COUNT; i++){
    if(!strcmp(name, sensor_strategy_table[i])){
      return i;
    }
  }

  return -1;
}

/* status information ***********************************************/

static char *sensor_status_table[KATCP_STATA_COUNT] = { "unknown", "nominal", "warn", "error", "failure" };

char *status_name_sensor_katcp(struct katcp_sensor *sn)
{
  sane_sensor(sn);

  return sensor_status_table[sn->s_status];
}

char *name_status_sensor_katcl(unsigned int code)
{
  if(code >= KATCP_STATA_COUNT){
    return NULL;
  }

  return sensor_status_table[code];
}

int status_code_sensor_katcl(char *name)
{
  int i;

  for(i = 0; i < KATCP_STATA_COUNT; i++){
    if(!strcmp(name, sensor_status_table[i])){
      return i;
    }
  }

  return -1;
}

int get_status_sensor_katcp(struct katcp_sensor *sn)
{
  sane_sensor(sn);

  return sn->s_status;
}

int set_status_sensor_katcp(struct katcp_sensor *sn, int status)
{
  sane_sensor(sn);

  if((status < 0) || (status >= KATCP_STATA_COUNT)){
    return -1;
  }

  sn->s_status = status;

  return 0;
}

int scan_value_sensor_katcp(struct katcp_acquire *a, char *value)
{
  sane_acquire(a);
  
  if(type_lookup_table[a->a_type].c_scan_value == NULL){
    return -1;
  }

  return (*(type_lookup_table[a->a_type].c_scan_value))(a, value);
}

/*** sensor value and support *********************************************/

int append_sensor_value_katcp(struct katcp_dispatch *d, int flags, struct katcp_sensor *sn)
{
  sane_sensor(sn);

  if(type_lookup_table[sn->s_type].c_append_value == NULL){
    fprintf(stderr, "append value: major logic problem: no append value function\n");
    abort();
  }

  return (*(type_lookup_table[sn->s_type].c_append_value))(d, flags, sn);
}

int generic_sensor_update_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn, char *name)
{
  char *status;

  if(append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, name) < 0){
    return -1;
  }

  if(append_args_katcp(d, 0, "%lu%03lu", sn->s_recent.tv_sec, sn->s_recent.tv_usec / 1000) < 0){
    return -1;
  }

  /* dirty shortcut */
  if(append_string_katcp(d, KATCP_FLAG_STRING, "1") < 0){
    return -1;
  }

  if(append_string_katcp(d, KATCP_FLAG_STRING, sn->s_name) < 0){
    return -1;
  }

  status = status_name_sensor_katcp(sn);
  if(status == NULL){
    return -1;
  }

  if(append_string_katcp(d, KATCP_FLAG_STRING, status) < 0){
    return -1;
  }

  return append_sensor_value_katcp(d, KATCP_FLAG_LAST, sn);
}

int force_acquire_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  struct katcp_nonsense *ns;
  struct katcp_acquire *a;
  int old, result;

  if(sn == NULL){
    return -1;
  }
 
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "forcing acquisition of sensor %s considered bad form", sn->s_name);

  a = sn->s_acquire;
  ns = match_nonsense_katcp(d, sn);

  if(ns){
    old = ns->n_strategy;
    ns->n_strategy = KATCP_STRATEGY_FORCED;

    result = run_acquire_katcp(d, a, 1);

    ns->n_strategy = old;
  } else {

    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "enabling offline sensor %s temporarily", sn->s_name);

    if(configure_sensor_katcp(d, sn, KATCP_STRATEGY_FORCED, 1, NULL) < 0){
      result = (-1);
    } else {
      result = run_acquire_katcp(d, a, 1);
      configure_sensor_katcp(d, sn, KATCP_STRATEGY_OFF, 1, NULL);
    }
  }

  return result;
}

int sensor_value_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_sensor *sn;
  unsigned int count, prefix, i;
  char *name;

  s = d->d_shared;

  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  count = 0;

  name = arg_string_katcp(d, 1);
  if(name){
    prefix = strlen(name);
  } else {
    prefix = 0;
  }

  for(i = 0; i < s->s_tally; i++){
    sn = s->s_sensors[i];
    if((sn->s_mode == 0) || (s->s_mode == sn->s_mode)){
      if((name == NULL) || (!strncmp(name, sn->s_name, prefix))){
        force_acquire_katcp(d, sn);
        count++;
      }
    }
  } 

  if(name && (count == 0)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no match for %s", name);
    extra_response_katcp(d, KATCP_RESULT_INVALID, "sensor");
    return KATCP_RESULT_OWN;
  }

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_unsigned_long_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, count);

  return KATCP_RESULT_OWN;
}

/*** sensor list and support **********************************************/

int append_sensor_type_katcp(struct katcp_dispatch *d, int flags, struct katcp_sensor *sn)
{
  char *name;

  sane_sensor(sn);

  name = type_name_sensor_katcp(sn);
  if(name == NULL){
    return -1;
  }

  if(type_lookup_table[sn->s_type].c_append_type){

    if(append_string_katcp(d, KATCP_FLAG_STRING | (flags & KATCP_FLAG_FIRST), name) < 0){
      return -1;
    }

    return (*(type_lookup_table[sn->s_type].c_append_type))(d, flags & KATCP_FLAG_LAST, sn);
  } else {
    return append_string_katcp(d, KATCP_FLAG_STRING | (flags & (KATCP_FLAG_FIRST | KATCP_FLAG_LAST)), name);
  }
}

static int inform_sensor_list_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{

#if 0
  if(append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_FIRST, "#sensor-list") < 0){
    return -1;
  }
#endif

  if(prepend_inform_katcp(d) < 0){
    return -1;
  }

  if(append_string_katcp(d, KATCP_FLAG_STRING, sn->s_name) < 0){
    return -1;
  }

  if(append_string_katcp(d, KATCP_FLAG_STRING, sn->s_description) < 0){
    return -1;
  }

  if(append_string_katcp(d, KATCP_FLAG_STRING, sn->s_units) < 0){
    return -1;
  }

  return append_sensor_type_katcp(d, KATCP_FLAG_LAST, sn);
}

int sensor_list_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_sensor *sn;
  unsigned int count, i, prefix;
  char *name;

  s = d->d_shared;

  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(argc > 1){
    name = arg_string_katcp(d, 1);
    if(name == NULL){
      return KATCP_RESULT_FAIL;
    }

    prefix = strlen(name);
  } else {
    name = NULL;
    prefix = 0;
  }

  count = 0;

  for(i = 0; i < s->s_tally; i++){
    sn = s->s_sensors[i];
    if((sn->s_mode == 0) || (s->s_mode == sn->s_mode)){
      if(name == NULL || (!strncmp(name, sn->s_name, prefix))){
        if(inform_sensor_list_katcp(d, sn) < 0){
          return KATCP_RESULT_FAIL;
        }
        count++;
      }
    }
  } 

  if(name && (count == 0)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown sensor %s", name);
    extra_response_katcp(d, KATCP_RESULT_INVALID, "sensor");
    return KATCP_RESULT_OWN;
  }

#if 0
  send_katcp(d, 
      KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "!sensor-list", 
      KATCP_FLAG_STRING, KATCP_OK,
      KATCP_FLAG_LAST  | KATCP_FLAG_ULONG, (unsigned long) count);
#endif

  prepend_reply_katcp(d);
  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_unsigned_long_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, count);

  return KATCP_RESULT_OWN;
}

/* sensor sampling and support **************************************/

int has_diff_katcp(struct katcp_sensor *sn)
{
  int result;

  sane_sensor(sn);

  result = 0;

  if(type_lookup_table[sn->s_type].c_append_diff) result++;
  if(type_lookup_table[sn->s_type].c_scan_diff) result++;
  if(type_lookup_table[sn->s_type].c_checks[KATCP_STRATEGY_DIFF]) result++;

  switch(result){
    case 0 : return 0;
    case 3 : return 1;
#ifdef DEBUG
    default :
             fprintf(stderr, "extra: logic problem: have only %d/3 functions for diff strategy", result);
             abort();
             break;
#endif

  }

  return 0;
}

int append_sensor_diff_katcp(struct katcp_dispatch *d, int flags, struct katcp_nonsense *ns)
{
  struct katcp_sensor *sn;

#ifdef DEBUG
  if(ns->n_strategy != KATCP_STRATEGY_DIFF){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "odd client to be interested in appending differential when not using that strategy");
  }
#endif

  sn = ns->n_sensor;

  sane_sensor(sn);

  if(type_lookup_table[sn->s_type].c_append_diff == NULL){
    return -1;
  }

  return (*(type_lookup_table[sn->s_type].c_append_diff))(d, flags, ns);
}

int scan_sensor_diff_katcp(struct katcp_nonsense *ns, char *extra)
{
  struct katcp_sensor *sn;

  sn = ns->n_sensor;

  sane_sensor(sn);

  if(type_lookup_table[sn->s_type].c_scan_diff == NULL){
    return -1;
  }

  return (*(type_lookup_table[sn->s_type].c_scan_diff))(ns, extra);
}

static int reply_sensor_sampling_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn)
{
  char *strategy;
  int result, extra;
  unsigned long period;
  struct katcp_nonsense *ns;
  extra = 0; /* paranoid tautology */

  sane_sensor(sn);

  if(prepend_reply_katcp(d) < 0){
    return -1;
  }

  if(append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK) < 0){
    return -1;
  }

  if(append_string_katcp(d, KATCP_FLAG_STRING, sn->s_name) < 0){
    return -1;
  }

  ns = match_nonsense_katcp(d, sn);
  if(ns){
    switch(ns->n_strategy){
      case KATCP_STRATEGY_DIFF :
        extra = has_diff_katcp(sn);
        break;
      case KATCP_STRATEGY_PERIOD : 
        extra = 1;
        break;
      default :
        extra = 0;
        break;
    }
    strategy = ns->n_manual ? strategy_name_sensor_katcp(ns) : "auto";
  } else {
    strategy = sensor_strategy_table[KATCP_STRATEGY_OFF];
    extra = 0;
  }

  result = append_string_katcp(d, KATCP_FLAG_STRING | (extra ? 0 : KATCP_FLAG_LAST), strategy ? strategy : "unknown");
  if(result < 0){
    return -1;
  }

  if(extra){
#ifdef DEBUG
    if(ns == NULL){
      fprintf(stderr, "sampling: logic problem: extra set but no current client active\n");
      abort();
    }
#endif
    switch(ns->n_strategy){
      case KATCP_STRATEGY_PERIOD :
        period = (ns->n_period.tv_sec * 1000) + (ns->n_period.tv_usec / 1000);
        result = append_unsigned_long_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_ULONG, period);
        break;
      case KATCP_STRATEGY_DIFF :
        result = append_sensor_diff_katcp(d, KATCP_FLAG_LAST, ns);
        break;
      default :
#ifdef DEBUG
        fprintf(stderr, "sampling: major logic problem: strategy %d should not need extra stuff", ns->n_strategy);
        abort();
#endif
        break;
    }
  }

  return result;
}

static int configure_sensor_katcp(struct katcp_dispatch *d, struct katcp_sensor *sn, int strategy, int manual, char *extra)
{
  struct katcp_nonsense *ns;
  struct katcp_acquire *a;
  unsigned long period;
  char *end;
  struct timeval fudge;

  if(manual == 0){ /* automatic mode an alias for EVENT */
    strategy = KATCP_STRATEGY_EVENT;
  }

  if((strategy < 0) || (strategy >= KATCP_STRATEGIES_COUNT)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "malformed strategy %d", strategy);
    return -1;
  }

  if(sn == NULL){
    return -1;
  }

  ns = find_nonsense_katcp(d, sn->s_name);
  if(ns){
#ifdef DEBUG
    if(strategy == KATCP_STRATEGY_FORCED){
      fprintf(stderr, "sensor: did not expect reconfiguration for forced acquire on existing sensor\n");
      abort();
    }
#endif
    if(ns->n_sensor != sn){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem sensor reference to client not bidirectional");
#ifdef DEBUG
      abort();
#endif
      return -1;
    }
  } 

  /* ns now either valid or NULL, sn guaranteed to be valid */

  a = sn->s_acquire;

  if(strategy == KATCP_STRATEGY_OFF){
    if(ns){
      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "turning off sensor client");
      destroy_nonsense_katcp(d, ns);
      ns = NULL;
    }
    /* WARNING: destroy_nonsense will call reload_ to stop scheduling itself */
  } else {
    if(ns == NULL){
      log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "creating sensor client");
      ns = create_nonsense_katcp(d, sn);
      if(ns == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate sensor shadow copy");
        return -1;
      }
    }
    switch(strategy){
      case KATCP_STRATEGY_EVENT : 
        ns->n_period.tv_sec = a->a_poll.tv_sec;
        ns->n_period.tv_usec = a->a_poll.tv_usec;
        break;
      case KATCP_STRATEGY_PERIOD : 
        if(extra == NULL){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "require a period as extra parameter");
          return -1;
        }
        period = strtoul(extra, &end, 10);
        log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "scan period is %lums", period);
        ns->n_period.tv_sec = period / 1000;
        ns->n_period.tv_usec = (period % 1000) * 1000;

        if(cmp_time_katcp(&(ns->n_period), &(a->a_limit)) < 0){
          fudge.tv_sec = 0;
          fudge.tv_usec = SENSOR_LIMIT_FUDGE;

          add_time_katcp(&(ns->n_period), &(a->a_limit), &fudge);

          log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "clamping poll rate for %s to %lu.%06lus", sn->s_name, ns->n_period.tv_sec, ns->n_period.tv_usec);
        }

        gettimeofday(&(ns->n_next), NULL);

        /* instead of calling just gettimeoday we could examine other nonsense entries and try to schedule ourselves for the same time - improves throughput, but may make things more jittery */

        break;
      case KATCP_STRATEGY_DIFF : 
        if(!has_diff_katcp(sn)){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "diff strategy not available for this type");
          return -1;
        }
        if(extra == NULL){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "require a delta as extra parameter");
          return -1;
        }
        if(scan_sensor_diff_katcp(ns, extra) < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to scan delta value %s for sensor %s", extra, sn->s_name);
          return -1;
        }
        ns->n_period.tv_sec = a->a_poll.tv_sec;
        ns->n_period.tv_usec = a->a_poll.tv_usec;
        break;
      case KATCP_STRATEGY_FORCED :
        break;
#ifdef DEBUG
      default :
        fprintf(stderr, "major logic problem: bad strategy %d\n", strategy);
        abort();
#endif
    }

    ns->n_strategy = strategy;
    ns->n_manual = manual;

    /* WARNING: reload sensor handles the scheduling, require that this is run after any create/time change */
    if(reload_sensor_katcp(d, sn) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to reschedule sensor %s", sn->s_name);
      return -1;
    }

  }

  log_message_katcp(d, sn->s_refs ? KATCP_LEVEL_INFO : KATCP_LEVEL_DEBUG, NULL, "%d clients subscribed to sensor %s", sn->s_refs, sn->s_name);
  if(a->a_periodics > 0){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "sensor %s polled once every %lu.%06lus", sn->s_name, a->a_current.tv_sec, a->a_current.tv_usec);
  }

  return 0;
}

int sensor_sampling_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_sensor *sn;
  char *name, *strategy, *extra;
  int value, manual;

  if(argc <= 1){
    extra_response_katcp(d, KATCP_RESULT_INVALID, "parameters");
    return KATCP_RESULT_OWN;
  } 

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return KATCP_RESULT_FAIL;
  }

  strategy = NULL;
  extra = NULL;
  manual = 1;

  if(argc > 2){
    strategy = arg_string_katcp(d, 2);
    if(argc > 3){
      extra = arg_string_katcp(d, 3);
    }
  }

  sn = find_sensor_katcp(d, name);
  if(sn == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown sensor %s", name);
    return KATCP_RESULT_FAIL;
  }

  if(strategy){
    if(!strcmp(strategy, "auto")){
      manual = 0;
      value = (-1);
    } else {
      manual = 1;
      value = strategy_code_sensor_katcp(strategy);
      if(value < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown strategy %s", strategy);
        return KATCP_RESULT_FAIL;
      }
    }

    if(configure_sensor_katcp(d, sn, value, manual, extra) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to configure sensor %s for strategy %s", name, strategy);
      return KATCP_RESULT_FAIL;
    }
  }

  if(reply_sensor_sampling_katcp(d, sn) < 0){
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OWN;
}

/************************************************************************************/

int sensor_dump_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_sensor *sn;
  struct katcp_nonsense *ns;
  struct katcp_acquire *a;
  struct katcp_integer_acquire *ia;
  struct katcp_double_acquire *da;
  int i, j, got;

  s = d->d_shared;

  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  for(i = 0; i < s->s_tally; i++){
    sn = s->s_sensors[i];
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "sensor %s at %p has type %d (%s) with %d clients", sn->s_name, sn, sn->s_type, type_name_sensor_katcp(sn), sn->s_refs);
    for(j = 0; j < sn->s_refs; j++){
      ns = sn->s_nonsense[j];
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "client %d in strategy %d (%s) with dispatch %p (%s)", j, ns->n_strategy, strategy_name_sensor_katcp(ns), ns->n_client, (ns->n_client == d) ? "this connection" : "another connection");
      if(ns->n_sensor != sn){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem - client does not point at sensor");
      }
    }
    a = sn->s_acquire;
    got = 0;
    for(j = 0; j < a->a_count; j++){
      if(a->a_sensors[j] == sn){
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "sensor at position %d of %d of acquire %p of type %d", j, a->a_count, a, a->a_type);
        got++;
      }
    }
    switch(a->a_type){
      case KATCP_SENSOR_INTEGER :
      case KATCP_SENSOR_BOOLEAN :
        ia = a->a_more;
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "acquire current value %d, get %p and local state %p", ia->ia_current, ia->ia_get, a->a_local);
        break;
      case KATCP_SENSOR_FLOAT  :
        da = a->a_more;
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "acquire current value %e, get %p and local state %p", da->da_current, da->da_get, a->a_local);
        break;

    }
    if(got == 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem - acquire does not know about this sensor");
    }
  } 

  return KATCP_RESULT_OK;
}

/***************************************************************************/

#if 0
char *assemble_sensor_name_katcp(struct katcp_notice *n, char *suffix)
{
  char *copy;
  int last, first, total;
  struct katcp_url *ku;

  if(n->n_name == NULL){
    return NULL;
  }

  ku = create_kurl_from_string_katcp(n->n_name);
  if(ku == NULL){
    return NULL;
  }

  last = strlen(suffix);

  if(ku->u_cmd){
    first = strlen(ku->u_cmd);
  } else {
    first = strlen(ku->u_host);
  }

  total = first + 7 + last + 1;

  copy = malloc(total);
  if(copy == NULL){
    destroy_kurl_katcp(ku);
    return NULL;
  }

  if(ku->u_cmd){
    snprintf(copy, total, "%s.%s", ku->u_cmd, suffix);
  } else {
    if(ku->u_port == NETC_DEFAULT_PORT){
      snprintf(copy, total, "%s.%s", ku->u_host, suffix);
    } else {
      snprintf(copy, total, "%s.%d.%s", ku->u_host, ku->u_port, suffix);
    }
  }

  copy[total - 1] = '\0';

#if 0
  ptr = strchr(copy, '#');
  if(ptr){
    ptr[0] = '\0';
  }
#endif

  destroy_kurl_katcp(ku);

  return copy;
}
#endif

int match_sensor_list_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_sensor *sn;
  struct katcl_parse *p, *px;
  struct katcp_acquire *a;
  struct katcp_job *j;
  char *inform, *name, *description, *type, *units, *combine;
  int code, min, max;
  unsigned int count;
#ifdef KATCP_USE_FLOATS
  double maxf, minf;
#endif

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "releasing sensor list match");
    return 0;
  }

  inform = get_string_parse_katcl(p, 0);
  if(inform == NULL){
    return 0;
  }
  
  if(!strcmp(inform, KATCP_RETURN_JOB)){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "detaching in response to %s", inform);
    return 0;
  }

  name = get_string_parse_katcl(p, 1);
  description = get_string_parse_katcl(p, 2);
  units = get_string_parse_katcl(p, 3);
  type = get_string_parse_katcl(p, 4);

#ifdef DEBUG
  fprintf(stderr, "sensor: saw sensor %s, type %s\n", name, type);
#endif

  if((name == NULL) || 
     (description == NULL) || 
     (type == NULL)){
    return -1;
  }
  
  combine = path_from_notice_katcp(n, name, 0);
  if(combine == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate combined name for upstream sensor %s", name);
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "saw subordinate sensor definition for %s, taking it as %s", name, combine);

  sn = find_sensor_katcp(d, combine);
  if(sn){
    free(combine);
    return 1;
  }

  code = type_code_sensor_katcp(type);
  if(code < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "saw sensor declaration for %s of unknown type %s", name, type);
    free(combine);
    return 1;
  }

#if 0
  j = find_job_katcp(d, n->n_name);
  if(j == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "could not locate job for notice %s", n->n_name);
  }
#endif

  /* WARNING: force mode to be 0, but what about sensor availability in various modes ? */
  sn = create_sensor_katcp(d, combine, description, units, KATCP_STRATEGY_EVENT, code, 0);
  free(combine);

  if(sn == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "relayed sensor declaration of %s failed", name);
    return -1;
  }

  /* TODO: adhocery ahead: There aught to be a decent way of creating type specific sensor data given a parse structure - using the the type lookup structure */

  /* assume a to have failed, if failed, destroy sensor */
  a = NULL;
  switch(code){
    case KATCP_SENSOR_INTEGER :
      count = get_count_parse_katcl(p);
      if(count >= 7){
        min = get_signed_long_parse_katcl(p, 5);
        max = get_signed_long_parse_katcl(p, 6);
        if(create_sensor_integer_katcp(d, sn, min, max) >= 0){
          a = setup_integer_acquire_katcp(d, NULL, NULL, NULL);
        }
      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%u parameters not sufficient to copy integer sensor %s", count, name);
      }
      break;
    case KATCP_SENSOR_BOOLEAN :
      if(create_sensor_boolean_katcp(d, sn) >= 0){
        a = setup_boolean_acquire_katcp(d, NULL, NULL, NULL);
      }
      break;
#ifdef KATCP_USE_FLOATS
    case KATCP_SENSOR_FLOAT :
      count = get_count_parse_katcl(p);
      if(count >= 7){
        minf = get_double_parse_katcl(p, 5);
        maxf = get_double_parse_katcl(p, 6);
#ifdef DEBUG
        fprintf(stderr, "match sensor float: min: <%f>, max: <%f>\n", minf, maxf);
#endif
        if(create_sensor_double_katcp(d, sn, minf, maxf) >= 0){
          a = setup_double_acquire_katcp(d, NULL, NULL, NULL);
        }
      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%u parameters not sufficient to copy floating point sensor %s", count, name);
      }
      break;
#endif
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to deal with sensor type %s yet while cloning %s", type, name);
      break;
  }

  /* if aquire has been created, things are ok, otherwise undo */
  if(a == NULL){
    destroy_sensor_katcp(d, sn);
    return 1;
  }

  if(link_acquire_katcp(d, a, sn, NULL)){
    destroy_sensor_katcp(d, sn);
    destroy_acquire_katcp(d, a);
    return -1;
  }

  j = find_containing_job_katcp(d, n->n_name);

  if(j){
    px = create_parse_katcl();
    if(px){

      add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?sensor-sampling");
      add_string_parse_katcl(px,                    KATCP_FLAG_STRING, name);
      add_string_parse_katcl(px, KATCP_FLAG_LAST  | KATCP_FLAG_STRING, "event");

      if(submit_to_job_katcp(d, j, px, NULL, NULL, NULL) == 0){
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "submitted request to sample %s to job %s", name, j->j_url->u_str);
      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to submit sampling request to job %s", j->j_url->u_str);
        destroy_parse_katcl(p);
      }
    } else {
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to allocate parse structure while samping sensor %s", name);
    }
  } else {
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to sample sensor %s as job for %s not available", name, n->n_name);
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "created local sensor from subordinate %s", name);

  return 1;
}

int match_sensor_status_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_sensor *sn;
  struct katcp_acquire *a;
  struct katcl_parse *p;
  char *inform, *name, *status, *value, *combine;
  int code;

  p = get_parse_notice_katcp(d, n);
  if(p == NULL){
    log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "releasing sensor status match");
    return 0;
  }

  inform = get_string_parse_katcl(p, 0);
  if(inform == NULL){
    return 0;
  }
  
  if(!strcmp(inform, KATCP_RETURN_JOB)){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "detaching in response to %s", inform);
    return 0;
  }


  /* WARNING: will only extract the first sensor out of a composite list */
  name = get_string_parse_katcl(p, 3);
  status = get_string_parse_katcl(p, 4);
  value = get_string_parse_katcl(p, 5);

  if((name == NULL) || 
     (status == NULL) || 
     (value == NULL)){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "insufficient parameters reported by sensor status");
    return -1;
  }

  combine = path_from_notice_katcp(n, name, 0);
  if(combine == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate combined name for upstream sensor %s", name);
    return -1;
  }

  sn = find_sensor_katcp(d, combine);
  free(combine);

  if(sn == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "saw sensor %s not yet declared", name);
    return 1;
  }

  /* in a real OO program, this would be an accessor function */
  a = sn->s_acquire;
  if(a == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "sensor %s has no acquire", name);
    return -1;
  }

  if(scan_value_sensor_katcp(a, value) < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to scan value %s for sensor %s", value, name);
    return -1;
  }

  code = status_code_sensor_katcl(status);
  if(code < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "saw bad status %s for sensor %s", status, name);
    return 1;
  }

  set_status_sensor_katcp(sn, code);

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "updated sensor %s to value %s with status %s", name, status, value);

  propagate_acquire_katcp(d, a);

  return 1;
}

int job_match_sensor_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  struct katcp_dispatch *dl;
  int result;

  result = 0;
  dl = template_shared_katcp(d);

  if(dl == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to acquire template");
    return -1;
  }

  if(match_inform_job_katcp(dl, j, "#sensor-list", &match_sensor_list_katcp, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to match sensor-list on job %s", j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    result = (-1);
  }

  if(match_inform_job_katcp(dl, j, "#sensor-status", &match_sensor_status_katcp, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to match sensor-status on job %s", j->j_url->u_str ? j->j_url->u_str : "<anonymous>");
    result = (-1);
  }

  return result;
}

int job_enable_sensor_katcp(struct katcp_dispatch *d, struct katcp_job *j)
{
  struct katcp_dispatch *dl;
  struct katcl_parse *p;

  dl = template_shared_katcp(d);
  if(dl == NULL){
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to acquire template");
    return -1;
  }

  p = create_parse_katcl();
  if(p == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create message");
    return -1;
  }

  if(add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, "?sensor-list") < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to assemble message");
    destroy_parse_katcl(p);
    return -1;
  }

  if(submit_to_job_katcp(dl, j, p, NULL, NULL, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to submit message to job");
    destroy_parse_katcl(p);
    return -1;
  }

  return 0;
}

int set_status_group_sensor_katcp(struct katcp_dispatch *d, char *prefix, int status)
{
  struct katcp_shared *s;
  struct katcp_sensor *sn;
  unsigned int i, len;
  int count;

  if((status < 0) || (status >= KATCP_STATA_COUNT)){
    return -1;
  }

  s = d->d_shared;
  if(s == NULL){
    return -1;
  }

  len = strlen(prefix);

  count = 0;
  for(i = 0; i < s->s_tally; i++){
    sn = s->s_sensors[i];

    sane_sensor(sn);

    if(!strncmp(prefix, sn->s_name, len)){
      sn->s_status = status;
      count++;
    }
  }

  return count;
}

int job_suspend_sensor_katcp(struct katcp_dispatch *d, struct katcp_notice *n, void *data)
{
  struct katcp_url *ku;
  char *prefix;
  int result;

  ku = create_kurl_from_string_katcp(n->n_name);
  if(ku == NULL){
    return -1;
  }

  if(ku->u_cmd){
    prefix = ku->u_cmd;
  } else {
    prefix = ku->u_host;
  }

  if(prefix == NULL){
    return -1;
  }

  result =  set_status_group_sensor_katcp(d, prefix, KATCP_STATUS_UNKNOWN);

  destroy_kurl_katcp(ku);

  return result;
}


/***************************************************************************/

int sensor_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_sensor *sn;
  struct katcp_dispatch *dl;
  struct katcp_nonsense *ns;
  struct katcp_job *jb;
  struct katcp_acquire *a;
  struct katcp_integer_acquire *ia;
  struct katcl_parse *p;
  int i, j, got;
  char *name, *type, *label, *value, *description, *units;

  s = d->d_shared;

  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }
  
  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return KATCP_RESULT_FAIL;
  }

  if(!strcmp(name, "list")){
    for(i = 0; i < s->s_tally; i++){
      sn = s->s_sensors[i];
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "sensor %s at %p has type %d (%s) with %d clients", sn->s_name, sn, sn->s_type, type_name_sensor_katcp(sn), sn->s_refs);
      for(j = 0; j < sn->s_refs; j++){
        ns = sn->s_nonsense[j];
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "client %d in strategy %d (%s) with dispatch %p (%s)", j, ns->n_strategy, strategy_name_sensor_katcp(ns), ns->n_client, (ns->n_client == d) ? "this connection" : "another connection");
        if(ns->n_sensor != sn){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem - client does not point at sensor");
        }
      }
      a = sn->s_acquire;
      got = 0;
      for(j = 0; j < a->a_count; j++){
        if(a->a_sensors[j] == sn){
          log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "sensor at position %d of %d of acquire %p of type %d", j, a->a_count, a, a->a_type);
          got++;
        }
      }
      switch(a->a_type){
        case KATCP_SENSOR_INTEGER :
        case KATCP_SENSOR_BOOLEAN :
          ia = a->a_more;
          log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "acquire current value %d, get %p and local state %p", ia->ia_current, ia->ia_get, a->a_local);
          break;
      }
      if(got == 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem - acquire does not know about this sensor");
      }
    } 
    return KATCP_RESULT_OK;

  } else if(!strcmp(name, "relay")){

    name = arg_string_katcp(d, 2);
    if(name == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "require a subordinate connection to relay");
      return KATCP_RESULT_FAIL;
    }

    jb = find_job_katcp(d, name);
    if(jb == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to find job labelled %s", name);
      return KATCP_RESULT_FAIL;
    }

    if(job_match_sensor_katcp(d, jb) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to match sensor inform messages for job %s", name);
    }

    dl = template_shared_katcp(d);
    if(dl == NULL){
      log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "unable to acquire template");
      return KATCP_RESULT_FAIL;
    }

    p = create_parse_katcl();
    if(p == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to create message");
      return KATCP_RESULT_FAIL;
    }

    if(add_string_parse_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, "?sensor-list") < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to assemble message");
      destroy_parse_katcl(p);
      return KATCP_RESULT_FAIL;
    }

    if(submit_to_job_katcp(dl, jb, p, NULL, NULL, NULL) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to submit message to job");
      destroy_parse_katcl(p);
      return KATCP_RESULT_FAIL;
    }

    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "initialised sensor tracking for subordinate %s", jb->j_url->u_str);
    return KATCP_RESULT_OK;

  } else if(!strcmp(name, "forget")){
    label = arg_string_katcp(d, 2);

    if(label == NULL){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "need a sensor name to forget");
      return KATCP_RESULT_FAIL;
    }

    sn = find_sensor_katcp(d, label);
    if(sn == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no sensor of name %s", label);
      return KATCP_RESULT_FAIL;
    }

    if(sn->s_acquire){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "refusing to terminate sensor which has acquire at %p", sn->s_acquire);
      return KATCP_RESULT_FAIL;
    }


    return KATCP_RESULT_FAIL;
  } else if(!strcmp(name, "create")){
    label = arg_string_katcp(d, 2);
    type = arg_string_katcp(d, 3);

    description = arg_string_katcp(d, 4);
    units = arg_string_katcp(d, 5);

    if((type == NULL) || (label == NULL)){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "create needs a name and type");
      return KATCP_RESULT_FAIL;
    }

    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "about to create sensor %s of type %s", label, type);

    if(!strcmp(type, "integer")){
      if(register_integer_sensor_katcp(d, 0, label, description, units, NULL, NULL, NULL, 0, INT_MAX) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register integer sensor %s", label);
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_OK;
    } else if(!strcmp(type, "boolean")){
      if(register_boolean_sensor_katcp(d, 0, label, description, units, NULL, NULL, NULL) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register boolean sensor %s", label);
        return KATCP_RESULT_FAIL;
      }

      return KATCP_RESULT_OK;
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no logic to handle type %s yet", type);
      return KATCP_RESULT_FAIL;
    }

  } else if(!strcmp(name, "update")){

    label = arg_string_katcp(d, 2);
    value = arg_string_katcp(d, 3);

    if((label == NULL) || (value == NULL)){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "create needs a name and value");
      return KATCP_RESULT_FAIL;
    }

    a = find_acquire_katcp(d, label);
    if(a == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate acquire for sensor %s", label);
      return KATCP_RESULT_FAIL;
    }

    switch(a->a_type){
      case KATCP_SENSOR_BOOLEAN :
      case KATCP_SENSOR_INTEGER :
        if(a->a_more == NULL){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no extra integer field for sensor %s", label);
          return KATCP_RESULT_FAIL;
        }
        
        ia = a->a_more;
        ia->ia_current = atoi(value);

        if(a->a_type == KATCP_SENSOR_BOOLEAN){
          if(ia->ia_current){
            ia->ia_current = 1;
          }
        }

        propagate_acquire_katcp(d, a);

        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "updated integer sensor %s to new value %d", label, ia->ia_current);
        return KATCP_RESULT_OK;

      default :
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to update sensor %s of unsupported type %d\n", label, a->a_type);
        return KATCP_RESULT_FAIL;
    }

  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown sensor operation %s", name);
    return KATCP_RESULT_FAIL;
  }

  /* should not be reached */
}
