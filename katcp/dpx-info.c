#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#include <unistd.h>
#include <errno.h>
#include <sysexits.h>

#include <sys/stat.h>

#include <netc.h>
#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

static int each_log_parse_katcp(struct katcl_line *l, int count, unsigned int limit, unsigned int level, struct katcl_parse *px)
{
  int result;

#if DEBUG > 1
  fprintf(stderr, "log: considering logging %p to %p (if %u >= %u)\n", px, l, level, limit);
#endif

  if(level < limit){
    return count;
  }

  result = append_parse_katcl(l, px);
  if(count < 0){
    return -1;
  }
  if(result < 0){
    return -1;
  }

  return count + 1;
}

int log_parse_katcp(struct katcp_dispatch *d, int level, int local, struct katcl_parse *px)
{

  /* WARNING: assumption if level < 0, then a relayed log message ... this probably should be a flag in its own right */

  int limit, count, cmp;
  unsigned int mask;
  int layer;
  int scope;
  int i, j;
  char *ptr;
  struct katcp_flat *fx, *ft;
  struct katcp_group *gx, *gt;
  struct katcp_shared  *s;

  if(px == NULL){
    return -1;
  }

  s = d->d_shared;
  if(s == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "log: no shared state available\n");
    abort();
#endif
    return -1;
  }

  ft = this_flat_katcp(d);
  gt = this_group_katcp(d);

#ifdef KATCP_CONSISTENCY_CHECKS
  ptr = get_string_parse_katcl(px, 0);
  if(ptr == NULL){
    fprintf(stderr, "log: empty message type\n");
    return -1;
  }
  if(strcmp(ptr, KATCP_LOG_INFORM)){
    fprintf(stderr, "log: expected message %s, not %s\n", KATCP_LOG_INFORM, ptr);
    return -1;
  }
  if(level < 0){
    fprintf(stderr, "log: no longer accept messages with negative priority\n");
    return -1;
  }
#endif

  layer = ft ? ft->f_layer : 0;
  limit = KATCP_MASK_LEVELS & ((unsigned int)level);
  mask = (~KATCP_MASK_LEVELS) & ((unsigned int)level);
  scope = level_to_scope_katcp(mask);

#if DEBUG > 1
  fprintf(stderr, "log: message from layer=%d, limit=%d, mask=%x, scope=%d\n", layer, limit, mask, scope);
#endif

  count = 0;

  /* WARNING: ft and gt may be NULL if outside flat context */
  /* WARNING: a message that isn't local won't be echoed back to its sender */

  if(local && ft){
    /* handle ourselves first, avoiding the local check elsewhere */
    fx = ft;
    count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
  }

  if(s->s_groups == NULL){
    return count;
  }

  if(scope <=  KATCP_SCOPE_LOCAL){
    scope = (-1);
  }

  for(j = 0; j < s->s_members; j++){
    gx = s->s_groups[j];

    cmp = scope;
    if((scope == KATCP_SCOPE_GROUP) && (gt != gx)){
      cmp = (-1); /* if not same group, then can't match on group basis */
    }

    for(i = 0; i < gx->g_count; i++){
      fx = gx->g_flats[i];
      if(fx && (fx != ft)){
#if DEBUG > 1
  fprintf(stderr, "log: testing %s (layer %d > %d) || (cmp %d [%d] == %d)\n", fx->f_name ? fx->f_name : "anon", fx->f_layer, layer, cmp, scope, fx->f_scope);
#endif

        if(
          /* if we are deeper layer than message always see it */
          (fx->f_layer > layer) ||
          /* else use sending scope */
          (cmp >= fx->f_scope)){
            count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
        }
      }
    }
  }

