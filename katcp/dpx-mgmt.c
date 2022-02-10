#ifdef KATCP_EXPERIMENTAL

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <time.h>

#include <sys/socket.h>
#include <sys/stat.h>

#include <arpa/inet.h>
#include <netinet/in.h>
#include <netinet/tcp.h>

#include <netc.h>
#include <katcp.h>
#include <katpriv.h>
#include <katcl.h>

#define DEFAULT_KEEPIDLE 7200
#define DEFAULT_KEEPCNT 9
#define DEFAULT_KEEPINTVL 75

/* client stuff *******************************************************************/

int client_exec_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *label, *group;
  struct katcp_flat *fx;
  struct katcp_group *gx;
  int i, size;
  char **vector;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a command");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  label = arg_string_katcp(d, 1);
  if(label == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc > 2){
    group = arg_string_katcp(d, 2);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
    gx = find_group_katcp(d, group);
    if(gx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate group called %s", group);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }
  } else {
    gx = this_group_katcp(d);
  }

  if(argc > 3){
    vector = malloc(sizeof(char *) * (argc - 2));
    if(vector == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate %u element vector", argc - 2);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
    }
    size = argc - 3;
    for(i = 0; i < size; i++){
      vector[i] = arg_string_katcp(d, i + 3);
#ifdef DEBUG
      fprintf(stderr, "exec: vector[%u]=%s\n", i, vector[i]);
#endif
    }
    vector[i] = NULL;
  } else {
    vector = NULL;
  }

#if 0
  fx = create_exec_flat_katcp(d, KATCP_FLAT_TOSERVER | KATCP_FLAT_TOCLIENT | KATCP_FLAT_PREFIXED | KATCP_FLAT_INSTALLINFO | KATCP_FLAT_RUNMAPTOO | KATCP_FLAT_SEESKATCP | KATCP_FLAT_SEESADMIN | KATCP_FLAT_SEESUSER, label, gx, vector);
#endif
  fx = create_exec_flat_katcp(d, KATCP_FLAT_TOSERVER | KATCP_FLAT_TOCLIENT | KATCP_FLAT_PREFIXED | KATCP_FLAT_INSTALLINFO | KATCP_FLAT_SEESKATCP | KATCP_FLAT_SEESADMIN | KATCP_FLAT_SEESUSER | KATCP_FLAT_PREPEND, label, gx, vector);
  if(vector){
    free(vector);
  }

  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate client connection");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  return KATCP_RESULT_OK;
}

