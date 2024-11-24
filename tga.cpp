#include "tga.h"

#include <stdio.h>
#include <fstream>
#include <string>
#include <cstring>

short le_short(unsigned char * bytes)
{
	return bytes[0] | ((char)bytes[1] << 8);
}

struct tga_header {
	char  id_length; // byte 0
	char  color_map_type;
	char  data_type_code; // 2 for uncompressed truecolor, 10 for RLE truecolor
	unsigned char  color_map_origin[2];
	unsigned char  color_map_length[2];
	char  color_map_depth;
	unsigned char  x_origin[2]; // byte 8
	unsigned char  y_origin[2];
	unsigned char  width[2];
	unsigned char  height[2];
	char  bits_per_pixel; // byte 16
	char  image_descriptor;
};

bool write_tga(const char * filename, unsigned width, unsigned height, const unsigned char * data) {
	tga_header header;
    memset(&header, 0, sizeof(header));

	header.data_type_code = 2;
	header.color_map_origin[0] = 0;
	memcpy(header.height, &height, 2);
	memcpy(header.width, &width, 2);
	header.bits_per_pixel = 24;

	FILE * f;
#ifdef _WIN32
	fopen_s(&f, filename, "wb");
#else
    f = fopen(filename, "wb");
#endif

	if (!f) {
		fprintf(stderr, "Unable to open %s for writing\n", filename);
		return false;
	}

    struct FileClose {
        FILE * f;
        ~FileClose() { fclose(f); }
    } closer{ f };

	if (1 != fwrite(&header, sizeof(header), 1, f)) {
		fprintf(stderr, "Failed to write 1 %lu byte header.\n", sizeof(header));
		return false;
	}

	if (width*height != fwrite(data, sizeof(unsigned char)*(header.bits_per_pixel/8), width*height, f)) {
		fprintf(stderr, "Failed to write %d %d-byte pixels.\n", width*height, header.bits_per_pixel / 3);
		return false;
	}

	return true;
}

void fail(const char * reason) {
    throw std::runtime_error(reason);
}

void * read_tga(const std::vector<char> & bytes, unsigned & width, unsigned & height, int & bpp) {
	int color_map_size, pixels_size;
    bool rle;
	void *pixels;

    printf("header size: %lu\n", sizeof(tga_header));

	if (bytes.size() < sizeof(tga_header)) {
        fail("data has no tga header");
	}
    tga_header & header = *(tga_header*)bytes.data();

	if (header.data_type_code != 2 && header.data_type_code != 10) {
        fail("data is not a truecolor tga");
	}
    if (header.bits_per_pixel != 24 && header.bits_per_pixel != 32) {
		fail("data is not a 24 or 32-bit uncompressed RGB tga file");
	}

    bpp = header.bits_per_pixel;
    u_char pixelSize = bpp / 8;
    rle = header.data_type_code == 2 ? false : true;

    int remainingBytes = bytes.size() - sizeof(tga_header);
	if (remainingBytes < header.id_length) {
		fail("data has incomplete id string");
	}
    remainingBytes -= header.id_length;

	color_map_size = le_short(header.color_map_length) * (header.color_map_depth / 8);
	if (remainingBytes < color_map_size) {
		fail("file has incomplete color map");
	}

    remainingBytes -= color_map_size;

	width = le_short(header.width); height = le_short(header.height);
	pixels_size = width * height * (header.bits_per_pixel / 8);

    if (remainingBytes < pixels_size) {
        fail("data has incomplete image");
    }

	pixels = malloc(pixels_size);
    const u_char rleChunkFlag = 0x80;
    const char * currentByte = &bytes[0] + (bytes.size() - remainingBytes);
    if (!rle) {
        memcpy(pixels, currentByte, pixels_size);
    } else {
        u_char * pixelCursor = (u_char*)pixels;
        u_char * end = pixelCursor + (width  * height * pixelSize);
        u_char chunkHeader = *currentByte++;
        do {
            if (chunkHeader & rleChunkFlag) { // rle compressed chunk
                u_char pixelCount = (chunkHeader ^ rleChunkFlag) + 1; // remove flag
                for (int i = 0; i < pixelCount; i++) {
                    memcpy(pixelCursor, currentByte, pixelSize);
                    pixelCursor += pixelSize;
                }
            } else {
                u_char pixelCount = chunkHeader + 1;
                memcpy(pixelCursor, currentByte, pixelCount * pixelSize);
                pixelCursor += (pixelCount * pixelSize);
                currentByte += (pixelCount * pixelSize);
            }
        } while (pixelCursor < end);
    }

    const unsigned char SCREEN_ORIGIN_BIT = 0x20;

    if (header.image_descriptor & SCREEN_ORIGIN_BIT == 0) {
        // origin is in bottom-left, which is opposite of Vulkan convention, so flip the image rows
        u_char * source = (u_char*)pixels;
        u_char * flipped = (u_char*)malloc(pixels_size);
        unsigned rowSize = pixelSize * width;
        for (size_t i = 0; i < height; i++) {
            memcpy(flipped + i * rowSize, source + ((height - 1 - i) * rowSize), rowSize);
        }
        free(pixels);
        pixels = flipped;
    }

	return pixels;
}
