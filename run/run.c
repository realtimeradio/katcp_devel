#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <sysexits.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>

#include <fcntl.h>
#include <time.h>
#include <signal.h>

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <sys/wait.h>

#include <katcp.h>
#include <katcl.h>
#include <katpriv.h>
#include <netc.h>

#define DEFAULT_LEVEL "info"
#define IO_INITIAL     1024
#define SOCKET_ATTEMPTS 10

struct totalstate{
  int t_verbose;
  int t_infer;
  char *t_system;
  struct sensor *t_head;
  struct sensor *t_current; /* TODO: need this? */
  unsigned int t_sensor_list;
  unsigned int t_sensor_added;
  unsigned int t_subscribed;
};

struct iostate{
  int i_fd;
  struct katcl_line *i_line;
  unsigned char *i_buffer;
  unsigned int i_size;
  unsigned int i_have;
  unsigned int i_done;
  int i_level;
};

struct sensor{
  char *s_name;
  unsigned int s_subscribed;
  unsigned int s_is_msgbuilt;
  struct sensor *s_next;
};

void destroy_iostate(struct iostate *io)
{
  if(io == NULL){
    return;
  }

  if(io->i_buffer){
    free(io->i_buffer);
    io->i_buffer = NULL;
  }

  if(io->i_line){
    destroy_katcl(io->i_line, 0);
    io->i_line = NULL;
  }

  if(io->i_fd >= 0){
    close(io->i_fd);
    io->i_fd = (-1);
  }

  free(io);
}

struct iostate *create_iostate(int fd, int level)
{
  struct iostate *io;

  io = malloc(sizeof(struct iostate));
  if(io == NULL){
    return NULL;
  }

  io->i_fd = (-1);
  io->i_buffer = NULL;
  io->i_line = NULL;

  if(level < 0){
    io->i_line = create_katcl(fd);
    if(io->i_line == NULL){
      destroy_iostate(io);
      return NULL;
    }
  } else {

    io->i_buffer = malloc(sizeof(unsigned char) * IO_INITIAL);
    if(io->i_buffer == NULL){
      destroy_iostate(io);
      return NULL;
    }

    io->i_size = IO_INITIAL;
  }

  io->i_fd = fd;

  io->i_have = 0;
  io->i_done = 0;
  io->i_level = level;

  return io;
}

int run_iostate(struct totalstate *ts, struct iostate *io, struct katcl_line *k)
{
  int rr, end, fd, sw, pr;
  unsigned char *tmp;
  char *request;
  unsigned int may, i;
  struct katcl_parse *px;

  if(io->i_line != NULL){
    end = read_katcl(io->i_line);

    while((pr = parse_katcl(io->i_line)) > 0){
      px = ready_katcl(io->i_line);
      if(px){
        request = get_string_parse_katcl(px, 0);
        if(request){
          if(!strcmp(request, KATCP_LOG_INFORM)){
            append_parse_katcl(k, px);
          }
        }
      }

      clear_katcl(io->i_line);
    }
  } else {

    if(io->i_have >= io->i_size){
      tmp = realloc(io->i_buffer, sizeof(unsigned char) * io->i_size * 2);
      if(tmp == NULL){
        return -1;
      }

      io->i_buffer = tmp;
      io->i_size *= 2;
    }

    may = io->i_size - io->i_have;
    end = 0;

    rr = read(io->i_fd, io->i_buffer, may);
    if(rr <= 0){
      if(rr < 0){
        switch(errno){
          case EAGAIN :
          case EINTR  :
            return 1;
          default :
            /* what about logging this ... */
            return -1;
        }
      } else {
        io->i_buffer[io->i_have] = '\n';
        io->i_have++;
        end = 1;
      }
    }

    io->i_have += rr;

    for(i = io->i_done; i < io->i_have; i++){
      switch(io->i_buffer[i]){
        case '\r' :
        case '\n' :
          io->i_buffer[i] = '\0';
          if(io->i_done < i){
#ifdef DEBUG
            fprintf(stderr, "considering <%s> (infer=%d)\n", io->i_buffer + io->i_done, ts->t_infer);
#endif
            if((ts->t_infer > 0) && (strncmp((char *)(io->i_buffer + io->i_done), KATCP_LOG_INFORM " ", 5) == 0)){

              if(flushing_katcl(k)){
                while(write_katcl(k) == 0);
              }

              fd = fileno_katcl(k);
              io->i_buffer[i] = '\n';
              sw = i - io->i_done + 1;
              write(fd, io->i_buffer + io->i_done, sw);
#ifdef DEBUG
              fprintf(stderr, "attempting raw relay to fd=%d of %d bytes\n", fd, sw);
#endif
            } else {
              log_message_katcl(k, io->i_level, ts->t_system, "%s", io->i_buffer + io->i_done);
            }
          }
          io->i_done = i + 1;
          break;
        default :
          /* do nothing ... */
          break;
      }
    }

    if(io->i_done < io->i_have){
      if(io->i_done > 0){
        memmove(io->i_buffer, io->i_buffer + io->i_done, io->i_have - io->i_done);
        io->i_have -= io->i_done;
        io->i_done = 0;
      }
    } else {
      io->i_have = 0;
      io->i_done = 0;
    }
  }

  return end ? 1 : 0;
}

