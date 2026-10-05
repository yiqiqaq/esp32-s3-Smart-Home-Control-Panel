#pragma once
#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Wall-clock service: SNTP after station got an IP, greeting bands and the
 * day/night flag consumed by the theme resolver. Time text is committed to the
 * app state only when the minute or the day/night phase changes. */

esp_err_t time_service_init(void);
bool time_service_is_synced(void);
bool time_service_is_day(void);   /* 06:00..17:59, falls back to day while unsynced */
uint8_t time_service_hour(void);  /* local hour 0..23; 0 while unsynced */

#ifdef __cplusplus
}
#endif
