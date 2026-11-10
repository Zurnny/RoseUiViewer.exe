#include "ImageDecode.h"
#include <algorithm>
#include <cmath>
#include <cstring>

#define STB_IMAGE_IMPLEMENTATION
#define STBI_NO_PSD
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#include <stb_image.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include <stb_image_write.h>

#define BCDEC_IMPLEMENTATION
#include "../third_party/bcdec/bcdec.h"

namespace {

uint32_t U32(const uint8_t* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | ((uint32_t)p[3] << 24); }
uint16_t U16(const uint8_t* p) { return (uint16_t)(p[0] | (p[1] << 8)); }

void Rgb565(uint16_t c, uint8_t out[3])
{
	int r = (c >> 11) & 31, g = (c >> 5) & 63, b = c & 31;
	out[0] = (uint8_t)((r << 3) | (r >> 2));
	out[1] = (uint8_t)((g << 2) | (g >> 4));
	out[2] = (uint8_t)((b << 3) | (b >> 2));
}

// Colour part of a BC1/BC2/BC3 block -> 16 RGBA pixels (alpha set to 255, or 0 for BC1 transparent).
void DecodeColorBlock(const uint8_t* b, uint8_t px[16][4], bool bc1)
{
	uint16_t c0 = U16(b), c1 = U16(b + 2);
	uint8_t col[4][4];
	Rgb565(c0, col[0]);
	Rgb565(c1, col[1]);
	col[0][3] = col[1][3] = 255;
	if (!bc1 || c0 > c1)
	{
		for (int i = 0; i < 3; i++)
		{
			col[2][i] = (uint8_t)((2 * col[0][i] + col[1][i]) / 3);
			col[3][i] = (uint8_t)((col[0][i] + 2 * col[1][i]) / 3);
		}
		col[2][3] = col[3][3] = 255;
	}
	else
	{
		for (int i = 0; i < 3; i++)
		{
			col[2][i] = (uint8_t)((col[0][i] + col[1][i]) / 2);
			col[3][i] = 0;
		}
		col[2][3] = 255;
		col[3][3] = 0;
	}
	uint32_t idx = U32(b + 4);
	for (int i = 0; i < 16; i++)
	{
		int k = (idx >> (2 * i)) & 3;
		memcpy(px[i], col[k], 4);
	}
}

// BC4-style 8-byte block -> 16 single-channel values.
void DecodeAlphaBlock(const uint8_t* b, uint8_t out[16])
{
	uint8_t a0 = b[0], a1 = b[1];
	uint8_t a[8];
	a[0] = a0;
	a[1] = a1;
	if (a0 > a1)
		for (int i = 1; i < 7; i++)
			a[i + 1] = (uint8_t)(((7 - i) * a0 + i * a1) / 7);
	else
	{
		for (int i = 1; i < 5; i++)
			a[i + 1] = (uint8_t)(((5 - i) * a0 + i * a1) / 5);
		a[6] = 0;
		a[7] = 255;
	}
	uint64_t bits = 0;
	for (int i = 0; i < 6; i++)
		bits |= (uint64_t)b[2 + i] << (8 * i);
	for (int i = 0; i < 16; i++)
		out[i] = a[(bits >> (3 * i)) & 7];
}

enum class Bc { None, BC1, BC2, BC3, BC4, BC5, BC6H, BC7 };

float HalfToFloat(uint16_t h)
{
	const int e = (h >> 10) & 31, m = h & 1023;
	float v = e == 0 ? m / 1024.f / 16384.f : e == 31 ? 65504.f : (1.f + m / 1024.f) * std::ldexp(1.f, e - 15);
	return (h & 0x8000) ? -v : v;
}

bool DecodeBlocks(const uint8_t* data, size_t size, int w, int h, Bc fmt, DecodedImage& out, std::string& error)
{
	const int bw = (w + 3) / 4, bh = (h + 3) / 4;
	const size_t block = (fmt == Bc::BC1 || fmt == Bc::BC4) ? 8 : 16; // BC2/3/5/6H/7 are 16 bytes
	if (size < (size_t)bw * bh * block)
	{
		error = "DDS file is shorter than its size says";
		return false;
	}
	out.width = w;
	out.height = h;
	out.rgba.assign((size_t)w * h * 4, 0);
	for (int by = 0; by < bh; by++)
		for (int bx = 0; bx < bw; bx++)
		{
			const uint8_t* b = data + ((size_t)by * bw + bx) * block;
			uint8_t px[16][4];
			switch (fmt)
			{
			case Bc::BC1: DecodeColorBlock(b, px, true); break;
			case Bc::BC2:
				DecodeColorBlock(b + 8, px, false);
				for (int i = 0; i < 16; i++)
				{
					int a = (b[i / 2] >> ((i & 1) * 4)) & 15;
					px[i][3] = (uint8_t)(a * 17);
				}
				break;
			case Bc::BC3:
			{
				DecodeColorBlock(b + 8, px, false);
				uint8_t a[16];
				DecodeAlphaBlock(b, a);
				for (int i = 0; i < 16; i++)
					px[i][3] = a[i];
				break;
			}
			case Bc::BC4:
			{
				uint8_t r[16];
				DecodeAlphaBlock(b, r);
				for (int i = 0; i < 16; i++)
					px[i][0] = px[i][1] = px[i][2] = r[i], px[i][3] = 255;
				break;
			}
			case Bc::BC7:
			{
				uint8_t tmp[16 * 4];
				bcdec_bc7(b, tmp, 4 * 4);
				for (int i = 0; i < 16; i++)
					memcpy(px[i], tmp + i * 4, 4);
				break;
			}
			case Bc::BC6H:
			{
				uint16_t tmp[16 * 3];
				bcdec_bc6h_half(b, tmp, 4 * 3, 0);
				for (int i = 0; i < 16; i++)
				{
					for (int c = 0; c < 3; c++)
					{
						float f = HalfToFloat(tmp[i * 3 + c]);
						px[i][c] = (uint8_t)std::round(std::min(1.f, std::max(0.f, f)) * 255.f);
					}
					px[i][3] = 255;
				}
				break;
			}
			case Bc::BC5:
			{
				uint8_t r[16], g[16];
				DecodeAlphaBlock(b, r);
				DecodeAlphaBlock(b + 8, g);
				for (int i = 0; i < 16; i++)
					px[i][0] = r[i], px[i][1] = g[i], px[i][2] = 0, px[i][3] = 255;
				break;
			}
			default: break;
			}
			for (int i = 0; i < 16; i++)
			{
				int x = bx * 4 + (i & 3), y = by * 4 + (i >> 2);
				if (x < w && y < h)
					memcpy(&out.rgba[((size_t)y * w + x) * 4], px[i], 4);
			}
		}
	return true;
}

int Shift(uint32_t mask)
{
	if (!mask)
		return 0;
	int s = 0;
	while (!(mask & 1))
		mask >>= 1, s++;
	return s;
}
int Bits(uint32_t mask)
{
	int n = 0;
	while (mask)
		n += mask & 1, mask >>= 1;
	return n;
}
uint8_t Channel(uint32_t pixel, uint32_t mask)
{
	if (!mask)
		return 0;
	uint32_t v = (pixel & mask) >> Shift(mask);
	int bits = Bits(mask);
	if (bits >= 8)
		return (uint8_t)(v >> (bits - 8));
	return (uint8_t)((v * 255) / ((1u << bits) - 1));
}

bool DecodeDDS(const std::string& bytes, DecodedImage& out, std::string& error)
{
	const uint8_t* d = (const uint8_t*)bytes.data();
	if (bytes.size() < 128)
	{
		error = "DDS file too small";
		return false;
	}
	const int h = (int)U32(d + 12), w = (int)U32(d + 16);
	const uint8_t* pf = d + 76;
	const uint32_t flags = U32(pf + 4);
	const uint32_t fourcc = U32(pf + 8);
	const uint32_t bitcount = U32(pf + 12);
	const uint32_t rm = U32(pf + 16), gm = U32(pf + 20), bm = U32(pf + 24), am = U32(pf + 28);
	size_t offset = 128;

	auto FCC = [](const char* s) { return (uint32_t)s[0] | ((uint32_t)s[1] << 8) | ((uint32_t)s[2] << 16) | ((uint32_t)s[3] << 24); };

	if (flags & 0x4) // FOURCC
	{
		Bc fmt = Bc::None;
		uint32_t dxgi_uncompressed = 0;
		if (fourcc == FCC("DXT1"))
			fmt = Bc::BC1;
		else if (fourcc == FCC("DXT2") || fourcc == FCC("DXT3"))
			fmt = Bc::BC2;
		else if (fourcc == FCC("DXT4") || fourcc == FCC("DXT5"))
			fmt = Bc::BC3;
		else if (fourcc == FCC("ATI1") || fourcc == FCC("BC4U"))
			fmt = Bc::BC4;
		else if (fourcc == FCC("ATI2") || fourcc == FCC("BC5U"))
			fmt = Bc::BC5;
		else if (fourcc == FCC("DX10"))
		{
			if (bytes.size() < 148)
			{
				error = "DDS DX10 header missing";
				return false;
			}
			uint32_t dxgi = U32(d + 128);
			offset = 148;
			switch (dxgi)
			{
			case 70: case 71: case 72: fmt = Bc::BC1; break;
			case 73: case 74: case 75: fmt = Bc::BC2; break;
			case 76: case 77: case 78: fmt = Bc::BC3; break;
			case 79: case 80: fmt = Bc::BC4; break;
			case 82: case 83: fmt = Bc::BC5; break;
			case 94: case 95: case 96: fmt = Bc::BC6H; break;
			case 97: case 98: case 99: fmt = Bc::BC7; break;
			case 27: case 28: case 29: dxgi_uncompressed = 1; break; // R8G8B8A8
			case 87: case 90: case 91: dxgi_uncompressed = 2; break; // B8G8R8A8
			default: error = "DDS format DXGI " + std::to_string(dxgi) + " isn't supported (DXT1/3/5, BC4-BC7 and uncompressed are)"; return false;
			}
		}
		else
		{
			error = "DDS compression isn't supported (try DXT5 or uncompressed)";
			return false;
		}
		if (dxgi_uncompressed)
		{
			size_t need = (size_t)w * h * 4;
			if (bytes.size() - offset < need)
			{
				error = "DDS file is shorter than its size says";
				return false;
			}
			out.width = w;
			out.height = h;
			out.rgba.assign(d + offset, d + offset + need);
			if (dxgi_uncompressed == 2)
				for (size_t i = 0; i < need; i += 4)
					std::swap(out.rgba[i], out.rgba[i + 2]);
			return true;
		}
		return DecodeBlocks(d + offset, bytes.size() - offset, w, h, fmt, out, error);
	}

	// Uncompressed RGB / luminance / alpha, described by bit masks.
	const int bpp = (int)bitcount / 8;
	if (bpp < 1 || bpp > 4)
	{
		error = "DDS pixel format isn't supported";
		return false;
	}
	size_t pitch = (size_t)w * bpp;
	if (bytes.size() - offset < pitch * h)
	{
		error = "DDS file is shorter than its size says";
		return false;
	}
	out.width = w;
	out.height = h;
	out.rgba.resize((size_t)w * h * 4);
	const bool luminance = (flags & 0x20000) != 0;
	const bool alpha_only = (flags & 0x2) != 0 && !(flags & 0x40) && !luminance;
	const bool has_alpha = (flags & 0x1) != 0 || alpha_only;
	for (int y = 0; y < h; y++)
		for (int x = 0; x < w; x++)
		{
			const uint8_t* p = d + offset + y * pitch + x * bpp;
			uint32_t v = 0;
			for (int i = 0; i < bpp; i++)
				v |= (uint32_t)p[i] << (8 * i);
			uint8_t* o = &out.rgba[((size_t)y * w + x) * 4];
			if (alpha_only)
			{
				o[0] = o[1] = o[2] = 255;
				o[3] = Channel(v, am ? am : 0xFF);
			}
			else if (luminance)
			{
				o[0] = o[1] = o[2] = Channel(v, rm);
				o[3] = has_alpha ? Channel(v, am) : 255;
			}
			else
			{
				o[0] = Channel(v, rm);
				o[1] = Channel(v, gm);
				o[2] = Channel(v, bm);
				o[3] = has_alpha ? Channel(v, am) : 255;
			}
		}
	return true;
}

} // namespace