#if 0
  if(mask & KATCP_LEVEL_LOCAL){ /* message never visible outside current connection */
    fx = ft;
    if(fx && (level >= 0)){
      count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
    }
  } else if(mask & KATCP_LEVEL_GROUP){ /* message stays within the same group, at most */
    gx = gt;
    if(gx){
      for(i = 0; i < gx->g_count; i++){
        fx = gx->g_flats[i];
        if(fx){
          if(fx == ft){
            if(level >= 0){
              count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
            }
          } else {
            if(fx->f_scope != KATCP_SCOPE_SINGLE){
              count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
            }
          }
        }
      }
    }
  } else { /* message goes everywhere, subject to scope filter */
    if(s->s_groups){
      for(j = 0; j < s->s_members; j++){
        gx = s->s_groups[j];
        for(i = 0; i < gx->g_count; i++){
          fx = gx->g_flats[i];
          if(fx && ((fx != ft) || (level >= 0))){
            switch(fx->f_scope){
              case KATCP_SCOPE_SINGLE :
                if(fx == ft){
                  count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
                }
                break;
              case KATCP_SCOPE_GROUP  :
                if(gx == gt){
                  count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
                }
                break;
              case KATCP_SCOPE_GLOBAL :
                count = each_log_parse_katcp(fx->f_line, count, fx->f_log_level, limit, px);
                break;
#ifdef KATCP_CONSISTENCY_CHECKS
              default :
                fprintf(stderr, "log: invalid scope %d for %p\n", fx->f_scope, fx);
                abort();
                break;
#endif
            }
          }
        }
      }
    }
  }
#endif

#ifdef DEBUG
  fprintf(stderr, "log: message %p reported %d times\n", px, count);
#endif

  return count;
}

int log_group_info_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;
  int level;
  char *ptr;

#ifdef DEBUG
  fprintf(stderr, "log: encountered a log message\n");
#endif

  fx = this_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

#if 0
  /* WARNING: probably the best way to inhibit this is to provide a means to deregister this handler */
  if(fx->f_flags & KATCP_FLAT_TOCLIENT){
#ifdef DEBUG
    fprintf(stderr, "log: ingnoring log message from client\n");
#endif
    return 0;
  }
#endif

  px = arg_parse_katcp(d);
  if(px == NULL){
    return -1;
  }

  ptr = get_string_parse_katcl(px, 1);
  if(ptr == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "log: null priority value for log message\n");
#endif
    return -1;
  }

  level = log_to_code_katcl(ptr);
  if(level < 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "log: invalid log priority value %s for log message\n", ptr);
#endif
    return -1;
  }

  level |= fx->f_log_reach;

  if(log_parse_katcp(d, level, 0, px) < 0){
    return -1;
  }

  return 0;
}

#define FIXUP_TABLE 256 /* has to be size of char ... */

static unsigned char fixup_remap_table[FIXUP_TABLE];
static unsigned int fixup_remap_ready = 0;

static void fixup_remap_init()
{
  unsigned int i;

  for(i = 0; i < FIXUP_TABLE; i++){
    fixup_remap_table[i] = i;
    if(isupper(fixup_remap_table[i])){
      fixup_remap_table[i] = tolower(fixup_remap_table[i]);
    }
    if(!isprint(fixup_remap_table[i])){
      fixup_remap_table[i] = KATCP_VRBL_DELIM_SPACER;
    }
  }

  /* strip out things that trigger variable substitution */
  fixup_remap_table[KATCP_VRBL_DELIM_GROUP] = KATCP_VRBL_DELIM_LOGIC;
  fixup_remap_table[KATCP_VRBL_DELIM_TREE ] = KATCP_VRBL_DELIM_LOGIC;
  fixup_remap_table[KATCP_VRBL_DELIM_ARRAY] = KATCP_VRBL_DELIM_LOGIC;

  /* and clean up things */
  fixup_remap_table[KATCP_VRBL_DELIM_FORBID] = KATCP_VRBL_DELIM_SPACER;
}
#undef FIXUP_TABLE

static char *make_child_field_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, char *name, char *prepend, int locator)
{
  /* WARNING: in a way this is a bit of a mess, some of this should be subsumed into the variable API, however, the client prefix doesn't belong there either ... it is complicated */
  char *copy, *strip;
  int suffix, prefix, size, i, j;

  if(fixup_remap_ready == 0){
    fixup_remap_init();
    fixup_remap_ready = 1;
  }

  if(strchr(name, KATCP_VRBL_DELIM_GROUP)){
    /* WARNING: special case - if variable name includes * we assume the child knows about our internals */
    return strdup(name);
  }

  size = 1;

  if(locator){
    size += 2;
  }

  if((prepend != NULL) && (name[0] != KATCP_VRBL_DELIM_LOGIC)){
    prefix = strlen(prepend);
  } else {
    prefix = 0;
  }
  size += (prefix + 1);

  if(name[0] == KATCP_VRBL_DELIM_LOGIC){
    strip = name + 1;
  } else {
    strip = name;
  }
  suffix = strlen(strip);
  if(suffix <= 0){
    return NULL;
  }
  size += suffix;

  copy = malloc(size);
  if(copy == NULL){
    return NULL;
  }

  i = 0;
  if(locator){
    copy[i++] = KATCP_VRBL_DELIM_GROUP;
  }
  if(prefix){
    for(j = 0; prepend[j] != '\0'; j++){
      copy[i++] = fixup_remap_table[((unsigned char *)prepend)[j]];
    }
    copy[i++] = KATCP_VRBL_DELIM_LOGIC;
  }

  for(j = 0; j < suffix; j++){
    copy[i++] = fixup_remap_table[((unsigned char *)strip)[j]];
  }

  if(locator){
    copy[i++] = KATCP_VRBL_DELIM_GROUP;
  }

  copy[i++] = '\0';

  return copy;
}

