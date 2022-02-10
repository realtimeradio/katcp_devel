#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>
#include <limits.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>

#include <netc.h>
#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

/* logging related commands *****************************************/

#define LEVEL_EXTENT_FLAT       0x1
#define LEVEL_EXTENT_GROUP      0x2
#define LEVEL_EXTENT_DEFAULT    0x4
#define LEVEL_EXTENT_REACH      0x8
#define LEVEL_EXTENT_LAYER      0x10

#define LEVEL_EXTENT_LEVELS     0x07
#define LEVEL_EXTENT_DETECT     0x1f

int set_group_log_level_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, struct katcp_group *gx, unsigned int level, unsigned int immediate)
{
  struct katcp_flat *fy;
  unsigned int i;

  if(level >= KATCP_MAX_LEVELS){
    return -1;
  }

  if(fx){
    fx->f_log_level = level;
    return level;
  }

  if(gx){
    if(immediate){
      for(i = 0; i < gx->g_count; i++){
        fy = gx->g_flats[i];
        fy->f_log_level = level;
      }
    }
    gx->g_log_level = level;
    return level;
  }

  return -1;
}

int generic_log_level_group_cmd_katcp(struct katcp_dispatch *d, int argc, unsigned int clue)
{
#define SMALL 16
  struct katcp_flat *fx;
  struct katcp_group *gx;
  int level, scope, layer, valid;
  unsigned int type;
  char *name, *requested, *extent, *end;
  char buffer[SMALL];

  level = (-1);
  scope = (-1);
  layer = INT_MAX;
  valid = 0;

  type = clue;

  if(argc > 1){
    requested = arg_string_katcp(d, 1);
    if(requested){
      if(type & LEVEL_EXTENT_LEVELS){
        if(!strcmp(requested, "all")){
          level = KATCP_LEVEL_TRACE;
        } else {
          level = log_to_code_katcl(requested);
        }
        if(level >= 0){
          valid = 1;
          type &= LEVEL_EXTENT_LEVELS;
        }
      }
      if(type & LEVEL_EXTENT_LAYER){
        layer = strtol(requested, &end, 10);
        if(end[0] == '\0'){
          valid = 1;
          type &= LEVEL_EXTENT_LAYER;
        }
      }
      if(type & LEVEL_EXTENT_REACH){
        scope = code_from_scope_katcp(requested);
        if(scope >= 0){
          valid = 1;
          type &= LEVEL_EXTENT_REACH;
        }
      }
    }
    if(valid == 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown log setting %s", requested ? requested : "<null>");
    }
  }

  fx = NULL;
  gx = NULL;
  name = NULL;

  if(type & (type - 1)){ /* heh */
    type = LEVEL_EXTENT_FLAT;
    if(argc > 2){
      extent = arg_string_katcp(d, 2);
      if(extent){
        if(!strcmp("client", extent)){
          type = LEVEL_EXTENT_FLAT;
        } else if(!strcmp("group", extent)){
          type = LEVEL_EXTENT_GROUP;
        } else if(!strcmp("default", extent)){
          type = LEVEL_EXTENT_DEFAULT;
        } else if(!strcmp("reach", extent)){
          type = LEVEL_EXTENT_REACH;
        } else if(!strcmp("layer", extent)){
          type = LEVEL_EXTENT_LAYER;
        } else {
          name = extent;
        }
      }
    }

    if(argc > 3){
      if(name){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "superfluous argument following %s", name);
        return KATCP_RESULT_FAIL;
      }
      name = arg_string_katcp(d, 3);
      if(name == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no or null subject for log adjustment");
        return KATCP_RESULT_FAIL;
      }
    }
  }

  switch(type){
    case LEVEL_EXTENT_FLAT    :
    case LEVEL_EXTENT_REACH   :
    case LEVEL_EXTENT_LAYER    :
      if(name){
        fx = scope_name_full_katcp(d, NULL, NULL, name);
      } else {
        fx = this_flat_katcp(d);
      }
      if(fx == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate a client with the name %s", name);
        return KATCP_RESULT_FAIL;
      }
      break;
    case LEVEL_EXTENT_GROUP   :
    case LEVEL_EXTENT_DEFAULT :
      if(name){
        gx = find_group_katcp(d, name);
      } else {
        gx = this_group_katcp(d);
      }
      if(gx == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate a client group with the name %s", name);
        return KATCP_RESULT_FAIL;
      }
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem trying to disambiguate 0x%x", type);
      return KATCP_RESULT_FAIL;
  }

#ifdef PARANOID /* redundant */
  if((fx == NULL) && (gx == NULL)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to work out which entity should have its log level accessed for extent %u", type);
    return KATCP_RESULT_FAIL;
  }