int client_connect_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *client, *group, *name;
  struct katcp_group *gx;
  int fd;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a destination to connect to");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  client = arg_string_katcp(d, 1);
  if(client == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc > 2){
    group = arg_string_katcp(d, 2);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
    gx = find_group_katcp(d, group);
    if(gx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate group called %s", group);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }
  } else {
    gx = this_group_katcp(d);
  }

  name = NULL;
  if(argc > 3){
    name = arg_string_katcp(d, 3);
  }
  if(name == NULL){
    name = client;
  }

  fd = net_connect(client, 0, NETC_ASYNC);
  if(fd < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to initiate connection to %s: %s", client, errno ? strerror(errno) : "unknown error");
    return KATCP_RESULT_FAIL;
  }

  fcntl(fd, F_SETFD, FD_CLOEXEC);

#if 0
  if(create_flat_katcp(d, fd, KATCP_FLAT_CONNECTING | KATCP_FLAT_TOSERVER | KATCP_FLAT_PREFIXED | KATCP_FLAT_INSTALLINFO | KATCP_FLAT_RUNMAPTOO, name, gx) == NULL){
#endif
  if(create_flat_katcp(d, fd, KATCP_FLAT_CONNECTING | KATCP_FLAT_TOSERVER | KATCP_FLAT_PREFIXED | KATCP_FLAT_INSTALLINFO | KATCP_FLAT_PREPEND, name, gx) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to allocate client connection");
    close(fd);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  return KATCP_RESULT_OK;
}

int client_config_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *option, *client;
  unsigned int mask, set;
  struct katcp_flat *fx, *fy;

  fy = this_flat_katcp(d);
  if(fy == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client scope available");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need an option");
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "supported flags include duplex server client hidden visible prefixed fixed stop-info relay-info translate native map-fallback map-always info-none info-katcp info-user info-admin info-all extra-relay extra-drop version-prepend version-unchanged permit-nul fill-nul");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  option = arg_string_katcp(d, 1);
  if(option == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire a flag");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc > 2){
    client = arg_string_katcp(d, 2);
    if(client == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire client name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
    fx = scope_name_full_katcp(d, NULL, NULL, client);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate client %s", client);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }
  } else {
    fx = fy;
  }

  set  = 0;
  mask = 0;

  if(!strcmp(option, "duplex")){
    set   = KATCP_FLAT_TOSERVER | KATCP_FLAT_TOCLIENT;
  } else if(!strcmp(option, "server")){
    mask   = KATCP_FLAT_TOSERVER;
    set    = KATCP_FLAT_TOCLIENT;
  } else if(!strcmp(option, "client")){
    mask   = KATCP_FLAT_TOCLIENT;
    set    = KATCP_FLAT_TOSERVER;
  } else if(!strcmp(option, "hidden")){
    set    = KATCP_FLAT_HIDDEN;
  } else if(!strcmp(option, "visible")){
    mask   = KATCP_FLAT_HIDDEN;
  } else if(!strcmp(option, "prefixed") || !strcmp(option, "sensor-prefix")){
    set    = KATCP_FLAT_PREFIXED;
  } else if(!strcmp(option, "fixed") || !strcmp(option, "sensor-unchanged")){
    mask   = KATCP_FLAT_PREFIXED;

  } else if(!strcmp(option, "stop-info")){
    mask   = KATCP_FLAT_INSTALLINFO;
  } else if(!strcmp(option, "relay-info")){
    set    = KATCP_FLAT_INSTALLINFO;
  } else if(!strcmp(option, "translate")){
    mask   = KATCP_FLAT_RETAINFO;
  } else if(!strcmp(option, "native")){
    set    = KATCP_FLAT_RETAINFO;
  } else if(!strcmp(option, "log-prefix")){
    set    = KATCP_FLAT_LOGPREFIX;
  } else if(!strcmp(option, "log-plain") || !strcmp(option, "log-unchanged")){
    mask   = KATCP_FLAT_LOGPREFIX;
#if 0
  } else if(!strcmp(option, "map-fallback")){
    mask   = KATCP_FLAT_RUNMAPTOO;
  } else if(!strcmp(option, "map-always")){
    set    = KATCP_FLAT_RUNMAPTOO;
#endif

  } else if(!strcmp(option, "permit-nul")){
    set    = KATCP_FLAT_PERMITNUL;
  } else if(!strcmp(option, "fill-nul")){
    mask   = KATCP_FLAT_PERMITNUL;

  } else if(!strcmp(option, "version-prepend") || !strcmp(option, "version-prefix")){
    set    = KATCP_FLAT_PREPEND;
  } else if(!strcmp(option, "version-unchanged")){
    mask   = KATCP_FLAT_PREPEND;

  } else if(!strcmp(option, "info-none")){
    mask   = KATCP_FLAT_SEESKATCP | KATCP_FLAT_SEESADMIN | KATCP_FLAT_SEESUSER | KATCP_FLAT_SEESMAPINFO;
  } else if(!strcmp(option, "info-katcp")){
    set    = KATCP_FLAT_SEESKATCP;
    mask   = KATCP_FLAT_SEESADMIN | KATCP_FLAT_SEESUSER;
  } else if(!strcmp(option, "info-user")){
    set    = KATCP_FLAT_SEESKATCP | KATCP_FLAT_SEESUSER;
    mask   = KATCP_FLAT_SEESADMIN;
  } else if(!strcmp(option, "info-admin")){
    set    = KATCP_FLAT_SEESKATCP | KATCP_FLAT_SEESADMIN;
    mask   = KATCP_FLAT_SEESUSER;
  } else if(!strcmp(option, "info-all")){
    set    = KATCP_FLAT_SEESKATCP | KATCP_FLAT_SEESADMIN | KATCP_FLAT_SEESUSER | KATCP_FLAT_SEESMAPINFO;

  } else if(!strcmp(option, "extra-relay")){
    set    = KATCP_FLAT_SEESMAPINFO;
  } else if(!strcmp(option, "extra-drop")){
    mask   = KATCP_FLAT_SEESMAPINFO;
  } else {
    /* WARNING: does not error out in an effort to be forward compatible */
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unknown configuration option %s", option);
    return KATCP_RESULT_OK;
  }

  if(reconfigure_flat_katcp(d, fx, (fx->f_flags & (~mask)) | set) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to change flags on client %s", fx->f_name);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int tcp_config_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *opt, *optval_str, *client;
  char *ptr, *end;
  struct katcp_flat *fx, *fy;
  int optval, fd;
  socklen_t optlen = sizeof(int);

  optval = 0;
  optval_str = client = NULL;
  opt = ptr = end = NULL;

#if !defined(TCP_KEEPCNT)
  log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "tcp configuration parameter TCP_KEEPCNT is not supported");
  return KATCP_RESULT_OK;
#endif

#if !defined(TCP_KEEPIDLE)
  log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "tcp configuration parameter TCP_KEEPIDLE is not supported");
  return KATCP_RESULT_OK;
#endif

#if !defined(TCP_KEEPINTVL)
  log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "tcp configuration parameter TCP_KEEPINTVL is not supported");
  return KATCP_RESULT_OK;
