#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

// Sets the TZ env var and seeds the system clock from the PCF85063 RTC.
// pcf85063_init() must have run first.
void time_sync_init(const char *tz, const char *ntp_server);

// Call once Wi-Fi has an IP: starts SNTP. On each successful sync the system
// time is written back into the RTC.
void time_sync_start_sntp(void);

// True once at least one NTP sync has completed this boot.
bool time_sync_ntp_done(void);

#ifdef __cplusplus
}
#endif