#endif

  switch(type){
    case LEVEL_EXTENT_FLAT    :
      if(fx){
        level = (level < 0) ? fx->f_log_level : set_group_log_level_katcp(d, fx, NULL, level, 0);
      }
      break;
    case LEVEL_EXTENT_GROUP   :
      if(gx){
        level = (level < 0) ? gx->g_log_level : set_group_log_level_katcp(d, NULL, gx, level, 1);
      }
      break;
    case LEVEL_EXTENT_DEFAULT :
      if(gx){
        level = (level < 0) ? gx->g_log_level : set_group_log_level_katcp(d, NULL, gx, level, 0);
      }
      break;
    case LEVEL_EXTENT_REACH   :
      if(fx){
        if(scope >= 0){
          fx->f_log_reach = scope_to_level_katcp(scope);
        } else {
          scope = level_to_scope_katcp(fx->f_log_reach);
        }
      }
      break;
    case LEVEL_EXTENT_LAYER   :
      if(fx){
        if(layer == INT_MAX){
          layer = fx->f_layer;
        } else {
          fx->f_layer = layer;
        }
      }
      break;
  }

  switch(type){
    case LEVEL_EXTENT_FLAT    :
    case LEVEL_EXTENT_GROUP   :
    case LEVEL_EXTENT_DEFAULT :
      if(level < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrieve or set a log level in this context");
        return KATCP_RESULT_FAIL;
      }

      name = log_to_string_katcl(level);
      if(name == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
        fprintf(stderr, "dpx: major logic problem: unable to convert %d to a symbolic log name\n", level);
        abort();
#endif
        return KATCP_RESULT_FAIL;
      }
      break;
    case LEVEL_EXTENT_REACH   :
      if(scope < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrieve or set a log reach in this context");
        return KATCP_RESULT_FAIL;
      }
      name = string_from_scope_katcp(scope);
      break;
    case LEVEL_EXTENT_LAYER   :
      if(layer == INT_MAX){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrieve or set a logging layer in this context");
        return KATCP_RESULT_FAIL;
      }
      snprintf(buffer, SMALL - 1, "%d", layer);
      name = buffer;
      break;
  }

  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problems while adjusting log settings");
    return KATCP_RESULT_FAIL;
  }


#ifdef DEBUG
  if(extra_response_katcp(d, KATCP_RESULT_OK, name) < 0){
    fprintf(stderr, "dpx[%p]: unable to generate extended response\n", fx);
  }
  fprintf(stderr, "dpx[%p]: completed log logic with own response messages\n", fx);
#else
  extra_response_katcp(d, KATCP_RESULT_OK, name);
#endif

  return KATCP_RESULT_OWN;
#undef SMALL
}

int log_level_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return generic_log_level_group_cmd_katcp(d, argc, LEVEL_EXTENT_GROUP);
}

int log_local_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return generic_log_level_group_cmd_katcp(d, argc, LEVEL_EXTENT_FLAT);
}

int log_default_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return generic_log_level_group_cmd_katcp(d, argc, LEVEL_EXTENT_DEFAULT);
}

int log_override_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return generic_log_level_group_cmd_katcp(d, argc, LEVEL_EXTENT_DETECT);
}

/* help ********************************************************************/


int print_help_cmd_item(struct katcp_dispatch *d, void *global, char *key, void *v)
{
  struct katcp_cmd_item *i;
  unsigned int *cp;

  cp = global;

  i = v;

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "%s is %s with %d references and %s local data", i->i_name, i->i_flags & KATCP_MAP_FLAG_HIDDEN ? "hidden" : "visible", i->i_refs, i->i_data ? "own" : "no");

  if(i->i_flags & KATCP_MAP_FLAG_HIDDEN){
    return -1;
  }

  prepend_inform_katcp(d);

  append_string_katcp(d, KATCP_FLAG_STRING, i->i_name);
  append_string_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_STRING, i->i_help);

  *cp = (*cp) + 1;

  return 0;
}

int help_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcp_cmd_item *i;
  struct katcp_cmd_map *mx;
  char *name, *match;
  unsigned int count;

  fx = require_flat_katcp(d);
  count = 0;

  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "help group called outside expected path");
    return KATCP_RESULT_FAIL;
  }

  mx = map_of_flat_katcp(fx);
  if(mx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no map to be found for client %s", fx->f_name);
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "should generate list of commands");
    if(mx->m_tree){
      complex_inorder_traverse_avltree(d, mx->m_tree->t_root, (void *)(&count), &print_help_cmd_item);
    }
  } else {
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "should provide help for %s", name);
    switch(name[0]){
      case KATCP_REQUEST :
      case KATCP_REPLY   :
      case KATCP_INFORM  :
        match = name + 1;
        break;
      default :
        match = name;
    }
    if(match[0] == '\0'){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to provide help on a null command");
      return KATCP_RESULT_FAIL;
    } else {
      i = find_data_avltree(mx->m_tree, match);
      if(i == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no match for %s found", name);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      } else {
        print_help_cmd_item(d, (void *)&count, match, (void *)i);
      }
    }
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", count);
}

/* watchdog */

int watchdog_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *source;
  struct katcp_flat *fx;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    source = "simplex";
  } else {
    if(remote_of_flat_katcp(d, fx) == sender_to_flat_katcp(d, fx)){
      source = "remote";
    } else {
      source = "internal";
    }
  }

  log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "fielding %s ping request", source);

  return KATCP_RESULT_OK;
}

/* client list */