#endif

  fx = this_flat_katcp(d);
  if (fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client scope available");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if (argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a tcp option");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  /* option */
  opt = arg_string_katcp(d, 1);
  if(opt == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire a tcp option");
    return KATCP_RESULT_FAIL;
  }

  /* check for settings */
  if (strcmp(opt, "info") == 0){
    if (argc > 2){
      fy = scope_name_full_katcp(d, NULL, NULL, arg_string_katcp(d, 2));
      if (fy == NULL){ 
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate client %s", client);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      }
      fx = fy;
    } 
    fd = fileno_katcl(fx->f_line);
    if(getsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, &optlen) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to get SO_KEEPALIVE %s", strerror(errno));
      return KATCP_RESULT_FAIL;
    }
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "SO_KEEPALIVE is %s", optval ? "enabled" : "disabled");

    if(getsockopt(fd, SOL_TCP, TCP_KEEPIDLE, &optval, &optlen) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to get TCP_KEEPIDLE %s", strerror(errno));
      return KATCP_RESULT_FAIL;
    }
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "TCP_KEEPIDLE is set to %d seconds", optval);

    if(getsockopt(fd, SOL_TCP, TCP_KEEPCNT, &optval, &optlen) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to get TCP_KEEPCNT %s", strerror(errno));
      return KATCP_RESULT_FAIL;
    }
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "TCP_KEEPCNT is set to %d probes", optval);

    if(getsockopt(fd, SOL_TCP, TCP_KEEPINTVL, &optval, &optlen) < 0){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to get TCP_KEEPINTVL %s", strerror(errno));
      return KATCP_RESULT_FAIL;
    }
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "TCP_KEEPINTVL is set to %d seconds", optval);

    return KATCP_RESULT_OK;
  }

  if (argc > 2){
    /* option argument */
    ptr = optval_str = arg_string_katcp(d, 2);
    strtoul(ptr, &end, 10);
    if(optval_str == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to get tcp option value");
      return KATCP_RESULT_FAIL;
    } else if (strcmp(optval_str, "default") == 0){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "using default setting");
      optval = 0;
    } else if ((*ptr != '\0') && (*end == '\0')){
      /* value argument */
      optval = arg_unsigned_long_katcp(d, 2);
    } else if (argc < 4){
      /* value argument assumed to be client argument */
      client = arg_string_katcp(d, 2);
      fy = scope_name_full_katcp(d, NULL, NULL, client); 
      if(fy == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate client %s", client);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      } else {
        fx = fy;
        log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "using default setting");
      }
    }

    if (argc > 3){
      client = arg_string_katcp(d, 3);
      fy = scope_name_full_katcp(d, NULL, NULL, client); 
      if(fy == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate client %s", client);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      }
      fx = fy;
    }

    fd = fileno_katcl(fx->f_line);
    if (strcmp(opt, "idle") == 0){
      optval = optval ? optval : DEFAULT_KEEPIDLE;
      if (setsockopt(fd, SOL_TCP, TCP_KEEPIDLE, (void *)&optval, sizeof(optval))) {
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to set TCP_KEEPIDLE to %d seconds %s", optval, strerror(errno));
        return KATCP_RESULT_FAIL;
      }
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "TCP_KEEPIDLE set to %d seconds", optval);
    } else if (strcmp(opt, "cnt") == 0){
      optval = optval ? optval : DEFAULT_KEEPCNT;
      if (setsockopt(fd, SOL_TCP, TCP_KEEPCNT, (void *)&optval, sizeof(optval))) {
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to set TCP_KEEPCNT to %d probes %s", optval, strerror(errno));
        return KATCP_RESULT_FAIL;
      }
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "TCP_KEEPCNT set to %d probes", optval);
    } else if (strcmp(opt, "intvl") == 0){
      optval = optval ? optval : DEFAULT_KEEPINTVL;
      if (setsockopt(fd, SOL_TCP, TCP_KEEPINTVL, (void *)&optval, sizeof(optval))) {
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to set TCP_KEEPINTVL to %d seconds %s", optval, strerror(errno));
        return KATCP_RESULT_FAIL;
      }
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "TCP_KEEPINTVL set to %d seconds", optval);
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "invalid tcp-config option");
      return KATCP_RESULT_FAIL;
    }
    /* enable SO_KEEPALIVE */
    if(setsockopt(fd, SOL_SOCKET, SO_KEEPALIVE, &optval, sizeof(optval))) {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to set SO_KEEPALIVE to %d %s", optval, strerror(errno));
      return KATCP_RESULT_FAIL;
    }
    log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "SO_KEEPALIVE is enabled");
  }

  return KATCP_RESULT_OK;
}

