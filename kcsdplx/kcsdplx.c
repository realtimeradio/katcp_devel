/* (c) 2015 SKA-SA */
/* Released under the GNU GPLv3 */

#include <stdio.h>
#include <stdlib.h>
#include <sysexits.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>
#include <time.h>

#include <sys/types.h>
#include <sys/stat.h>

#include <katcp.h>
#include <katpriv.h>
#include <fork-parent.h>

#include "kcsdplx.h"

/* function prototypes */
static void usage(char *app);

int main(int argc, char *argv[])
{
  int foreground = 0;
  char *initfile = NULL;
  char *logfile = KCSDPX_LOG;
  char *port = NULL;
  char *script = NULL;
  struct katcp_dispatch *d;
  int option;
  int status;
  int lfd;
  time_t now;
  /* unsigned int p = 0; */

  while ((option = getopt(argc, argv, "vfhi:s:l:p:")) != -1) {
    switch (option) {
      case 'v':
        fprintf(stdout, "%s version %s\n", argv[0], V_GIT);
        return EX_OK;
        break;
      case 'f':
        foreground = 1;
        break;
      case 'h':
        usage(argv[0]);
        return EX_OK;
        break;
      case 'i':
        initfile = optarg;
        break;
      case 's':
        script = optarg;
        break;
      case 'l':
        logfile = optarg;
        break;
      case 'p':
        port = optarg;
        break;
      default:
        usage(argv[0]);
        return EX_USAGE;
    }
  }

  if (!foreground){
    fprintf(stderr,"%s: about to go into background\n", argv[0]);
    if(fork_parent() < 0){
      fprintf(stderr, "%s: unable to fork_parent\n", argv[0]);
      return EX_OSERR;
    }
  }

  /* create a state handle */
  d = startup_katcp();
  if (d == NULL){
    fprintf(stderr, "%s: unable to allocate state\n", argv[0]);
    return EX_SOFTWARE;
  }

#if 0
  if (port){
    errno = 0;
    p = strtol(port, NULL, 10);   /* TODO: error checking on string to int convertion */
  }
#endif

  /* configure the duplex server */
  if (config_server_flat_katcp(d, initfile, script, port) != 0){
    fprintf(stderr, "%s: unable to configure server\n", argv[0]);
    return EX_CONFIG; 
  }

#ifdef DKATCP_CONSISTENCY_CHECKS
  signal(SIGPIPE, SIG_DFL);
#else
  signal(SIGPIPE, SIG_IGN);
#endif


  /* all stderr ouput will be logged to file ; parent closed */
  if (!foreground){
    lfd = open(logfile, O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_NOCTTY, S_IWUSR | S_IRUSR | S_IRGRP | S_IWGRP);
    if (lfd < 0){
      fprintf(stderr,"%s: unable to open %s: %s\n",argv[0], logfile, strerror(errno));
      fflush(stderr);
      return EX_CANTCREAT;
    } else {
      fflush(stderr);
      if (dup2(lfd,STDERR_FILENO) >= 0){
        now = time(NULL);
        fprintf(stderr,"Logging to file started at %s\n",ctime(&now));
      }
      close(lfd);
    }
  }

  /* run the duplex server */
  run_server_flat_katcp(d);

  fprintf(stderr, "%s: server about to end\n", argv[0]);

  status = exited_katcp(d);

  shutdown_katcp(d);

  return status;
}

static void usage(char *app)
{
  fprintf(stdout, "Usage: %s [-hf] [-i init-file] [-p port]\n", app);
  fprintf(stdout, "-h               this help\n");
  fprintf(stdout, "-v               print version information\n");
  fprintf(stdout, "-f               run in foreground (default is background/daemon)\n");
  fprintf(stdout, "-i init-file     load <init-file> containing katcp commands at startup\n");
  fprintf(stdout, "-s exe           execute script/binary <exe> at startup\n");
  fprintf(stdout, "                 pipe connects stdout/stdin of <exe> to kcsdplxd)\n");
  fprintf(stdout, "                 PATH variable searched if no absolute path given\n");
  fprintf(stdout, "-l log-file      log output to <log-file>\n");
  fprintf(stdout, "-p [host:]port   create listener on a given [host:]port e.g. -p localhost:10000 OR -p 10000\n");
}