static int print_client_list_katcp(struct katcp_dispatch *d, struct katcp_flat *fx, char *name)
{
#define BUFFER 64
  int result, r, pending;
  char *ptr;
  struct katcp_group *gx;
  struct katcp_shared *s;
  char buffer[BUFFER];
  struct katcp_response_handler *rh;
  unsigned int i, count;
  struct timeval now, delta;
  struct katcl_parse *px;
  unsigned int size;

  s = d->d_shared;
  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  if(fx->f_name == NULL){
    return 0;
  }

  if(name && strcmp(name, fx->f_name)){
    return 0;
  }

  if((fx->f_flags & KATCP_FLAT_HIDDEN) && (name == NULL)){
    return 0;
  }

  r = prepend_inform_katcp(d);
  if(r < 0){
#ifdef DEBUG
    fprintf(stderr, "print_client_list: prepend failed\n");
#endif
    return -1;
  }
  result = r;

  r = append_string_katcp(d, KATCP_FLAG_LAST | KATCP_FLAG_STRING, fx->f_name);
  if(r < 0){
#ifdef DEBUG
    fprintf(stderr, "print_client_list: append of %s failed\n", fx->f_name);
#endif
    return -1;
  }

  result += r;

  gettimeofday(&now, NULL);

  print_time_delta_katcm(buffer, BUFFER, now.tv_sec - fx->f_start);

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "%s connection", fx->f_name);

#if KATCP_PROTOCOL_MAJOR_VERSION >= 5
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s was started at %lu", fx->f_name, fx->f_start);
#else
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s was started at %lu000", fx->f_name, fx->f_start);
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has been running for %s", fx->f_name, buffer);

  ptr = log_to_string_katcl(fx->f_log_level);
  if(ptr){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has log level <%s>", fx->f_name, ptr);
  } else {
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "client %s has unreasonable log level", fx->f_name);
  }

  if(fx->f_flags & KATCP_FLAT_HIDDEN){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s will not be listed", fx->f_name);
  }

  if(fx->f_flags & KATCP_FLAT_TOSERVER){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s may issue requests as peer is a server", fx->f_name);
  }
  if(fx->f_flags & KATCP_FLAT_TOCLIENT){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s may field requests as peer is a client", fx->f_name);
  }

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s prefix its name to sensor definitions", fx->f_name, (fx->f_flags & KATCP_FLAT_PREFIXED) ? "will" : "will not");
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s prepend its name to version messages", fx->f_name, (fx->f_flags & KATCP_FLAT_PREPEND) ? "will" : "will not");

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s prefix its name to log inform messages", fx->f_name, (fx->f_flags & KATCP_FLAT_LOGPREFIX) ? "will" : "will not");

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s translate relayed inform messages", fx->f_name, (fx->f_flags & KATCP_FLAT_RETAINFO) ? "will not" : "will");

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s relay inform messages", fx->f_name, (fx->f_flags & KATCP_FLAT_INSTALLINFO) ? "will" : "will not");
  if(fx->f_flags & KATCP_FLAT_INSTALLINFO){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s translate relayed inform messages", fx->f_name, (fx->f_flags & KATCP_FLAT_RETAINFO) ? "will not" : "will");
  }
