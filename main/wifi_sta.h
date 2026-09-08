#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*wifi_got_ip_cb_t)(void);

// Starts Wi-Fi in station mode and keeps trying to (re)connect. `on_got_ip` is
// invoked from the default event loop each time an IP is acquired.
void wifi_sta_start(const char *ssid, const char *pass, wifi_got_ip_cb_t on_got_ip);

bool wifi_sta_connected(void);
int  wifi_sta_rssi(void);

#ifdef __cplusplus
}
#endif
