#ifndef WIFI_H
#define WIFI_H
#include "freertos/event_groups.h"

extern EventGroupHandle_t s_wifi_event_group;
#define WIFI_CONNECTED_BIT BIT0


void wifi_init_sta(void);

#endif // WIFI_H