#if 0
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s will lookup default inform handler %s", fx->f_name, (fx->f_flags & KATCP_FLAT_RUNMAPTOO) ? "regardless of reply handler" : "if no reply handler available");
#endif

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s accept katcp admin messages", fx->f_name, (fx->f_flags & KATCP_FLAT_SEESKATCP) ? "will" : "will not");
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s accept extra admin messages", fx->f_name, (fx->f_flags & KATCP_FLAT_SEESADMIN) ? "will" : "will not");
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s accept user messages", fx->f_name, (fx->f_flags & KATCP_FLAT_SEESUSER) ? "will" : "will not");
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s relay messages seen in map handlers", fx->f_name, (fx->f_flags & KATCP_FLAT_SEESMAPINFO) ? "will" : "will not");

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s %s accept and relay empty sensor fields", fx->f_name, (fx->f_flags & KATCP_FLAT_PERMITNUL) ? "will" : "will not");

  ptr = string_from_scope_katcp(fx->f_scope);
  if(ptr){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has %s scope", fx->f_name, ptr);
  } else {
    log_message_katcp(d, KATCP_LEVEL_WARN | KATCP_LEVEL_LOCAL, NULL, "client %s has invalid scope", fx->f_name);
  }

  ptr = string_from_scope_katcp(level_to_scope_katcp(fx->f_log_reach));
  if(ptr){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s collected log messages have %s scope", fx->f_name, ptr);
  } else {
    log_message_katcp(d, KATCP_LEVEL_WARN | KATCP_LEVEL_LOCAL, NULL, "client %s has invalid log message scope", fx->f_name);
  }

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s is in layer %d", fx->f_name, fx->f_layer);

  switch((fx->f_stale & KATCP_STALE_MASK_SENSOR)){
    case KATCP_STALE_SENSOR_NAIVE :
      log_message_katcp(d, KATCP_LEVEL_DEBUG | KATCP_LEVEL_LOCAL, NULL, "client %s has not listed sensors", fx->f_name);
      break;
    case KATCP_STALE_SENSOR_CURRENT :
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has up to date sensor listing", fx->f_name);
      break;
    case KATCP_STALE_SENSOR_STALE :
      log_message_katcp(d, KATCP_LEVEL_WARN | KATCP_LEVEL_LOCAL, NULL, "client %s has sensor listing which is out of date", fx->f_name);
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_FATAL | KATCP_LEVEL_LOCAL, NULL, "client %s has corrupt sensor state tracking", fx->f_name);
      break;
  }

  log_message_katcp(d, ((fx->f_max_defer > 0) ? KATCP_LEVEL_WARN : KATCP_LEVEL_INFO) | KATCP_LEVEL_LOCAL, NULL, "client %s has had a maximum of %d requests outstanding", fx->f_name, fx->f_max_defer);

  for(i = 0; i < KATCP_SIZE_REPLY; i++){
    rh = &(fx->f_replies[i]);
    if(rh->r_reply != NULL){
      sub_time_katcp(&delta, &now, &(rh->r_when));
      log_message_katcp(d, (rh->r_message ? KATCP_LEVEL_INFO : KATCP_LEVEL_ERROR) | KATCP_LEVEL_LOCAL, NULL, "client %s has request %u queued for %s issued %lu.%06lus ago", fx->f_name, i, rh->r_message ? rh->r_message : "<UNKNOWN>", delta.tv_sec, delta.tv_usec);
    }
  }

  if(fx->f_deferring & KATCP_DEFER_OWN_REQUEST){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s is deferring - awaiting reply to its request", fx->f_name);
  }
  if(fx->f_deferring & KATCP_DEFER_OUTSIDE_REQUEST){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s is deferring - stalling request made of it", fx->f_name);
  }

  size = size_gueue_katcl(fx->f_defer);
  if(size > 0){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has %u requests in queue", fx->f_name, size);
    for(i = 0; i < size; i++){
      px = get_from_head_gueue_katcl(fx->f_defer, i);
      if(px){
        ptr = get_string_parse_katcl(px, 0);
        if(ptr){
          log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s at %u deferred %s ...", fx->f_name, i, ptr);
        }
      }
    }
  }

  if(flushing_katcl(fx->f_line)){
    count = flushing_queue_katcl(fx->f_line);
    log_message_katcp(d, ((count > 1) ? KATCP_LEVEL_WARN : KATCP_LEVEL_INFO) | KATCP_LEVEL_LOCAL, NULL, "client %s has %u messages of %u limit in output queue", fx->f_name, count, fx->f_pending_limit);

    count = flushing_bytes_katcl(fx->f_line);
    log_message_katcp(d, ((count > KATCL_IO_SIZE) ? KATCP_LEVEL_WARN : KATCP_LEVEL_INFO) | KATCP_LEVEL_LOCAL, NULL, "client %s has %u bytes of %u limit stalled in output buffer", fx->f_name, count, fx->f_pending_limit);
  }

  gx = fx->f_group;
  if(gx && gx->g_name){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s member of group <%s>", fx->f_name, gx->g_name);
  }

  pending = pending_endpoint_katcp(d, fx->f_peer);
  if(pending > 0){
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s has %d %s in queue", fx->f_name, pending, (pending > 1) ? "commands" : "command");
  }

  sub_time_katcp(&delta, &now, &(fx->f_last_write));
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s last write performed %lu.%06lus ago", fx->f_name, delta.tv_sec, delta.tv_usec);

  sub_time_katcp(&delta, &now, &(fx->f_last_read));
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "client %s last read %lu.%06lus ago", fx->f_name, delta.tv_sec, delta.tv_usec);

  show_endpoint_katcp(d, fx->f_name ? fx->f_name : "unknown", KATCP_LEVEL_DEBUG | KATCP_LEVEL_LOCAL, fx->f_peer);
  show_endpoint_katcp(d, fx->f_name ? fx->f_name : "unknown", KATCP_LEVEL_TRACE | KATCP_LEVEL_LOCAL, fx->f_remote);

  log_message_katcp(d, KATCP_LEVEL_DEBUG | KATCP_LEVEL_LOCAL, NULL, "client %s running with fd %d", fx->f_name ? fx->f_name : "unknown", fileno_katcl(fx->f_line));

  return result;
#undef BUFFER
}

int client_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_shared *s;
  struct katcp_group *gx;
  struct katcp_flat *fx, *fy;
  unsigned int i, j, total;
  char *name;

  s = d->d_shared;
  total = 0;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client data available, probably called in incorrect context");
    return KATCP_RESULT_FAIL;
  }

  if(argc > 1){
    name = arg_string_katcp(d, 1);
  } else {
    name = NULL;
  }

  switch(fx->f_scope){
    case KATCP_SCOPE_LOCAL :
      /* WARNING: maybe a client can not be hidden from itself ? */
      if(print_client_list_katcp(d, fx, name) > 0){
        total++;
      }
      break;
    case KATCP_SCOPE_GROUP :
      gx = this_group_katcp(d);
      if(gx){
        for(i = 0; i < gx->g_count; i++){
          fy = gx->g_flats[i];
          if(print_client_list_katcp(d, fy, name) > 0){
            total++;
          }
        }
      }
      break;
    case KATCP_SCOPE_ALL :
      for(j = 0; j < s->s_members; j++){
        gx = s->s_groups[j];
        if(gx){
          for(i = 0; i < gx->g_count; i++){
            fy = gx->g_flats[i];
            if(print_client_list_katcp(d, fy, name) > 0){
              total++;
            }
          }
        }
      }
      break;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid client scope %d for %s\n", fx->f_scope, fx->f_name);
      return KATCP_RESULT_FAIL;
  }

  if((total == 0) && (name != NULL)){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", total);
}

/* halt and restart *************************************************/

