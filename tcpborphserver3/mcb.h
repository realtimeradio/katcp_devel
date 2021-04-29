#ifndef MCB_H_
#define MCB_H_

#include <stdlib.h>

struct mcb_nx {
  int a;
  int b;
};

int mcb_ok_check_cmd(struct katcp_dispatch *d, int argc);
int mcb_own_check_cmd(struct katcp_dispatch *d, int argc);
int mcb_send_value_cmd(struct katcp_dispatch *d, int argc);
int mcb_notices_cmd(struct katcp_dispatch *d, int argc);

#endif // MCB_H_
