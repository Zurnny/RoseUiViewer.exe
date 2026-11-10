#include "ViewerRenderer.h"
#include "GameFiles.h"
#include "ImageDecode.h"
#include <RmlUi/Core/Log.h>

// An image that can't be loaded is drawn as nothing (RmlUi would otherwise draw a solid white box).
static Rml::TextureHandle Blank(ViewerRenderer* r, Rml::Vector2i& dims)
{
	static const Rml::byte clear[4] = {0, 0, 0, 0};
	dims = {1, 1};
	return r->GenerateTexture({clear, 4}, dims);
}

Rml::TextureHandle ViewerRenderer::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	std::string bytes;
	fs::path real = GameFiles::Resolve(source);
	if (!GameFiles::ReadAll(real, bytes))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Image not found: %s (it may be packed in the game's data files rather than loose in Data\\ui)",
			source.c_str());
		return Blank(this, texture_dimensions);
	}
	if (IsGitLfsPointer(bytes))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING,
			"%s is a Git LFS placeholder, not the real image. Install Git LFS and run 'git lfs pull' in that repo to get the real files.",
			source.c_str());
		return Blank(this, texture_dimensions);
	}
	DecodedImage img;
	std::string error;
	if (!DecodeImage(bytes, img, error))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Can't read image %s: %s", source.c_str(), error.c_str());
		return Blank(this, texture_dimensions);
	}
	PremultiplyAlpha(img);
	texture_dimensions = {img.width, img.height};
	return GenerateTexture({img.rgba.data(), img.rgba.size()}, texture_dimensions);
}
