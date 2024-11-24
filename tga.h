#pragma once

#include <vector>

void * read_tga(const std::vector<char> & bytes, unsigned & width, unsigned & height, int & bpp);

bool write_tga(const char * filename, unsigned width, unsigned height, const unsigned char * data);