void usage(char *app)
{
  printf("kcprun: a wrapper to katcp-ify program output\n\n");
  printf("usage: %s [options] command ...\n", app);
  printf("-h                 this help\n");
  printf("-q                 run quietly\n");
  printf("-v                 increase verbosity\n");
  printf("-p server:port     connect to the server address on the given port\n");
  printf("-e level           specify the level for messages from standard error\n");
  printf("-o level           specify the level for messages from standard output\n");
  printf("-s subsystem       specify the subsystem (overrides KATCP_LABEL)\n");
  printf("-n label[=value]   modify the child process environment\n");
  printf("-l priority        run at lower priority, using nice level\n");
  printf("-i                 inhibit termination of subprocess on eof on standard input\n");
  printf("-j                 do not read from standard input\n");
  printf("-t milliseconds    set a maximum time to run for the given command\n");
  printf("-r                 forward raw #log messages unchanged\n");
  printf("-x                 relay child exit codes\n");
}

static int tweaklevel(int verbose, int level)
{
  int result;

  result = level + (verbose - 1);
  if(result < KATCP_LEVEL_TRACE){
    return KATCP_LEVEL_TRACE;
  } else if(result > KATCP_LEVEL_FATAL){
    return KATCP_LEVEL_FATAL;
  }

  return result;
}

static int collect_child(struct katcl_line *k, char *task, char *system, int verbose, pid_t pid, int timeout)
{
  int status, code, sig, level;
  pid_t result;

  if(timeout > 0){
    alarm(timeout);
  }

  result = waitpid(pid, &status, (timeout > 0) ? 0 : WNOHANG);

  alarm(0);

  if(result == 0){
    /* child still running */
    log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_DEBUG), system, "task %s still running", task);
    return -1;
  }

  if(result < 0){
    switch(errno){
      case EINTR :
        log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_DEBUG), system, "task %s still running after waiting for %d000ms", task, timeout);
        return -1;
      case ENOENT :
      case ECHILD :
        /* no child */
        return 0;
      default :
        /* other problem */
        log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_WARN), system, "unable to collect task %s: %s", task, strerror(errno));
        return 1;
    }
  }

  if(result != pid){
    log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_ERROR), system, "major logic problem: expected pid %u but received %u", pid, result);
    return 1;
  }

  /* now result == pid */

  /* child has gone, collect the return code */
  if(WIFEXITED(status)){
    code = WEXITSTATUS(status);
    if(code > 0){
      log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_WARN), system, "task %s exited with nonzero exit code", task);
    } else {
      log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_INFO), system, "task %s exited normally", task);
    }

    return (code > 0) ? code: 0;
  }

  if(WIFSIGNALED(status)){
    sig = WTERMSIG(status);
    switch(sig){
      case SIGTERM :
      case SIGKILL :
        level = KATCP_LEVEL_INFO;
        break;
      case SIGPIPE :
        level = KATCP_LEVEL_WARN;
        break;
      default :
        level = KATCP_LEVEL_ERROR;
        break;
    }
    log_message_katcl(k, tweaklevel(verbose, level), system, "task %s exited with signal %d", task, sig);

    return 1;
  }

  /* process stopped ? continued ? other weirdness */

  log_message_katcl(k, tweaklevel(verbose, KATCP_LEVEL_WARN), system, "task %s in rather unusual status 0x%x", task, status);
  return 1;
}

