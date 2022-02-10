#ifndef TG_H_
#define TG_H_

int tap_stop_cmd(struct katcp_dispatch *d, int argc);
int tap_start_cmd(struct katcp_dispatch *d, int argc);

int tap_info_cmd(struct katcp_dispatch *d, int argc);
int tap_reload_cmd(struct katcp_dispatch *d, int argc);
int tap_config_cmd(struct katcp_dispatch *d, int argc);

int tap_ip_config_cmd(struct katcp_dispatch *d, int argc);

int tap_multicast_add_group_cmd(struct katcp_dispatch *d, int argc);
int tap_multicast_remove_group_cmd(struct katcp_dispatch *d, int argc);

int tap_route_add_cmd(struct katcp_dispatch *d, int argc);

int tap_multicast_add_group_cmd(struct katcp_dispatch *d, int argc);
int tap_multicast_remove_group_cmd(struct katcp_dispatch *d, int argc);

void stop_all_getap(struct katcp_dispatch *d, int final);

int tap_dhcp_cmd(struct katcp_dispatch *d, int argc);

#endif