int client_halt_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *client, *group;
  struct katcp_flat *fx;

  client = NULL;
  group = NULL;

  if(argc > 1){
    client = arg_string_katcp(d, 1);
    if(client == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
    if(argc > 2){
      group = arg_string_katcp(d, 2);
      if(group == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
      }
    }
  }

  if(client){
    fx = scope_name_full_katcp(d, NULL, group, client);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client with name %s", client);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }
  } else {
    fx = require_flat_katcp(d);
  }

  if(terminate_flat_katcp(d, fx) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to terminate client %s", fx->f_name ? fx->f_name : "without a name");
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int client_switch_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *client, *group;
  struct katcp_flat *fx;
  struct katcp_group *gx;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  group = arg_string_katcp(d, 1);
  if(group == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  gx = find_group_katcp(d, group);
  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group with name %s", group);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  if(argc > 2){
    client = arg_string_katcp(d, 2);
    if(client == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire client name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
    fx = scope_name_full_katcp(d, NULL, NULL, client);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client with name %s", client);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }
  } else {
    client = NULL;
    fx = this_flat_katcp(d);
    if(fx == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
  }

  if(gx == fx->f_group){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "client already in group %s", group);
    return KATCP_RESULT_OK;
  }

  if(switch_group_katcp(d, fx, gx) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to transfer client to group %s", group);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  return KATCP_RESULT_OK;
}

int client_rename_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *from, *to, *group, *old;
  struct katcp_flat *fx;

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  to = arg_string_katcp(d, 1);
  if(to == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire new name");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc > 2){
    from = arg_string_katcp(d, 2);
    if(from == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire previous name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
  } else {
    fx = require_flat_katcp(d);
    if(fx == NULL){
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
    }
    from = fx->f_name;
  }

  if(argc > 3){
    group = arg_string_katcp(d, 3);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
  } else {
    group = NULL;
  }

  if(from == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "this client does not have a valid current name and cannot proceed");
    /* TODO: could do a find and check if any sensors pending - if not this test is too general */
    return KATCP_RESULT_FAIL;
  }

  old = strdup(from);
  if (old == NULL){
    /* non-critical failure but need to handle error case for later usage*/
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "allocation failure (non-critical)");
  }

  /* WARNING: commited to freeing old from here onwards */

  if(rename_flat_katcp(d, group, from, to) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to rename from %s to %s within %s group", from, to, group ? group : "any");
    if(old){
      free(old);
    }
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "instance previously called %s now is %s", old ? old : "<unknown>", to);

  /* TODO: find and rename all sensor subscription timers linked to this flat instance, or maybe cancel them if this isn't possible */

  if(old != NULL){
    free(old);
  }

  return KATCP_RESULT_OK;
}

/* group related commands *********************************************************/

int group_create_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *group, *tmp;
  struct katcp_group *go, *gx;
  struct katcp_shared *s;
  int depth, i;

  s = d->d_shared;
  depth = 1;

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a new group name");
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a group name");
    return KATCP_RESULT_FAIL;
  }

  gx = find_group_katcp(d, name);
  if(gx != NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group %s already exists (%u members, %u users, autoremove=%u)", name, gx->g_count, gx->g_use, gx->g_autoremove);
    return KATCP_RESULT_FAIL;
  }

  group = arg_string_katcp(d, 2);
  if(group == NULL){
    go = s->s_fallback;
  } else {
    go = find_group_katcp(d, group);
    if(go == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "template group %s not found", group);
      return KATCP_RESULT_FAIL;
    }
  }

  for(i = 3; i < argc; i++){
    tmp = arg_string_katcp(d, i);
    if(tmp){
      if(!strcmp(tmp, "linked")){
        depth = 0;
      }
    }
  }

  gx = duplicate_group_katcp(d, go, name, depth);
  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to duplicate group as %s", name);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int group_halt_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_group *gx;
  char *name;

  if(argc <= 1){
    gx = this_group_katcp(d);
    if(gx){
      if(terminate_group_katcp(d, gx, 1) == 0){
        return KATCP_RESULT_OK;
      } else {
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to terminate %s", gx->g_name);
      }
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group available in this context");
    }
    return KATCP_RESULT_FAIL;
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to retrieve group name");
    return KATCP_RESULT_FAIL;
  }

  gx = find_group_katcp(d, name);
  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group %s found", name);
    return KATCP_RESULT_FAIL;
  }

  if(terminate_group_katcp(d, gx, 1) != 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to terminate group %s", name);
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int group_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_group *gx;
  struct katcp_shared *s;
  struct katcp_cmd_map *mx;
  int j, count, i;
  char *ptr, *group, *name;

  s = d->d_shared;

  count = 0;

  if(argc >= 2){
    name = arg_string_katcp(d, 1);
    if(name == NULL){
      return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
    }
  } else {
    name = NULL;
  }

  for(j = 0; j < s->s_members; j++){
    gx = s->s_groups[j];

    if(gx->g_name){
      group = gx->g_name;
      if(name && strcmp(group, name)){
        /* WARNING */
        continue;
      }
    } else {
      group = "<anonymous>";
      if(name){
        /* WARNING */
        continue;
      }
    }

    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s has %d references", group, gx->g_use);
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s has %u members", group, gx->g_count);
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s will %s if not used", group, gx->g_autoremove ? "disappear" : "persist");


    if(gx->g_flags & KATCP_GROUP_OVERRIDE_MISC){
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces %s of null sensor fields", group, (gx->g_flags & KATCP_FLAT_PERMITNUL) ? "acceptance" : "filling");
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s sets new clients to be %s", group, (gx->g_flags & KATCP_FLAT_HIDDEN) ? "hidden" : "visible");
    } else {
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s delegates miscellaneous configuration fields to client creation context", group);
    }

    if(gx->g_flags & KATCP_GROUP_OVERRIDE_BROADCAST){
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces %s of katcp admin messages", group, (gx->g_flags & KATCP_FLAT_SEESKATCP) ? "sending" : "inhibition");
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces %s of extra admin messages", group, (gx->g_flags & KATCP_FLAT_SEESADMIN) ? "sending" : "inhibition");
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces %s of user messages", group, (gx->g_flags & KATCP_FLAT_SEESUSER) ? "sending" : "inhibition");

      /* SEESMAPINFO ? */
    } else {
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s delegates broadcast messsage sending policy client creation context", group);
    }

    if(gx->g_flags & KATCP_GROUP_OVERRIDE_RELAYING){
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces relayed informs to be %s", group, (gx->g_flags & KATCP_FLAT_RETAINFO) ? "retained" : "rewritten");
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces members %s relay inform messages", group, (gx->g_flags & KATCP_FLAT_INSTALLINFO) ? "to" : "to not");
    } else {
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s delegates relay rewriting decision to client creation context", group);
    }

    if(gx->g_flags & KATCP_GROUP_OVERRIDE_PREFIXING){
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces members %s prefix their name to sensor definitions", group, (gx->g_flags & KATCP_FLAT_PREFIXED) ? "to" : "to not");
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces versions labels to be %s with member name", group, (gx->g_flags & KATCP_FLAT_PREPEND) ? "prepended" : "unchanged");
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s forces members %s prefix their name to log inform messages", group, (gx->g_flags & KATCP_FLAT_LOGPREFIX) ? "to" : "not to");
    } else {
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s delegates prefixing and prepending decision to client creation context", group);
    }

    ptr = log_to_string_katcl(gx->g_log_level);
    if(ptr){
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s sets members log level to %s", group, ptr);
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group %s has unreasonable log level", group);
    }

    ptr = string_from_scope_katcp(gx->g_scope);
    if(ptr){
      log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s sets %s client scope", group, ptr);
    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group %s has invalid scope setting", group);
    }

    for(i = 0; i < KATCP_SIZE_MAP; i++){
      mx = gx->g_maps[i];
      if(mx){
        log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "group %s command map %d has %s %s and is referenced %d times", group, i, mx->m_name ? "name" : "no", mx->m_name ? mx->m_name : "name", mx->m_refs);
      }
    }

    if(gx->g_name){
      prepend_inform_katcp(d);
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, gx->g_name);
      count++;
    }
  }

  if((count == 0) && (name != NULL)){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%d", count);
}