int restart_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  /* halt and restart handlers could really be merged into one function */

  struct katcp_group *gx;
  struct katcp_flat *fx;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client data available, probably called in incorrect context");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "restart request from %s", fx->f_name ? fx->f_name : "<unknown party>");

  switch(fx->f_scope){
    case KATCP_SCOPE_LOCAL :
      if(terminate_flat_katcp(d, fx) < 0){
        return KATCP_RESULT_FAIL;
      }
      return KATCP_RESULT_OK;
    case KATCP_SCOPE_GROUP :
      gx = fx->f_group;
      if(gx){
        if(terminate_group_katcp(d, gx, 0) >= 0){
          return KATCP_RESULT_OK;
        }
      }
      return KATCP_RESULT_FAIL;
    case KATCP_SCOPE_ALL :
      terminate_katcp(d, KATCP_EXIT_RESTART);
      return KATCP_RESULT_OK;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid client scope %d for %s\n", fx->f_scope, fx->f_name);
      return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int halt_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_group *gx;
  struct katcp_flat *fx;

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client data available, probably called in incorrect context");
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "halt request from %s", fx->f_name ? fx->f_name : "<unknown party>");

  switch(fx->f_scope){
    case KATCP_SCOPE_LOCAL :
      if(terminate_flat_katcp(d, fx) < 0){
        return KATCP_RESULT_FAIL;
      }
      return KATCP_RESULT_OK;
    case KATCP_SCOPE_GROUP :
      gx = fx->f_group;
      if(gx){
        log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "initiating halt for %s group", gx->g_name ? gx->g_name : "<unnamed>");
        if(terminate_group_katcp(d, gx, 1) >= 0){
          return KATCP_RESULT_OK;
        }
      }
      return KATCP_RESULT_FAIL;
    case KATCP_SCOPE_ALL :
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "initiating global halt");
      terminate_katcp(d, KATCP_EXIT_HALT);
      return KATCP_RESULT_OK;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid client scope %d for %s\n", fx->f_scope, fx->f_name);
      return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

/* sensor related functions ***************************************************/

int sensor_list_callback_katcp(struct katcp_dispatch *d, unsigned int *count, char *key, struct katcp_vrbl *vx)
{
  struct katcp_vrbl_payload *value, *help, *units, *type, *range;
  char *fallback;
  int result;
#if 0
  struct katcp_vrbl_payload *prefix;
  char *front;
#endif
  struct katcp_flat *fx;
  int fill;

  result = is_vrbl_sensor_katcp(d, vx);
  if(result <= 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    if(result < 0){
      fprintf(stderr, "sensor: sensor %s severely malformed\n", key);
      abort();
    }
#endif
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "suppressing listing of variable %s", key);
    return result;
  }

  if(vx->v_flags & KATCP_VRF_HID){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "suppressing listing of hidden sensor %s", key);
    return 0;
  }


#if 0
  vy = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_VALUE);
  uy = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_UNITS);
  hy = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_HELP);
  ty = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_TYPE);
#endif

  value = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_VALUE);
  if(value == NULL){
    return 0;
  }
  fallback = type_to_string_vrbl_katcp(d, value->p_type);
  if(fallback == NULL){
    return 0;
  }

#if 0
  prefix = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_PREFIX);
  if(prefix){
    /* TODO: should extract the prefix, convert it to a string (no vrbl API for that yet, then store it in front */
  }
#endif

#if 0
  front = NULL;
#endif
  fill = 1;
  fx = this_flat_katcp(d);
  if(fx){
    if(fx->f_flags & KATCP_FLAT_PERMITNUL){
      fill = 0;
    }
  }

  prepend_inform_katcp(d);

#if 0
  if(front){
    append_args_katcp(d, KATCP_FLAG_STRING, "%s.%s", front, key);
  } else {
#endif
    append_string_katcp(d, KATCP_FLAG_STRING, key);
#if 0
  }
#endif

  help = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_HELP);
  if(help && (help->p_type == KATCP_VRT_STRING)){
    append_payload_vrbl_katcp(d, 0, vx, help);
  } else {
    append_string_katcp(d, KATCP_FLAG_STRING, fill ? "undocumented" : NULL);
  }

  units = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_UNITS);
  if(units && (units->p_type == KATCP_VRT_STRING)){
    append_payload_vrbl_katcp(d, 0, vx, units);
  } else {

    append_string_katcp(d, KATCP_FLAG_STRING, fill ? "none" : NULL);
  }

  range = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_RANGE);

  type = find_payload_katcp(d, vx, KATCP_VRC_SENSOR_TYPE);
  if(type && (type->p_type == KATCP_VRT_STRING)){
    /* allows us to override the native type to something in sensor world */
    append_payload_vrbl_katcp(d, (range ? 0 : KATCP_FLAG_LAST), vx, type);
  } else {
    append_string_katcp(d, KATCP_FLAG_STRING | (range ? 0 : KATCP_FLAG_LAST), fallback);
  }

  if(range){
    append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, range);
  }

  if(count){
    *count = (*count) + 1;
  }

  return 0;
}

int sensor_list_void_callback_katcp(struct katcp_dispatch *d, void *state, char *key, void *data)
{
  struct katcp_vrbl *vx;
  unsigned int *count;

  vx = data;
  count = state;

  return sensor_list_callback_katcp(d, count, key, vx);
}

