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

// Git LFS stores large files in a repo as small text placeholders; a folder cloned without Git LFS
// (or downloaded as a ZIP from GitHub) has these instead of the real images and fonts.
bool IsGitLfsPointer(const std::string& bytes);
