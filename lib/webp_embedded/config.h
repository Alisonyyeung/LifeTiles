/* Minimal libwebp config for ESP32 decoder build. */

#ifndef WEBP_WEBP_CONFIG_H_
#define WEBP_WEBP_CONFIG_H_

#define HAVE_BUILTIN_BSWAP16 1
#define HAVE_BUILTIN_BSWAP32 1
#define HAVE_BUILTIN_BSWAP64 1

#define WEBP_HAVE_GIF 0
#define WEBP_HAVE_GL 0
#define WEBP_HAVE_JPEG 0
#define WEBP_HAVE_PNG 0
#define WEBP_HAVE_SDL 0
#define WEBP_HAVE_TIFF 0

#define WEBP_NEAR_LOSSLESS 1

#endif