int group_config_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;
  struct katcp_group *gx;
#if 0
  struct katcp_shared *s;
#endif
  char *option, *group;
  unsigned int mask, set;

#if 0
  s = d->d_shared;
#endif

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no client scope available");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "valid configuration values in clude all-prefix all-unchanged sensor-prefix log-prefix version-prefix delegates permit-nul fill-nul");
    return KATCP_RESULT_FAIL;
  }

  option = arg_string_katcp(d, 1);
  if(option == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire a flag");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  if(argc > 2){
    group = arg_string_katcp(d, 2);
    if(group == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to acquire group name");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
    }
    gx = scope_name_group_katcp(d, group, fx);
  } else {
    gx = this_group_katcp(d);
    group = NULL;
  }

  if(gx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate group %s", group ? group : "of current client");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  set  = 0;
  mask = 0;

  if(!strcmp(option, "all-prefix")){        /* prefix names */
    set    = KATCP_GROUP_OVERRIDE_PREFIXING | (KATCP_FLAT_LOGPREFIX | KATCP_FLAT_PREFIXED | KATCP_FLAT_PREPEND);
  } else if(!strcmp(option, "all-unchanged")){    /* make names absolute */
    set    = KATCP_GROUP_OVERRIDE_PREFIXING;
    mask   = (KATCP_FLAT_LOGPREFIX | KATCP_FLAT_PREFIXED | KATCP_FLAT_PREPEND);
  } else if(!strcmp(option, "sensor-prefix")){    /* make names absolute */
    set    = KATCP_GROUP_OVERRIDE_PREFIXING | (KATCP_FLAT_PREFIXED);
  } else if(!strcmp(option, "log-prefix")){    /* make names absolute */
    set    = KATCP_GROUP_OVERRIDE_PREFIXING | (KATCP_FLAT_LOGPREFIX);
  } else if(!strcmp(option, "version-prefix")){    /* make names absolute */
    set    = KATCP_GROUP_OVERRIDE_PREFIXING | (KATCP_FLAT_PREPEND);
  } else if(!strcmp(option, "delegates")){ /* pick whatever the calling logic prefers */
    mask   = KATCP_GROUP_OVERRIDE_MISC | KATCP_GROUP_OVERRIDE_BROADCAST | KATCP_GROUP_OVERRIDE_RELAYING | KATCP_GROUP_OVERRIDE_PREFIXING | KATCP_FLAT_CONFIGMASK;
  } else if(!strcmp(option, "permit-nul")){
    set    = KATCP_GROUP_OVERRIDE_MISC | KATCP_FLAT_PERMITNUL;
  } else if(!strcmp(option, "fill-nul")){
    set    = KATCP_GROUP_OVERRIDE_MISC;
    mask   = KATCP_FLAT_PERMITNUL;
  } else { /* TODO: broadcast and relayinfo overrides */
    /* WARNING: does not error out in an effort to be forward compatible */
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown configuration option %s", option);
    return KATCP_RESULT_OK;
  }

  gx->g_flags = (gx->g_flags & (~mask)) | set;

  return KATCP_RESULT_OK;
}

/* listener related commands ******************************************************/

int listener_create_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *group, *address;
  unsigned int port, fixup;
  struct katcp_group *gx;
  struct katcp_shared *s;

  s = d->d_shared;

#ifdef KATCP_CONSISTENCY_CHECKS
  if(s->s_fallback == NULL){
    fprintf(stderr, "listen: expected an initialised fallback group\n");
    abort();
  }
