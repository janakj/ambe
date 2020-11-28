/*
 * A library for working with DVSI's AMBE vocoder chips
 *
 * Copyright (C) 2019-2020 Internet Real-Time Lab, Columbia University
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <https://www.gnu.org/licenses/>.
 */

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
