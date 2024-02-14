#ifndef INCLUSION_GUARD_VIP_H
#define INCLUSION_GUARD_VIP_H

#include <gio/gio.h>

int vip_init();

gboolean vip_cmd_callback(GIOChannel *source,
                         GIOCondition cond,
                         gpointer data);

#endif // INCLUSION_GUARD_VIP_H