int sensor_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *key;
  unsigned int i, count;
  int result;
  struct katcp_vrbl *vx;
  struct katcp_flat *fx;

  result = 0;
  count = 0;

  if(argc > 1){
    /* One of the few commands which accepts lots of parameters */
    for(i = 1 ; i < argc ; i++){
      key = arg_string_katcp(d, i);
      if(key == NULL){
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
      }
      vx = find_vrbl_katcp(d, key);
      if(vx == NULL){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%s not found", key);
        result = (-1);
      } else {
        if(sensor_list_callback_katcp(d, &count, key, vx) < 0){
          result = (-1);
        }
      }
    }
  } else {
    result = traverse_vrbl_katcp(d, (void *)&count, &sensor_list_void_callback_katcp);
    fx = this_flat_katcp(d);
    if(fx){
      fx->f_stale = (fx->f_stale & (~KATCP_STALE_MASK_SENSOR)) | KATCP_STALE_SENSOR_CURRENT;
    }
  }

  if(result < 0){
    return KATCP_RESULT_FAIL;
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", count);
}

int sensor_sampling_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  int result;
  char *key, *strategy;
  struct katcp_vrbl *vx;
  struct katcp_flat *fx;
  int current_stg, new_stg;
#ifdef KATCP_HEAP_TIMERS
  struct timeval tv;
  char *temp;
  int ret;
  int len;
