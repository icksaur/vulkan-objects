#pragma once

#include <vector>
#include <cstdint>

void * read_tga(const std::vector<uint8_t> & bytes, unsigned & width, unsigned & height, int & bpp);

bool write_tga(const char * filename, unsigned width, unsigned height, const unsigned char * data);