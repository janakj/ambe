#pragma once
#include <stdint.h>
#include <stdlib.h>

#ifdef __cplusplus
extern "C" {
#endif

void* ambe_open      (const char* uri, const char* rate, int deadline);
void  ambe_close     (void* handle);
int   ambe_compress  (char* bits, size_t* bit_count, void* handle, const int16_t* samples, size_t sample_count);
int   ambe_decompress(int16_t* samples, size_t* sample_count, void* handle, const char* bits, size_t bit_count);

#ifdef __cplusplus
}
#endif