#endif

  if(argc < 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a label");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  if(find_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER)){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "listener with name %s already exists", name);
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  port = 0;
  address = NULL;
  group = NULL;

  if(argc > 2){
    port = arg_unsigned_long_katcp(d, 2);

    fixup = net_port_fixup(port);
    if(fixup != port){
      log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "truncating unreasonable port %u to %u", port, fixup);
      port = fixup;
    }

    if(argc > 3){
      address = arg_string_katcp(d, 3);
      if(argc > 4){
        group = arg_string_katcp(d, 4);
      }
    }
  }

  if(group){
    gx = find_group_katcp(d, group);
    if(gx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no group with name %s found", group);
      return KATCP_RESULT_FAIL;
    }
  } else {
    gx = find_group_katcp(d, name);
    if(gx == NULL){
      gx = s->s_fallback;
      if(gx == NULL){
        log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no default group available");
        return KATCP_RESULT_FAIL;
      }
    }
  }

  if(create_listen_flat_katcp(d, name, port, address, gx) == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to listen on %s: %s", name, strerror(errno));
    return KATCP_RESULT_FAIL;
  }

  return KATCP_RESULT_OK;
}

int listener_halt_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name;

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  if(destroy_listen_flat_katcp(d, name) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to destroy listener instance %s which might not even exist", name);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "halted listener %s", name);

  return KATCP_RESULT_OK;
}

int listener_config_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *value;
  struct katcp_listener *kl;
  struct katcp_arb *a;
  int opts, fd;

  if(argc <= 2){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a listener to configure");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  name = arg_string_katcp(d, 1);
  value = arg_string_katcp(d, 2);
  if((name == NULL) || (value == NULL)){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  a = find_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER);
  if(a == NULL){
    log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "no listener %s found", name);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
  }

  kl = data_arb_katcp(d, a);
  if(kl == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  if(!strcmp(value, "nagle")){
    kl->l_options &= ~KATCP_LISTEN_NO_NAGLE;
  } else if(!strcmp(value, "fast")){
    kl->l_options |=  KATCP_LISTEN_NO_NAGLE;
  } else {
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unsupported option %s", value);
  }

#ifdef TCP_NODELAY
  opts = (kl->l_options & KATCP_LISTEN_NO_NAGLE) ? 1 : 0;
  fd = fileno_arb_katcp(d, a);
  if(setsockopt(fd, IPPROTO_TCP, TCP_NODELAY, &opts, sizeof(opts)) < 0){
    log_message_katcp(d, KATCP_LEVEL_WARN, NULL, "unable to update nagle socket option: %s", strerror(errno));
  }
#endif

  return KATCP_RESULT_OK;
}

int print_listener_katcp(struct katcp_dispatch *d, struct katcp_arb *a, void *data)
{
  char *name, *extra;
  struct katcp_listener *kl;
  struct katcp_group *gx;

  name = name_arb_katcp(d, a);
  if(name == NULL){
    return -1;
  }

  kl = data_arb_katcp(d, a);
  if(kl == NULL){
    return -1;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL , NULL, "listener %s %s nagles algorithm", name, (kl->l_options & KATCP_LISTEN_NO_NAGLE) ? "does not use" : "uses");

  prepend_inform_katcp(d);

  extra = NULL;

  if(kl->l_group){
    gx = kl->l_group;
    if(gx->g_name && strcmp(gx->g_name, name)){
      extra = gx->g_name;
    }
  }

  append_string_katcp(d, KATCP_FLAG_STRING, name);

  if(extra || kl->l_address){
    append_unsigned_long_katcp(d, KATCP_FLAG_ULONG, kl->l_port);
    append_string_katcp(d, KATCP_FLAG_STRING | (extra ? 0 : KATCP_FLAG_LAST), kl->l_address ? kl->l_address : "0.0.0.0");
    if(extra){
      append_string_katcp(d, KATCP_FLAG_STRING | KATCP_FLAG_LAST, extra);
    }
  } else {
    append_unsigned_long_katcp(d, KATCP_FLAG_ULONG | KATCP_FLAG_LAST, kl->l_port);
  }

  return 0;
}

int listener_list_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name;
  int count;
  struct katcp_arb *a;

  name = NULL;

  if(argc > 1){
    name = arg_string_katcp(d, 1);
    if(name == NULL){
      return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
    }

    /* a bit too "close" to the internals ... there should be find listener function */
    a = find_type_arb_katcp(d, name, KATCP_ARB_TYPE_LISTENER);
    if(a == NULL){
      log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "no listener %s found", name);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
    }

    if(print_listener_katcp(d, a, NULL) < 0){
      count = (-1);
    } else {
      count = 0;
    }
  } else {
    count = foreach_arb_katcp(d, KATCP_ARB_TYPE_LISTENER, &print_listener_katcp, NULL);
  }

  if(count < 0){
    return KATCP_RESULT_FAIL;
  }

  return extra_response_katcp(d, KATCP_RESULT_OK, "%d", count);
}

/* command/map related commands ***************************************************/

#define CMD_OP_REMOVE  0
#define CMD_OP_FLAG    1
#define CMD_OP_HELP    2