int version_group_info_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_vrbl *vx;
  struct katcp_endpoint *self, *remote, *origin;
  char *name, *version, *build, *ptr;
  unsigned int count;

#ifdef DEBUG
  fprintf(stderr, "version: encountered peer version message\n");
#endif

  fx = this_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = arg_parse_katcp(d);
  if(px == NULL){
    return -1;
  }

  origin = sender_to_flat_katcp(d, fx);
  remote = remote_of_flat_katcp(d, fx);
  self = handler_of_flat_katcp(d, fx);

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "%s saw a version message (origin=%p, remote=%p, self=%p)", fx->f_name, origin, remote, self);

  /* WARNING: unclear if this code path is ever needed, as we have the relay infrastructure to track pending requests. sensor-status is special, variable updates queue messages to it */
  if((fx->f_flags & KATCP_FLAT_TOSERVER) == 0){ /* not connected to a server */
    if(origin != remote){ /* and message not from remote (weak test, origin gets rewritten in case of relayed requests) */
      if(remote){ /* ... better relay it on, if somebody requested it */
        send_message_endpoint_katcp(d, self, remote, px, 0);
      } else {
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal problem, saw a version message but have nowhere to send it to");
      }

      return 0;
    }
  }

  count = get_count_parse_katcl(px);
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "saw a version field with %u args - attempting to process it", count);

  if(count < 2){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "insufficient parameters for usable version report - only got %u", count);
    return 0;
  }

  name = get_string_parse_katcl(px, 1);
  version = get_string_parse_katcl(px, 2);

  if(count > 2){
    build = get_string_parse_katcl(px, 3);
  } else {
    build = NULL;
  }

  ptr = make_child_field_katcp(d, fx, name, (fx->f_flags & KATCP_FLAT_PREPEND) ? fx->f_name : NULL, 1);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable fixup version field %s", name);
    return KATCP_RESULT_FAIL;
  }

  vx = find_vrbl_katcp(d, ptr);
  if(vx != NULL){
    if(is_ver_sensor_katcp(d, vx)){
      /* WARNING: unclear - maybe clobber the sensor entirely ... */
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "leaving old version definition unchanged");
    } else {
      /* unreasonable condition, up the severity */
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unwilling to transform existing variable %s into a version description", ptr);
    }
    free(ptr);
    return KATCP_RESULT_OWN; /* WARNING: is this a reasonable exit code ? */
  }

  /* here we can assume vx is a new variable */
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "defining new version string %s as %s", name, ptr);

  vx = scan_vrbl_katcp(d, NULL, version, KATCP_VRC_VERSION_VERSION, 1, KATCP_VRT_STRING);
  if(vx == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable to declare version variable %s for %s", ptr, fx->f_name);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  /* TODO: notice errors */
  if(build){
    scan_vrbl_katcp(d, vx, build, KATCP_VRC_VERSION_BUILD, 1, KATCP_VRT_STRING);
  }

  if(configure_vrbl_katcp(d, vx, KATCP_VRF_VER, NULL, NULL, NULL, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to configure new variable %s as version field", ptr);
    destroy_vrbl_katcp(d, ptr, vx);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  if(update_vrbl_katcp(d, fx, ptr, vx, 0) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to store new version variable %s for client %s", ptr, fx->f_name);
    destroy_vrbl_katcp(d, name, vx);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "declared version variable %s as %s", name, ptr);

  free(ptr);

  return KATCP_RESULT_OK;
}

int sensor_list_group_info_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_vrbl *vx;
  struct katcp_endpoint *self, *remote, *origin;
  char *name, *description, *units, *type, *ptr, *field;
  unsigned typecode, i;

#ifdef DEBUG
  fprintf(stderr, "log: encountered a sensor-list message\n");
#endif

  fx = this_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = arg_parse_katcp(d);
  if(px == NULL){
    return -1;
  }

  origin = sender_to_flat_katcp(d, fx);
  remote = remote_of_flat_katcp(d, fx);
  self = handler_of_flat_katcp(d, fx);

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "saw a sensor list message (origin=%p, remote=%p, self=%p)", origin, remote, self);

  if((fx->f_flags & KATCP_FLAT_TOSERVER) == 0){ /* we aren't connected to a server */
    if(origin != remote){ /* and it didn't come from the remote party (weak test, this gets rewritten in case of a relay */
      if(remote){ /* ... better relay it on, if somebody requested it */
        send_message_endpoint_katcp(d, self, remote, px, 0);
      } else {
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal problem, saw a sensor list but have nowhere to send it to");
      }

      return 0;
    }
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "saw a sensor list - attempting to load it");

  name = get_string_parse_katcl(px, 1);
  description = get_string_parse_katcl(px, 2);
  units = get_string_parse_katcl(px, 3);
  type = get_string_parse_katcl(px, 4);

  if((name == NULL) || (description == NULL) || (type == NULL)){
    if(name){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "declaration of sensor %s lacks a description or type", name);
    } else {
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "deficient sensor declaration does not have a name");
    }
    return KATCP_RESULT_FAIL;
  }

  typecode = type_from_string_sensor_katcp(d, type);
  if(typecode < 0){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unsupported type %s of sensor %s", type, name);
    return KATCP_RESULT_FAIL;
  }


  ptr = make_child_field_katcp(d, fx, name, (fx->f_flags & KATCP_FLAT_PREFIXED) ? fx->f_name : NULL, 1);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable fixup sensor name %s", name);
    return KATCP_RESULT_FAIL;
  }

  vx = find_vrbl_katcp(d, ptr);
  if(vx != NULL){
    if(is_vrbl_sensor_katcp(d, vx)){
#if 0
    if(vx->v_flags & KATCP_VRF_SEN){ /* might be better choice: the extra checks in is_vrbl might be too fussy during set up ? */
#endif
      /* WARNING: unclear - maybe clobber the sensor entirely ... */
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "leaving old sensor definition unchanged");
    } else {
      /* unreasonable condition, up the severity */
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unwilling to transform existing variable %s into a sensor", ptr);
    }
    free(ptr);
    return KATCP_RESULT_OWN; /* WARNING: is this a reasonable exit code ? */
  }

  /* here we can assume vx is a new variable */
  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "defining new sensor %s as %s", name, ptr);

  vx = scan_vrbl_katcp(d, NULL, NULL, KATCP_VRC_SENSOR_VALUE, 1, KATCP_VRT_STRING);
  if(vx == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable to declare sensor variable %s for %s", ptr, fx->f_name);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  /* TODO: notice errors */
  scan_vrbl_katcp(d, vx, description, KATCP_VRC_SENSOR_HELP, 1, KATCP_VRT_STRING);
  scan_vrbl_katcp(d, vx, type, KATCP_VRC_SENSOR_TYPE, 1, KATCP_VRT_STRING);

  if(units){
    scan_vrbl_katcp(d, vx, units, KATCP_VRC_SENSOR_UNITS, 1, KATCP_VRT_STRING);
  } else {
    if((fx->f_flags & KATCP_FLAT_PERMITNUL) == 0){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "sensor %s declared with no units", name);
    } else {
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "accepted sensor %s which has no units", name);
    }
  }

  if(typecode == KATCP_SENSOR_DISCRETE){
    for(i = 5; i < argc; i++){
      field = get_string_parse_katcl(px, i);
      if(field){
        scan_vrbl_katcp(d, vx, field, KATCP_VRC_SENSOR_RANGE "#-", 1, KATCP_VRT_STRING);
      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to acquire field %u of discrete sensor %s declared by %s", i - 5, ptr, fx->f_name);
      }
    }
    if(argc <= 5){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "discrete sensor %s declared by %s has no possible values", ptr, fx->f_name);
    }
  }

  if(configure_vrbl_katcp(d, vx, KATCP_VRF_SEN, NULL, NULL, NULL, NULL) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to configure new variable %s as sensor", ptr);
    destroy_vrbl_katcp(d, ptr, vx);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  if(update_vrbl_katcp(d, fx, ptr, vx, 0) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to store new sensor variable %s for client %s", ptr, fx->f_name);
    destroy_vrbl_katcp(d, name, vx);
    free(ptr);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "declared variable %s as %s", name, ptr);

  schedule_sensor_update_katcp(d, ptr);

  free(ptr);

  return KATCP_RESULT_OK;
}