#endif

  if(argc <= 1){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  key = arg_string_katcp(d, 1);
  if(key == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  vx = find_vrbl_katcp(d, key);
  if(vx == NULL){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%s not found", key);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  result = is_vrbl_sensor_katcp(d, vx);
  if(result <= 0){
#ifdef KATCP_CONSISTENCY_CHECKS
    if(result < 0){
      fprintf(stderr, "sensor: sensor %s severely malformed\n", key);
      abort();
    }
#endif
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not using variable %s", key);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  if(vx->v_flags & KATCP_VRF_HID){
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not using hidden sensor %s", key);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "sensor sampling not available outside duplex context");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  current_stg = current_strategy_sensor_katcp(d, vx, fx);

  if(argc <= 2){
    /* WARNING: will have to get more information for period sensors ... */

    strategy = strategy_to_string_sensor_katcp(d, current_stg);

    if(strategy == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }

  } else {

    strategy = arg_string_katcp(d, 2);
    if(strategy == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }

    new_stg = strategy_from_string_sensor_katcp(d, strategy);

#if 0
    /* NOT safe, a client may be subscribed to loads of sensors */
    fx->f_rename_lock = 0;
#endif

    switch(new_stg){
      case KATCP_STRATEGY_EVENT :
        if(current_stg == KATCP_STRATEGY_PERIOD){
          if(forget_period_variable_katcp(d, vx, fx) < 0){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to unsubscribe period based sampling for sensor %s", key);
            return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
          }
        }

        /* try to register event subscription */
        if(monitor_event_variable_katcp(d, vx, fx) < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register event based sampling for sensor %s", key);
          return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
        }

        break;

      case KATCP_STRATEGY_PERIOD :
#ifdef KATCP_HEAP_TIMERS
        if (string_to_tv_katcp(&tv, arg_string_katcp(d, 3))){
          return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
        }

        if(current_stg == KATCP_STRATEGY_EVENT){
          if(forget_event_variable_katcp(d, vx, fx) < 0){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to unsubscribe event based sampling for sensor %s", key);
            return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
          }
        }

        len = strlen(key) + strlen(fx->f_name) + strlen(fx->f_group->g_name) + 1 + 1 + 1;   /* group name + dot + connection name + dot + sensor name + '\0' */

        temp = (char *) malloc(len);
        if (NULL == temp){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate memory for named timer instance");
          return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
        }

        snprintf(temp, len, "%s.%s.%s", fx->f_group->g_name, fx->f_name, key);
        /* FIXME: handle case where client is renamed or client moves to new group -> timer name needs to change */
        /*        in order to maintain "lookup-ability" */
        temp[len - 1] = '\0';

        ret = monitor_period_variable_katcp(d, vx, fx, &tv, temp);
//        ret = monitor_period_variable_katcp(d, vx, fx, &tv, NULL);
        free(temp);

        if(ret < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register period based sampling for sensor %s", key);
          return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
        }

        /* disable renaming of flat */
        fx->f_rename_lock = 1;
#else
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no period sensor sampling support - rebuild with HEAP_TIMER to enable it");
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
#endif
        break;

      case KATCP_STRATEGY_OFF :

        /* get current strategy and call corresponding forget function */
        if(current_stg == KATCP_STRATEGY_EVENT){
          if(forget_event_variable_katcp(d, vx, fx) < 0){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to unsubscribe event based sampling for sensor %s", key);
            return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
          }
        }

        if(current_stg == KATCP_STRATEGY_PERIOD){
          if(forget_period_variable_katcp(d, vx, fx) < 0){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to unsubscribe period based sampling for sensor %s", key);
            return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
          }
        }
        break;

      /* TODO */
      case KATCP_STRATEGY_DIFF :
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "sampling strategy %s not implemented for sensor %s", strategy, key);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);

      default :
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid sensor sampling strategy %s", strategy);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }
  }

  /* WARNING: all ERROR paths should have exited above ... can safely report now */

#ifdef KATCP_CONSISTENCY_CHECKS
  if((key == NULL) || (strategy == NULL)){
    fprintf(stderr, "dpx: sensor-sampling: logic problem: essential display field left out\n");
    abort();
  }
#endif

  prepend_reply_katcp(d);

  append_string_katcp(d, KATCP_FLAG_STRING, KATCP_OK);
  append_string_katcp(d, KATCP_FLAG_STRING, key);
  append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, strategy);

  dump_variable_sensor_katcp(d, vx, KATCP_LEVEL_DEBUG);

  return KATCP_RESULT_OWN;
}

int bulk_sensor_sampling_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  int result;
  char *key, *string;
  struct katcp_vrbl *vx;
  struct katcp_flat *fx;
  int strategy, prior;
  struct timeval tv;
  int back, i;
#ifdef KATC_HEAPTIMERS
  char *temp;
  int len;
#endif
  char *key_base;
  char *key_start;
  char *key_end;
  int done = 0;
  unsigned int index = 0;

  if(argc <= 2){
    if(argc > 1){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "bulk sensor sampling can not be used to query sensor strategies");
    }
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "sensor sampling not available outside duplex context");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  back = argc - 1;
  for(strategy = (-1); (strategy < 0) && (back > 1); back--){
    string = arg_string_katcp(d, back);
    if(string == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }

    strategy = strategy_from_string_sensor_katcp(d, string);

    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "potential strategy %d at parameter [%d]=%s", strategy, back, string);
  }

  if(strategy < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to discover a strategy from argument %d onwards", back + 1);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  switch(strategy){
    case KATCP_STRATEGY_OFF :
      break;

    case KATCP_STRATEGY_EVENT :
      break;

    case KATCP_STRATEGY_PERIOD :
      if (string_to_tv_katcp(&tv, arg_string_katcp(d, 3))){
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
      }
      break;

    case KATCP_STRATEGY_DIFF :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "sampling strategy %s not implemented", strategy);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);

    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid sensor sampling strategy number %d", strategy);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  key_base = arg_copy_string_katcp(d, 1);

  if(key_base == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  key_start = key_base;
  do{
    i = 1;
    key_end = strchr(key_start, ',');
    if(key_end) {
      index = key_end - key_start;
      key_start[index] = '\0';
    } else {
      done = 1;
    }
    key = key_start;
    if(key == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no useful sensor in parameter %d", i);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }

    vx = find_vrbl_katcp(d, key);
    if(vx == NULL){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "no sensor %s found", key);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }

    result = is_vrbl_sensor_katcp(d, vx);
    if(result <= 0){
#ifdef KATCP_CONSISTENCY_CHECKS
      if(result < 0){
        fprintf(stderr, "sensor: sensor %s severely malformed\n", key);
        abort();
      }
#endif
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not using variable %s", key);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }

    if(vx->v_flags & KATCP_VRF_HID){
      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "not using hidden sensor %s", key);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }

    prior = current_strategy_sensor_katcp(d, vx, fx);
    if(prior == strategy){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "no strategy change for sensor %s", key);
    } else {

      if(prior == KATCP_STRATEGY_EVENT){
        if(forget_event_variable_katcp(d, vx, fx) < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to unsubscribe event based sampling for sensor %s", key);
          return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
        }
      }

      if(prior == KATCP_STRATEGY_PERIOD){
        if(forget_period_variable_katcp(d, vx, fx) < 0){
          log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to unsubscribe period based sampling for sensor %s", key);
          return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
        }
      }

      switch(strategy){
        case KATCP_STRATEGY_EVENT :
          /* try to register event subscription */
          if(monitor_event_variable_katcp(d, vx, fx) < 0){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register event based sampling for sensor %s", key);
            return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
          }

          break;

        case KATCP_STRATEGY_PERIOD :
#ifdef KATCP_HEAPTIMERS
          len = strlen(key) + strlen(fx->f_name) + strlen(fx->f_group->g_name) + 1 + 1 + 1;   /* group name + dot + connection name + dot + sensor name + '\0' */

          temp = (char *) malloc(len);
          if (NULL == temp){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate memory for named timer instance for sensor %s", key);
            return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
          }

          snprintf(temp, len, "%s.%s.%s", fx->f_group->g_name, fx->f_name, key);
          temp[len - 1] = '\0';
          result = monitor_period_variable_katcp(d, vx, fx, &tv, temp);
          free(temp);

          if(result < 0){
            log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to register period based sampling for sensor %s", key);
            return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
          }

          /* WARNING: a timer means that the flat can not be renamed */
          fx->f_rename_lock = 1;
#else
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no period sensor sampling support - rebuild with HEAP_TIMER to enable it");
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
#endif

          break;

        case KATCP_STRATEGY_OFF :
          break;

        default :
          /* NOT reached, real programs dump core :) */
          return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
      }
    }

    if(key_end){
      key_start = key_end + 1;
    }
    i++;
  } while (!done);

  free(key_base);
  return KATCP_RESULT_OK;
}

/********************************************************************************/