volatile int saw_timeout=0;

void handle_timeout(int signal)
{
  saw_timeout++;
}

/**
  * \brief Check if sensor already added in linked list
  * \param l Reference to katcl_line structure
  * \param ts Reference to totalstate structure
  * \param sensor Reference to sensor name
  * \return 0  as success or -1 as failure
  */
int is_sensor_in_list(struct katcl_line *l, struct totalstate *ts, char *sensor)
{
  struct sensor *link;

  if (ts->t_head == NULL){
    sync_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "empty sensor list");
    return -1;
  }
  if (sensor == NULL){
    sync_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "invalid sensor name");
    return -1;
  }

  link = ts->t_head;
  while (link){
    if (!strcmp(link->s_name, sensor)){
      sync_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "sensor %s already in list", sensor);
      return 1;
    }
    link = link->s_next;
  }

  return 0;
}

/**
  * \brief Print list of all sensors
  * \param l Reference to katcl_line strucuture
  * \param ts Reference to totalstate structure
  * \return 0 as success or -1 as failure
  */
int print_list(struct katcl_line *l, struct totalstate *ts)
{
  struct sensor *link;
  int i;

  i = 0;
  if (ts == NULL){
  log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "total state invalid");
    return -1;
  }

  link = ts->t_head;
  while (link != NULL){
    log_message_katcl(l, KATCP_LEVEL_INFO, ts->t_system, "in list:%d: %s", i, link->s_name);
    link = link->s_next;
    i++;
  }

  return 0;
}

/**
 * \brief Create linked list node
 * \param l Reference to katcl_line structure
 * \param sys Reference to application name
 * \param sensor Reference to sensor name
 * \return pointer as success or NULL as failure
 */
struct sensor *create_link(struct katcl_line *l, char *sys, char *sensor)
{
  struct sensor *link;

  link = NULL;

  link = malloc(sizeof(struct sensor));
  if (link == NULL){
    sync_message_katcl(l, KATCP_LEVEL_ERROR, sys, "unable to malloc link for sensor list");
    return link;
  }
  link->s_subscribed = 0;
  link->s_is_msgbuilt = 0;
  link->s_name = strdup(sensor);
  link->s_next = NULL;

  return link;
}

/**
 * \brief Add sensor to linked list
 * \param l Reference to katcl_line structure
 * \param ts Reference to totalstate structure
 * \param sensor Reference to sensor name
 * \return 0 as success or -1 as failure
 */
int add_sensor(struct katcl_line *l, struct totalstate *ts, char *sensor)
{
  struct sensor *temp;

  temp = NULL;

  if (sensor == NULL){
    sync_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "invalid sensor name provided");
    return -1;
  }

  if (ts->t_head == NULL){
    ts->t_head = create_link(l, ts->t_system, sensor);
    if (ts->t_head == NULL){
      sync_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "unable to malloc link for sensor list");
      return -1;
    }
    ts->t_current = ts->t_head;
    ts->t_sensor_added++;

    return 0;
  }

  if (is_sensor_in_list(l, ts, sensor) == 0){
    temp = create_link(l, ts->t_system, sensor);
    if (temp == NULL){
      sync_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "unable to malloc link for sensor list");
      return -1;
    }
    ts->t_current->s_next = temp;
    ts->t_current = temp;

    ts->t_sensor_added++;
  } else {
    return 1;
  }

  return 0;
}

