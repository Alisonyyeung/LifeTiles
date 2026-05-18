#pragma once

#ifdef __cplusplus
extern "C" {
#endif

void wifi_services_init(void);

/** NTP, today's quote, upload server — runs on a worker task after (re)connect. */
void wifi_services_on_connected(void);

#ifdef __cplusplus
}
#endif
