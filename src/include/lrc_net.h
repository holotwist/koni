#ifndef LRC_NET_H
#define LRC_NET_H

#include <stdint.h>

void lrc_fetch_async(const char* title, const char* artist, const char* album, uint32_t duration, const char* filepath);

#endif // LRC_NET_H