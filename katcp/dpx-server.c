#include <stdio.h>
#include <unistd.h>
#include <time.h>

#include <katpriv.h>
#include <katcp.h>

int config_server_flat_katcp(struct katcp_dispatch *dl, char *configfile, char *exe, char *host_port){
  /*host_port in format 'host:port' or simply 'port' alone*/
  int fd;
  struct katcp_flat *ff;
  struct katcp_flat *fx;
  struct katcp_arb *l;
  char *vector[2] = {NULL, NULL};

  if(configfile){
    fd = pipe_from_file_katcp(dl, configfile);
    if (fd < 0){
#ifdef DEBUG
      fprintf(stderr, "config: creation of pipe from file failed\n");
#endif
      return terminate_katcp(dl, KATCP_EXIT_ABORT);
    }
    ff = create_flat_katcp(dl, fd, 0, NULL, NULL);
    if (ff == NULL){
#ifdef DEBUG
      fprintf(stderr, "config: pipe from file - unable to create flat data structure\n");
#endif
      close(fd);
      return terminate_katcp(dl, KATCP_EXIT_ABORT);
    }
  }

  if (exe){
    vector[0] = exe;
    fx = create_exec_flat_katcp(dl, KATCP_FLAT_TOSERVER, NULL, NULL, vector);
    if (fx == NULL){
#ifdef DEBUG
      fprintf(stderr, "config: pipe from exe - unable to create flat data structure\n");
#endif
      return terminate_katcp(dl, KATCP_EXIT_ABORT);
    }
  }

  if(host_port){
    l = create_listen_flat_katcp(dl, "init_listen", 0, host_port, NULL);
    if(l == NULL){
#ifdef DEBUG
      fprintf(stderr, "config: unable to create listener on %s\n", host_port);
#endif
      return terminate_katcp(dl, KATCP_EXIT_ABORT);
    }
  }

  return 0;
}


int run_server_flat_katcp(struct katcp_dispatch *dl){
  struct katcp_shared *s;

  s = dl->d_shared;

  time(&(s->s_start));

  return run_core_loop_katcp(dl);
}


#define TEMP_BUFFER_SIZE  21

int run_config_server_flat_katcp(struct katcp_dispatch *dl, char *configfile, char *exe, char *host, int port){
  char buffer[TEMP_BUFFER_SIZE] = {'\0'};
  char *host_port;
  int ret;

  /* if port number given in args -> assume hostname and port seperate arguments */
  if (port){
    snprintf(buffer, TEMP_BUFFER_SIZE, "%s:%d", host, port);
    buffer[TEMP_BUFFER_SIZE - 1] = '\0';
    host_port = buffer;
  } else {
  /* if port number not given in args -> assume arg host has format "host:port" */
    host_port = host;
  }
  ret = config_server_flat_katcp(dl, NULL, NULL, host_port);
  if (ret){
    return ret;
  }

  return run_server_flat_katcp(dl);
}

#undef TEMP_BUFFER_SIZE
