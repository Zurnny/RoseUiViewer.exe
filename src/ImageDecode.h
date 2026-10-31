#pragma once
#include <cstdint>
#include <string>
#include <vector>

struct DecodedImage {
	int width = 0, height = 0;
	std::vector<uint8_t> rgba; // straight (not premultiplied) RGBA8, top row first
};

// Decodes DDS (uncompressed, DXT1/3/5, BC4/BC5) and anything stb_image reads (PNG, TGA, JPG, BMP).
bool DecodeImage(const std::string& bytes, DecodedImage& out, std::string& error);

void PremultiplyAlpha(DecodedImage& img);