static int configure_cmd_group_katcp(struct katcp_dispatch *d, int argc, int op, unsigned int flags)
{
  struct katcp_cmd_map *mx;
  struct katcp_cmd_item *ix;
  struct katcp_flat *fx;
  char *name, *help;

  if(argc <= 1){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  name = arg_string_katcp(d, 1);
  if(name == NULL){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  fx = this_flat_katcp(d);
  if(fx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "interesting failure: not called within a valid duplex context\n");
    abort();
#endif
    return KATCP_RESULT_FAIL;
  }

  mx = map_of_flat_katcp(fx);
  if(mx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "interesting failure: no map available\n");
    abort();
#endif
    return KATCP_RESULT_FAIL;
  }

  switch(op){
    case CMD_OP_REMOVE :
      if(remove_cmd_map_katcp(mx, name) < 0){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "removal of command %s failed", name);
        return KATCP_RESULT_FAIL;
      } else {
        return KATCP_RESULT_OK;
      }
      break;
    case CMD_OP_HELP :
      ix = find_cmd_map_katcp(mx, name);
      if(ix == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no command %s found", name);
        return KATCP_RESULT_FAIL;
      }

      help = arg_string_katcp(d, 2);
      if(help == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a valid help string for %s", name);
        return KATCP_RESULT_FAIL;
      }

      if(set_help_cmd_item_katcp(ix, help) < 0){
        return KATCP_RESULT_FAIL;
      }

      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "updated help message for %s held %d times", name, ix->i_refs);

      return KATCP_RESULT_OK;
    case CMD_OP_FLAG :
      ix = find_cmd_map_katcp(mx, name);
      if(ix == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no command %s found", name);
        return KATCP_RESULT_FAIL;
      }

      set_flag_cmd_item_katcp(ix, flags);

      log_message_katcp(d, KATCP_LEVEL_DEBUG, NULL, "change on command used %d times", ix->i_refs);

      return KATCP_RESULT_OK;
    default :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "internal usage problem while configuring a command");
      return KATCP_RESULT_FAIL;
  }

}

int hide_cmd_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return configure_cmd_group_katcp(d, argc, CMD_OP_FLAG, KATCP_MAP_FLAG_HIDDEN);
}

int uncover_cmd_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return configure_cmd_group_katcp(d, argc, CMD_OP_FLAG, 0);
}

int delete_cmd_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  return configure_cmd_group_katcp(d, argc, CMD_OP_REMOVE, 0);
}

int help_cmd_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{

  return configure_cmd_group_katcp(d, argc, CMD_OP_HELP, 0);
}

int alias_cmd_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *source, *target;
  struct katcp_cmd_map *mx;
  struct katcp_cmd_item *ix;
  struct katcp_flat *fx;

  if(argc <= 2){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_USAGE);
  }

  target = arg_string_katcp(d, 1);
  source = arg_string_katcp(d, 2);
  if((source == NULL) || (target == NULL)){
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  fx = this_flat_katcp(d);
  if(fx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "interesting failure: not called within a valid duplex context\n");
    abort();
#endif
    return KATCP_RESULT_FAIL;
  }

  mx = map_of_flat_katcp(fx);
  if(mx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "interesting failure: no map available\n");
    abort();
#endif
    return KATCP_RESULT_FAIL;
  }

  ix = find_cmd_map_katcp(mx, source);
  if(ix == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no command %s found", source);
    return KATCP_RESULT_FAIL;
  }

  if(ix->i_data){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "command %s has local state which makes it unsafe to clone", source);
    return KATCP_RESULT_FAIL;
  }

  if(add_full_cmd_map_katcp(mx, target, ix->i_help, ix->i_flags, ix->i_call, NULL, ix->i_clear) < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to setup an alias for %s as %s", source, target);
    return KATCP_RESULT_FAIL;
  }

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "set up an alias for %s as %s", source, target);
  return KATCP_RESULT_OK;

}
/* scope commands ***/

int scope_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *name, *ptr;
  struct katcp_flat *fx;
  struct katcp_group *gx;
  int scope;

  if(argc <= 1){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient parameters");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  ptr = arg_string_katcp(d, 1);
  if(ptr == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "null parameters");
    return extra_response_katcp(d, KATCP_RESULT_INVALID, KATCP_FAIL_BUG);
  }

  scope = code_from_scope_katcp(ptr);
  if(scope < 0){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown scope %s", ptr);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  /* WARNING: the *_scope variables should probably be changed using accessor functions, but not used sufficiently often */

  if(argc > 3){
    ptr = arg_string_katcp(d, 2);
    name = arg_string_katcp(d, 3);

    if((ptr == NULL) || (name == NULL)){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "insufficient or null parameters");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }

    if(!strcmp(ptr, "client")){
      fx = scope_name_full_katcp(d, NULL, NULL, name);
      if(fx == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "client %s not found", name);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      }

      fx->f_scope = scope;

    } else if(!strcmp(ptr, "group")){
      gx = find_group_katcp(d, name);
      if(gx == NULL){
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "group %s not found", name);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      }

      gx->g_scope = scope;

    } else {
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unknown scope extent %s", ptr);
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    }

  } else {
    fx = this_flat_katcp(d);
    if(fx == NULL){
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "no current client available");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_API);
    }

    fx->f_scope = scope;
  }

  return KATCP_RESULT_OK;
}

/*********************************************************************************/