/**
  * \brief Destroy sensors
  * \param ts Reference to totalstate structure
  * \return void
 */
void destroy_sensors(struct totalstate *ts)
{
  struct sensor *link;

  while (ts->t_head != NULL){
    link = ts->t_head;
    ts->t_head = ts->t_head->s_next;
    free(link->s_name);
    free(link);
  }
}

/**
  * \brief Establish connection to server
  * \param l Reference to katcl_line structure
  * \param ts Reference to totalstate structure
  * \param server Reference to server IP:PORT
  * \return
*/
int initiate_connection(struct katcl_line *l, struct totalstate *ts, char *server)
{
  int fd, flags, attempts;

  fd = -1;
  attempts = SOCKET_ATTEMPTS;
  flags = 0;

  if (ts->t_verbose > 0){
    flags = NETC_VERBOSE_ERRORS;
    if (ts->t_verbose > 1){
      flags = NETC_VERBOSE_STATS;
    }
  }

  /* await for child process to initialise server */
  while((attempts-- > 0) && (fd < 0)){
    fd = net_connect(server, 0, flags);
    sleep(1);
  }

  if(fd < 0){
    if(ts->t_verbose > 0){
      log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "unable to initiate connection to %s", server);
    }
  }

  return fd;
}

/**
 * \brief Build sensor subscribe message
 * \param p Reference to katcl_line structure
 * \param l Reference to katcl_line structure
 * \param ts Reference to totalstate structure
 * \return 0 as success or -1 as failure
 */
int subscribe_sensor(struct katcl_line *p, struct katcl_line *l, struct totalstate *ts)
{
  struct sensor *link;
  int loop;

  loop = 1;
  if (ts->t_head == NULL){
    log_message_katcl(l, KATCP_LEVEL_INFO, ts->t_system, "attempt to subscribe empty sensor list");
    return -1;
  }

  link = ts->t_head;
  while (loop && (link != NULL)){
    if(link->s_subscribed == 0 && (link->s_is_msgbuilt == 0)){
      append_string_katcl(p, KATCP_FLAG_FIRST | KATCP_FLAG_STRING, "?sensor-sampling");
      append_string_katcl(p, KATCP_FLAG_STRING, link->s_name);
      append_string_katcl(p, KATCP_FLAG_STRING | KATCP_FLAG_LAST, "event");
      link->s_is_msgbuilt = 1;
      loop = 0;
    }
    link = link->s_next;
  }

  return 0;
}

/**
  * \brief Check if sensor is subcribed
  * \param l Reference to katcl_line structure
  * \param ts Reference to totalstate structure
  * \return 0 as success or -1 as failure
  */
int check_sensor_subscribe(struct katcl_line *l, struct totalstate *ts)
{
  struct sensor *link;

  if (ts->t_head == NULL){
    log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "invalid sensor list");
    return -1;
  }

  link = ts->t_head;
  while (link != NULL){
    if (!link->s_subscribed){
      sync_message_katcl(l, KATCP_LEVEL_WARN, ts->t_system, "%s not subscribed", link->s_name);
    }
    link = link->s_next;
  }

  return 0;
}

/**
  * \brief Mark sensor as subscribed
  * \param l Reference to katcl_line structure
  * \param ts Reference to totalstate structure
  * \param sensor Reference to sensor name
  * \return 0 as success or -1 as failure
  */
int set_sensor_subscribe(struct katcl_line *l, struct totalstate *ts, char *sensor)
{
  struct sensor *link;
  int loop;

  loop = 1;
  if (ts->t_head == NULL){
    log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "invalid sensor list");
    return -1;
  }

  if (sensor == NULL){
    log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "invalid sensor list");
    return -1;
  }

  link = ts->t_head;
  while (loop && link != NULL){
    if (!strcmp(link->s_name, sensor) && !link->s_subscribed){
      link->s_subscribed = 1;
      log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "%s subscribed", link->s_name);
      ts->t_subscribed++;
      loop = 0;
    } else {
      link = link->s_next;
    }
  }

  return 0;
}