bool DecodeImage(const std::string& bytes, DecodedImage& out, std::string& error)
{
	if (bytes.size() >= 4 && memcmp(bytes.data(), "DDS ", 4) == 0)
		return DecodeDDS(bytes, out, error);

	int w = 0, h = 0, n = 0;
	stbi_uc* px = stbi_load_from_memory((const stbi_uc*)bytes.data(), (int)bytes.size(), &w, &h, &n, 4);
	if (!px)
	{
		error = stbi_failure_reason() ? stbi_failure_reason() : "unknown image format";
		return false;
	}
	out.width = w;
	out.height = h;
	out.rgba.assign(px, px + (size_t)w * h * 4);
	stbi_image_free(px);
	return true;
}

void PremultiplyAlpha(DecodedImage& img)
{
	for (size_t i = 0; i + 3 < img.rgba.size(); i += 4)
	{
		unsigned a = img.rgba[i + 3];
		img.rgba[i] = (uint8_t)((img.rgba[i] * a + 127) / 255);
		img.rgba[i + 1] = (uint8_t)((img.rgba[i + 1] * a + 127) / 255);
		img.rgba[i + 2] = (uint8_t)((img.rgba[i + 2] * a + 127) / 255);
	}
}

bool IsGitLfsPointer(const std::string& bytes)
{
	static const char sig[] = "version https://git-lfs.github.com/spec/";
	return bytes.size() < 1024 && bytes.compare(0, sizeof(sig) - 1, sig) == 0;
}
