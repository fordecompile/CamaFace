#pragma once
#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <wchar.h>

int sp_open(const wchar_t* com_port, int baud);
void sp_close(void);
int sp_write(const void* data, int len);
int sp_read(void* data, int len, int timeout_ms);

#ifdef __cplusplus
}
#endif