int broadcast_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  char *inform, *group;
  struct katcl_parse *px, *py;
  struct katcp_flat *fx;
  struct katcp_group *gx;
  char *ptr;

  py = arg_parse_katcp(d);
  if(py == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "interesting internal problem: unable to acquire current parser message");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }

  fx = this_flat_katcp(d);
  if(fx == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "logic problem: broadcast not run in flat scope");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_BUG);
  }


  if(argc < 3){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a group and a message");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  group = arg_string_katcp(d, 1);
  if(group){

    gx = scope_name_group_katcp(d, group, fx);
    if(gx == NULL){
      if(strcmp(group, "*")){ /* WARNING: made up syntax - needs to be checked across everything */
        log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "unable to locate group called %s", group);
        return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_NOT_FOUND);
      }
    }

    /* TODO: should check scope - if not global, restrict to current one ... */

  } else {
    /* group is null, assume everybody */
    switch(fx->f_scope){
      case KATCP_SCOPE_GROUP :
        gx = this_group_katcp(d);
        break;
      case KATCP_SCOPE_ALL :
        gx = NULL;
        break;
      /* case KATCP_SCOPE_LOCAL :  */
      default :
        /* TODO: should spam ourselves ... in order not to give away that we are restricted */
        gx = NULL;
        return KATCP_RESULT_OK;
    }
  }

  inform = arg_string_katcp(d, 2);
  if(inform == NULL){
    log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "need a non-null inform");
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
  }

  switch(inform[0]){
    case KATCP_REPLY :
    case KATCP_REQUEST :
      log_message_katcp(d, KATCP_LEVEL_ERROR, NULL, "refusing to broadcast a message which is not an inform");
      return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_USAGE);
    /* case KATCP_INFORM : */
    default :
      break;
  }

  ptr = default_message_type_katcm(inform, KATCP_INFORM);
  if(ptr == NULL){
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  px = create_referenced_parse_katcl();
  if(px == NULL){
    free(ptr);
    return extra_response_katcp(d, KATCP_RESULT_FAIL, KATCP_FAIL_MALLOC);
  }

  if(argc > 3){
    add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, ptr);

    add_trailing_parse_katcl(px, KATCP_FLAG_LAST, py, 3);

  } else {
    add_string_parse_katcl(px, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING, ptr);
  }

  free(ptr);
  ptr = NULL;

  if(broadcast_flat_katcp(d, gx, px, NULL, NULL) < 0){
    destroy_parse_katcl(px);
    return KATCP_RESULT_FAIL;
  }

  destroy_parse_katcl(px);
  return KATCP_RESULT_OK;
}

/* system information *************************************************************/

int system_info_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
#define BUFFER 64
  struct katcp_shared *s;
  time_t now;
  char buffer[BUFFER];
  int size;
  struct heap *th;

  s = d->d_shared;
  if(s == NULL){
    return KATCP_RESULT_FAIL;
  }

  time(&now);
  print_time_delta_katcm(buffer, BUFFER, now - s->s_start);

  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "server has %u groups", s->s_members);
#if KATCP_PROTOCOL_MAJOR_VERSION >= 5
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "server was launched at %lu", s->s_start);
#else
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "server was launched at %lu000", s->s_start);
#endif
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "server has been running for %s", buffer);
#ifdef __DATE__
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "server was built on %s at %s", __DATE__, __TIME__);
  log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "server has currently %u parse messages allocated", allocated_parses_katcl());
#endif

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d active %s", s->s_lcount, (s->s_lcount == 1) ? "listener" : "listeners");

  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d active %s", s->s_epcount, (s->s_epcount == 1) ? "endpoint" : "endpoints");

#ifdef KATCP_HEAP_TIMERS
  th = s->s_tmr_heap;
  size = get_size_of_heap(th);
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d %s in heap priority queue", size, size == 1 ? "timer" : "timers");
#else
  log_message_katcp(d, KATCP_LEVEL_INFO, NULL, "%d %s scheduled", s->s_length, (s->s_length == 1) ? "timer" : "timers");
#endif

#undef BUFFER
  return KATCP_RESULT_OK;
}

/* identify current connection - several telnet localhost connections end up ambiguous */

int whoami_group_cmd_katcp(struct katcp_dispatch *d, int argc)
{
  struct katcp_flat *fx;

  fx = this_flat_katcp(d);
  if (fx == NULL){
#ifdef KATCP_CONSISTENCY_CHECKS
    fprintf(stderr, "whoami: major logic problem: no handle to current connection\n");
    abort();
#endif
    /* potentially serious error - why don't we have a valid handle on connection */
    log_message_katcp(d, KATCP_LEVEL_FATAL, NULL, "no handle to current connection");
#ifdef DEBUG
    fprintf(stderr, "whoami: null pointer to connection - potentially dangerous\n");
#endif
    return KATCP_RESULT_FAIL;
  }

  /* is the flat named ?*/

  if(fx->f_name == NULL){
    /* flat was created with no name */
    log_message_katcp(d, KATCP_LEVEL_INFO | KATCP_LEVEL_LOCAL, NULL, "current client has no name and will not show up in client list");
    /* probably nicer to have an un-named connection have a nonexistant name */
    return KATCP_RESULT_OK;
  }

  extra_response_katcp(d, KATCP_RESULT_OK, fx->f_name);

  return KATCP_RESULT_OWN;
}

#endif
