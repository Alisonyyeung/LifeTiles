#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/** Start FreeRTOS HTTP task on core 0; call once from setup. */
void image_upload_server_init(void);
void image_upload_server_start(void);
/** Stop and restart HTTP server (e.g. after IP change). */
void image_upload_server_restart(void);

#ifdef __cplusplus
}
#endif