/**
  * \brief Build sensor list message
  * \param l Reference to katcl_line structure
  * \return 0 as success
  */
int sensor_list(struct katcl_line *l)
{
  append_string_katcl(l, KATCP_FLAG_FIRST | KATCP_FLAG_LAST | KATCP_FLAG_STRING , "?sensor-list");
  return 0;
}

/**
  * \brief Create netstate structure
  * \param l Reference to katcl_line structure
  * \param ts Reference to totalstate structure
  * \param server Reference to sensor name
  * \return katcl_line structure as success or NULL as failure
  */
struct katcl_line *create_netstate(struct katcl_line *l, struct totalstate *ts, char *server)
{
  int fd;
  struct katcl_line *p;

  if (server == NULL){
    log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "invalid server name provided");
    return NULL;
  }

  fd = initiate_connection(l, ts, server);

  if(fd < 0){
    log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "unable to initiate connection to %s", server);
    return NULL;
  }

  p = create_katcl(fd);
  if(p == NULL){
    return NULL;
  }

  return p;
}

/**
  * \brief Handle netstate i/o
  * \param p Reference to katcl_line structure
  * \param l Reference to katcl_line structure
  * \param ts Reference to totalstate structure
  * \return 0 as success or -1 as failure
  */
int parse_netstate(struct katcl_line *p, struct katcl_line *l, struct totalstate *ts)
{
  /* Remove parts that are now in main */
  char *name, *ptr;
  struct katcl_parse *sniff;

  while (parse_katcl(p) > 0){
    sniff = ready_katcl(p);
    if (sniff){
      ptr = get_string_parse_katcl(sniff, 0);
      switch (ptr[0]){
        case KATCP_INFORM :
          if (!strcmp(ptr, "#sensor-status")){
            append_parse_katcl(l, sniff);
          }
          if (!strcmp(ptr, "#sensor-list")){
            add_sensor(l, ts, get_string_parse_katcl(sniff, 1));
          }
          break;

        case KATCP_REPLY :
          if (!strcmp(ptr, "!sensor-sampling")){
            ptr = get_string_parse_katcl(sniff, 1);
            name = get_string_parse_katcl(sniff, 2);
            if ((ptr == NULL) || strcmp(ptr, KATCP_OK)){
              log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "unable to monitor sensor %s", name);
              clear_katcl(p);
              return -1;
            }
            set_sensor_subscribe(p, ts, name);
          } else if (!strcmp(ptr, "!sensor-list")){
            ptr = get_string_parse_katcl(sniff, 1);
            if ((ptr == NULL) || strcmp(ptr, KATCP_OK)){
              log_message_katcl(l, KATCP_LEVEL_ERROR, ts->t_system, "unable to get sensor list");
              clear_katcl(p);
              return -1;
            }
            ts->t_sensor_list = get_unsigned_long_parse_katcl(sniff, 2);
            log_message_katcl(l, KATCP_LEVEL_INFO, ts->t_system, "server has %s sensors", get_string_parse_katcl(sniff, 2));
          } else {
            log_message_katcl(l, KATCP_LEVEL_WARN, ts->t_system, "response %s is unexpected", ptr);
          }
          break;

        case KATCP_REQUEST :
          log_message_katcl(l, KATCP_LEVEL_WARN, ts->t_system, "encountered an unanswerable request %s", ptr);
          break;

        default :
          log_message_katcl(l, KATCP_LEVEL_WARN, ts->t_system, "read malformed message %s", ptr);
          break;
      }
    clear_katcl(p);
    }
  }

  return 0;
}

