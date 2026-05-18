#pragma once

#include <Arduino.h>

#define NET_HTTPS_BODY_MAX  (128 * 1024)

/**
 * GET over HTTPS into a PSRAM buffer (mutex, heap wait, retries).
 * On success *out_body is heap_caps_malloc'd; caller must heap_caps_free it.
 */
bool net_https_get(const char *url, char **out_body, size_t *out_len, uint32_t timeout_ms);

/** Same with a custom minimum contiguous internal heap block before TLS. */
bool net_https_get_ex(const char *url, char **out_body, size_t *out_len, uint32_t timeout_ms,
                      size_t min_internal_block);

/** Wait until internal heap can satisfy TLS (used before HTTPS and after large fetches). */
bool net_https_wait_heap(size_t min_internal_block, uint32_t wait_ms);