int sensor_value_callback_katcp(struct katcp_dispatch *d, unsigned int *count, char *key, struct katcp_vrbl *vx)
{
  struct katcl_parse *px;
  int result;

  result = is_vrbl_sensor_katcp(d, vx);
  if(result <= 0){
#ifdef DEBUG
    fprintf(stderr, "sensor value: %p not a sensor\n", vx);
#endif
    return result;
  }

  if(vx->v_flags & KATCP_VRF_HID){
#ifdef DEBUG
    fprintf(stderr, "sensor value: %p is hidden\n", vx);
#endif
    return 0;
  }

  px = make_sensor_katcp(d, key, vx, KATCP_SENSOR_VALUE_INFORM);
  if(px == NULL){
#ifdef DEBUG
    fprintf(stderr, "sensor value: unable to assemble output for %p\n", vx);
#endif
    return -1;
  }

#if 1
  /* WARNING: this is a bit ugly - we need prepend_inform for future tags, but the parse layer doesn't know about tags ... */

  /* TODO: there should probably be a add_tagged_string call which can be used to build up a tagged prefix on a parse structure */

  prepend_inform_katcp(d);

  append_trailing_katcp(d, KATCP_FLAG_LAST, px, 1);
#else
  /* this won't work for tags */

  append_parse_katcp(d, px);
#endif

  destroy_parse_katcl(px);

  if(count){
    *count = (*count) + 1;
  }

  return 0;
}

int sensor_value_void_callback_katcp(struct katcp_dispatch *d, void *state, char *key, void *data)
{
  struct katcp_vrbl *vx;
  unsigned int *count;

  vx = data;
  count = state;

  return sensor_value_callback_katcp(d, count, key, vx);
}

int sensor_value_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *key;
  unsigned int i, count;
  int result;
  struct katcp_vrbl *vx;

  result = 0;
  count = 0;

  if(argc > 1){
    /* One of the few commands which accepts lots of parameters */
    for(i = 1 ; i < argc ; i++){
      key = arg_string_katcp(d, i);
      if(key == NULL){
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
      }
      vx = find_vrbl_katcp(d, key);
      if(vx == NULL){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%s not found", key);
        result = (-1);
      } else {
        if(sensor_value_callback_katcp(d, &count, key, vx) < 0){
          result = (-1);
        }
      }
    }
  } else {
    result = traverse_vrbl_katcp(d, (void *)&count, &sensor_value_void_callback_katcp);
  }

  if(result < 0){
    return KATCP_RESULT_FAIL;
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", count);
}

/* version related function ***************************************************/

#if 0
int version_list_callback_katcp(struct katcp_dispatch *d, void *state, char *key, struct katcp_vrbl *vx)
{
  struct katcp_payload *pversion, *pbuild:
  unsigned int *cp;
  int type;

  cp = state;

#ifdef DEBUG
  fprintf(stderr, "version: about to consider variable %p\n", vx);
#endif

  if(vx == NULL){
    return -1;
  }

  if((vx->v_flags & KATCP_VRF_VER) == 0){
#ifdef DEBUG
    fprintf(stderr, "version: %p not a version variable\n", vx);
#endif
    return 0;
  }

  /* WARNING: this duplicates too much from version_connect, but needs the prepend_inform */

  type = find_type_vrbl_katcp(d, vx, NULL);
  switch(type){
    case KATCP_VRT_STRING :
      prepend_inform_katcp(d);
      append_string_katcp(d, KATCP_FLAG_STRING, key);
      append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, NULL);
      *cp = (*cp) + 1;
      return 0;
    case KATCP_VRT_TREE :

      pversion = find_payload_katcp(d, vx, KATCP_VRC_VERSION_VERSION);
      pbuild = find_payload_katcp(d, vx, KATCP_VRC_VERSION_BUILD);

      if(pversion){
        if(type_payload_vrbl_katcp(d, vx, pversion) != KATCP_VRT_STRING){
          pversion = NULL;
        }
      }
      if(pbuild){
        if(type_payload_vrbl_katcp(d, vx, pbuild) != KATCP_VRT_STRING){
          pbuild = NULL;
        }
      }

      if(pversion || pbuild){
        prepend_inform_katcp(d);
        append_string_katcp(d, KATCP_FLAG_STRING, key);
        if(pbuild){
          if(pversion){
            append_payload_vrbl_katcp(d, 0, vx, pversion);
          } else {
            append_string_katcp(d, KATCP_FLAG_STRING, "unknown");
          }
          append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, pbuild);
        } else {
          append_payload_vrbl_katcp(d, KATCP_FLAG_LAST, vx, pversion);
        }
        *cp = (*cp) + 1;
      }

      return 0;

    default :
      return 0;
  }
}
#endif

int version_list_void_callback_katcp(struct katcp_dispatch *d, void *state, char *key, void *data)
{
  struct katcp_vrbl *vx;

  vx = data;

  return version_generic_callback_katcp(d, state, key, vx);
}

int version_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *key;
  unsigned int i;
  int result;
  struct katcp_vrbl *vx;
  unsigned int count;

  result = 0;
  count = 0;

#ifdef DEBUG
  fprintf(stderr, "version: invoking listing with %d parameters\n", argc);
#endif

  if(argc > 1){
    for(i = 1 ; i < argc ; i++){
      key = arg_string_katcp(d, i);
      if(key == NULL){
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
      }
      vx = find_vrbl_katcp(d, key);
      if(vx == NULL || ((vx->v_flags & KATCP_VRF_VER) == 0)){
        log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "%s not found", key);
        result = (-1);
      } else {
        if(version_generic_callback_katcp(d, (void *)&count, key, vx) < 0){
          result = (-1);
        }
      }
    }
  } else {
    result = traverse_vrbl_katcp(d, (void *)&count, &version_list_void_callback_katcp);
  }

  if(result < 0){
    return KATCP_RESULT_FAIL;
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%u", count);
}


#endif