int sensor_status_group_info_katcp(struct katcp_dispatch *d, int argc)
{
#define TIMESTAMP_BUFFER 20
  struct katcp_flat *fx;
  struct katcl_parse *px;
  struct katcp_endpoint *self, *remote, *origin;
  struct katcp_vrbl *vx;
  char *name, *stamp, *value, *status, *ptr, *tmp;
  char buffer[TIMESTAMP_BUFFER];
  int unhide, count;

#ifdef DEBUG
  fprintf(stderr, "log: encountered a sensor-status message\n");
#endif

  fx = this_flat_katcp(d);
  if(fx == NULL){
    return -1;
  }

  px = arg_parse_katcp(d);
  if(px == NULL){
    return -1;
  }

  count = get_count_parse_katcl(px);

  origin = sender_to_flat_katcp(d, fx);
  remote = remote_of_flat_katcp(d, fx);
  self = handler_of_flat_katcp(d, fx);

  log_message_katcp(d, KATCP_LEVEL_TRACE, NULL, "saw a sensor status message (origin=%p, remote=%p, self=%p)", origin, remote, self);

  if(origin == remote){
    /* comes in from fd, process it */

    /* status: 0 */
    stamp = get_string_parse_katcl(px, 1);
    /* 1: 2 */
    name = get_string_parse_katcl(px, 3);
    status = get_string_parse_katcl(px, 4);
    value = get_string_parse_katcl(px, 5);

    if((stamp == NULL) || (name == NULL) || (status == NULL) || (value == NULL)){
      if(name){
        if(stamp == NULL){
          tmp = "timestamp";
        } else if(status == NULL){
          tmp = "status";
        } else {
          tmp = "value";
        }
        if((fx->f_flags & KATCP_FLAT_PERMITNUL) == 0){
          log_message_katcp(d, (count < 6) ? KATCP_LEVEL_WARN : KATCP_LEVEL_INFO, NULL, "encountered sensor update for %s which lacks a valid %s field", name, tmp);
        }
      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "encountered sensor update without a valid name");
      }
      return -1;
    }

    if(fixup_timestamp_katcp(stamp, buffer, TIMESTAMP_BUFFER) < 0){
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "timestamp %s not reasonable", stamp);
      return -1;
    }
    buffer[TIMESTAMP_BUFFER - 1] = '\0';

    ptr = make_child_field_katcp(d, fx, name, (fx->f_flags & KATCP_FLAT_PREFIXED) ? fx->f_name : NULL, 0);
    if(ptr == NULL){
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "unable fixup sensor name %s", name);
      return -1;
    }

    vx = find_vrbl_katcp(d, ptr);
    if(vx){
      if(is_vrbl_sensor_katcp(d, vx)){

        if(vx->v_flags & KATCP_VRF_HID){
          log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "variable %s is hidden", ptr);
          unhide = 0;
        } else {
          hide_vrbl_katcp(d, vx);
          unhide = 1;
        }

        scan_vrbl_katcp(d, vx, buffer, KATCP_VRC_SENSOR_TIME,   1, KATCP_VRT_STRING);
        scan_vrbl_katcp(d, vx, status, KATCP_VRC_SENSOR_STATUS, 1, KATCP_VRT_STRING);

        if(unhide){
          show_vrbl_katcp(d, vx);
        }

        scan_vrbl_katcp(d, vx, value,  KATCP_VRC_SENSOR_VALUE,  1, KATCP_VRT_STRING);

      } else {
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "variable %s exists but not a sensor thus not propagating it", ptr);
      }
    } else {
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "no declaration for sensor %s thus not propagating it", ptr);
    }

    free(ptr);
  } else {

    if(fx->f_flags & KATCP_FLAT_TOCLIENT){

      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "origin endpoint of message is %p, remote %p", origin, remote);

      if(remote){
        /* WARNING: this is needed to relay variable subscriptions */
        send_message_endpoint_katcp(d, self, remote, px, 0);
      } else {
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal problem, saw a sensor status but have nowhere to send it to");
      }

    } else {
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "%s is not a client so not receiving sensor status messages", fx->f_name ? fx->f_name : "unknown party");
    }
  }

  return 0;
#undef TIMESTAMP_BUFFER
}

#endif