int main(int argc, char **argv)
{
#define BUFFER 128
#if 0
  int parentgone, childgone;
#endif
  int terminate, code, exitrelay, checkinput, limit;
  int levels[2], index, efds[2], ofds[2];
  int i, j, c, offset, result, rr;
  struct katcl_line *k, *p;
  char *app;
  char *tmp, *value, *server;
  pid_t pid;
  struct totalstate total, *ts;
  struct iostate *erp, *orp;
  fd_set fsr, fsw; /* fsr_net, fsw_net; */
  int mfd, fd;
  unsigned char buffer[BUFFER];
  struct timeval timeout;
  struct sigaction sag;
  int niceness;

  sag.sa_handler = &handle_timeout;
  sag.sa_flags = 0;
  sigemptyset(&sag.sa_mask);

  sigaction(SIGALRM, &sag, NULL);

  ts = &total;

  terminate = 1;
  offset = (-1);
  exitrelay = 0;
  checkinput = 1;
  limit = 0;
  niceness = 0;

  levels[0] = KATCP_LEVEL_INFO;
  levels[1] = KATCP_LEVEL_ERROR;

  i = j = 1;

  ts->t_system = getenv("KATCP_LABEL");
  ts->t_verbose = 1;
  ts->t_infer = 0;
  ts->t_head = ts->t_current = NULL;
  ts->t_sensor_list = 0;
  ts->t_sensor_added = 0;
  ts->t_subscribed = 0;

  if(ts->t_system == NULL){
    ts->t_system = "run";
  }

  server = NULL;
  app = argv[0];

  p = NULL;

  k = create_katcl(STDOUT_FILENO);
  if(k == NULL){
    fprintf(stderr, "%s: unable to create katcp message logic\n", app);
    return EX_OSERR;
  }

  while (i < argc) {
    if (argv[i][0] == '-') {
      c = argv[i][j];
      switch (c) {

        case 'h' :
          usage(app);
          return 0;

        case 'x' :
          exitrelay = 1;
          j++;
          break;

        case 'i' :
          terminate = 0;
          j++;
          break;

        case 'j' :
          checkinput = 0;
          j++;
          break;

        case 'r' :
          ts->t_infer = 1;
          j++;
          break;

        case 'q' :
          ts->t_verbose = 0;
          j++;
          break;

        case 'v' :
          ts->t_verbose++;
          j++;
          break;

        case 'e' :
        case 'l' :
        case 'n' :
        case 'o' :
        case 'p' :
        case 's' :
        case 't' :

          j++;
          if (argv[i][j] == '\0') {
            j = 0;
            i++;
          }

          if (i >= argc) {
            sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "option -%c needs a parameter", c);
            return EX_USAGE;
          }

          index = 0;

          switch(c){
            case 'e' :
              index++;
              /* fall */
            case 'o' :
              tmp = argv[i] + j;
#ifdef KATCP_CONSISTENCY_CHECKS
              if((index < 0) || (index > 1)){
                fprintf(stderr, "logic problem: index value %d not correct\n", index);
                abort();
              }
#endif
              if(!strcmp(tmp, "auto")){
                levels[index] = (-1);
              } else {
                levels[index] = log_to_code_katcl(tmp);
                if(levels[index] < 0){
                  sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "unknown log level %s", tmp);
                  levels[index] = KATCP_LEVEL_FATAL;
                }
              }
              break;
            case 's' :
              ts->t_system = argv[i] + j;
              if(ts->t_system){
                setenv("KATCP_LABEL", ts->t_system, 1);
              }

              break;
            case 'n' :
              tmp = strdup(argv[i] + j);
              if(tmp){
                value = strchr(tmp, '=');
                if(value){
                  value[0] = '\0';
                  value++;
                  result = setenv(tmp, value, 1);
                } else {
                  result = unsetenv(tmp);
                }
                if(result){
                  sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "unable to to modify environment: %s\n", strerror(errno));
                }
                free(tmp);
              } else {
                sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "unable to retrieve environment variable");
              }
              break;
            case 'p' :
              server = argv[i] + j;
              break;

            case 'l' :
              niceness = atoi(argv[i] + j);
              if(niceness != 0){
                if(niceness <= 0){
                  if(getuid()){
                    sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "unlikely to be able to increase priority if not root");
                  }
                }
                if(nice(niceness) < 0){
                  sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "unable to adjust priority to %d", niceness);
                }
              }
              break;
            case 't' :
              limit = atoi(argv[i] + j);
              if(limit == 0){
                sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "limit needs to be a natural number");
              } else {
                timeout.tv_sec = limit / 1000;
                timeout.tv_usec = (limit % 1000) * 1000;
              }
              break;

          }

          i++;
          j = 1;
          break;

        case '-' :
          j++;
          break;
        case '\0':
          j = 1;
          i++;
          break;
        default:
          fprintf(stderr, "%s: unknown option -%c\n", app, argv[i][j]);
          return 2;
      }
    } else {
      if(offset < 0){
        offset = i;
      }
      i = argc;
    }
  }

  if(offset < 0){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "need something to run");
    return EX_USAGE;
  }

  if(pipe(efds) < 0){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to create error pipe: %s", strerror(errno));
    return EX_OSERR;
  }

  if(pipe(ofds) < 0){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to create output pipe: %s", strerror(errno));
    return EX_OSERR;
  }

  pid = fork();
  if(pid < 0){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to fork: %s", strerror(errno));
    return EX_OSERR;
  }

  if(pid == 0){
    /* in child */

    if(terminate || (checkinput == 0)){
      /* child not going to listen to stdin anyway, though maybe use /dev/zero instead, just in case ? */
      fd = open("/dev/null", O_RDONLY);
      if(fd < 0){
        sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_WARN), ts->t_system, "unable to open /dev/null: %s", strerror(errno));
      }

      if(dup2(fd, STDIN_FILENO) != STDIN_FILENO){
        sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_WARN), ts->t_system, "unable to replace standard input filedescriptor: %s", strerror(errno));
      } else {
        close(fd);
      }
    }

    if(dup2(efds[1], STDERR_FILENO) != STDERR_FILENO){
      sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to replace standard error filedescriptor: %s", strerror(errno));
      return EX_OSERR;
    }

    if(dup2(ofds[1], STDOUT_FILENO) != STDOUT_FILENO){
      sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to replace standard output filedescriptor: %s", strerror(errno));
      return EX_OSERR;
    }

    close(ofds[0]);
    close(efds[0]);

    execvp(argv[offset], &(argv[offset]));

    return EX_OSERR;
  }

  log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_DEBUG), ts->t_system, "launched %s as process %d", argv[offset], pid);

  close(ofds[1]);
  close(efds[1]);

  orp = create_iostate(ofds[0], levels[0]);
  erp = create_iostate(efds[0], levels[1]);

  ofds[0] = (-1);
  efds[0] = (-1);

  if((orp == NULL) || (erp == NULL)){
    sync_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "unable to allocate state for io handlers");
    return EX_OSERR;
  }

#if 0
  childgone = 0;
  parentgone = 0;
#endif

  if (server != NULL){
    p = create_netstate(k, ts, server);
    if (p == NULL){
      sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "unable to allocate state for net handler");
      return EX_OSERR;
    }
    sensor_list(p);
  }

  do{
    FD_ZERO(&fsr);
    FD_ZERO(&fsw);

    mfd = 0;

    if (p != NULL){
      fd = fileno_katcl(p);
      FD_SET(fd, &fsr);
      if(fd > mfd){
        mfd = fd;
      }

      if(flushing_katcl(p)){
        fd = fileno_katcl(p);
        FD_SET(fd, &fsw);
      }

      fd = fileno_katcl(p);
      if(FD_ISSET(fd, &fsw)){
        result =  write_katcl(p);
        if(result < 0){
          sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "write failed: %s", strerror(error_katcl(p)));
          return -1;
        }
      }

      if(FD_ISSET(fd, &fsr)){
        result = read_katcl(p);
        if(result){
          sync_message_katcl(k, KATCP_LEVEL_ERROR, ts->t_system, "read failed: %s\n", (result < 0) ? strerror(error_katcl(p)) : "connection terminated");
          return -1;
        }
      }
      parse_netstate(p, k, ts);
      if (ts->t_subscribed < ts->t_sensor_list) {
        /* check_sensor_subscribe(k, ts); */
        subscribe_sensor(p, k, ts);
      }
    }

    if(checkinput){
      fd = STDIN_FILENO;
      FD_SET(fd, &fsr);
      if(fd > mfd){
        mfd = fd;
      }
    }

    FD_SET(orp->i_fd, &fsr);
    if(orp->i_fd > mfd){
      mfd = orp->i_fd;
    }

    FD_SET(erp->i_fd, &fsr);
    if(erp->i_fd > mfd){
      mfd = erp->i_fd;
    }

    if(flushing_katcl(k)){
#ifdef DEBUG
      fprintf(stderr, "need to flush\n");
#endif
      fd = fileno_katcl(k);
      FD_SET(fd, &fsw);
    }

    result = select(mfd + 1, &fsr, &fsw, NULL, limit ? &timeout : NULL);
    if(result < 0){
      switch(errno){
        case EAGAIN :
        case EINTR  :
          result = 0;
          break;
        default :
          log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_ERROR), ts->t_system, "select failed: %s", strerror(errno));
          break;
      }
    } else if(result == 0){
      log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_WARN), ts->t_system, "timeout of %s after %ums", argv[offset], limit);
      result = (-1);
    } else {

      fd = fileno_katcl(k);
      if(FD_ISSET(fd, &fsw)){
#ifdef DEBUG
        fprintf(stderr, "invoking flush logic\n");
#endif
        write_katcl(k);
      }

      if(FD_ISSET(STDIN_FILENO, &fsr)){
        rr = read(STDIN_FILENO, buffer, BUFFER);
        if(rr <= 0){
          if(rr < 0){
            switch(errno){
              case EAGAIN :
              case EINTR  :
                break;
              default :
#if 0
                parentgone = 1;
#endif
                result = (-1);
                break;
            }
          } else {
#if 0
            parentgone = 1;
#endif
            result = (-1);
          }
        }
      }

      if(FD_ISSET(orp->i_fd, &fsr)){
        if(run_iostate(ts, orp, k)){
#if 0
          childgone = 1;
#endif
          result = (-1);
        }
      }

      if(FD_ISSET(erp->i_fd, &fsr)){
        if(run_iostate(ts, erp, k)){
#if 0
          childgone = 1;
#endif
          result = (-1);
        }
      }

    }

  } while(result >= 0);

  code = collect_child(k, argv[offset], ts->t_system, ts->t_verbose, pid, 0);

  if(code < 0){
    if(terminate){
      log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_INFO), ts->t_system, "terminating task %s", argv[offset]);
      if(kill(pid, SIGTERM) < 0){
        switch(errno){
          case ESRCH  :
          case ENOENT :
            break;
          default :
            log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_INFO), ts->t_system, "unable to terminate task %s", argv[offset]);
            break;
        }
      } else {
        if(collect_child(k, argv[offset], ts->t_system, ts->t_verbose, pid, 1) < 0){
          log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_INFO), ts->t_system, "killing task %s", argv[offset]);
          if(kill(pid, SIGKILL) < 0){
            switch(errno){
              case ESRCH  :
              case ENOENT :
                break;
              default :
                log_message_katcl(k, tweaklevel(ts->t_verbose, KATCP_LEVEL_INFO), ts->t_system, "unable to terminate task %s", argv[offset]);
                break;
            }
          } else {
            code = collect_child(k, argv[offset], ts->t_system, ts->t_verbose, pid, 1);
          }
        }
      }
    }
  }

  if(exitrelay == 0){
    code = 0;
  }

  while((result = write_katcl(k)) == 0);

  destroy_iostate(orp);
  destroy_iostate(erp);

  destroy_katcl(k, 0);
  destroy_katcl(p, 0);
  destroy_sensors(ts);

  return code;
#undef BUFFER
}
