#include "ViewerRenderer.h"
#include "GameFiles.h"
#include "ImageDecode.h"
#include <RmlUi/Core/Log.h>

Rml::TextureHandle ViewerRenderer::LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source)
{
	std::string bytes;
	fs::path real = GameFiles::Resolve(source);
	if (!GameFiles::ReadAll(real, bytes))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Image not found: %s", source.c_str());
		return 0;
	}
	DecodedImage img;
	std::string error;
	if (!DecodeImage(bytes, img, error))
	{
		Rml::Log::Message(Rml::Log::LT_WARNING, "Can't read image %s: %s", source.c_str(), error.c_str());
		return 0;
	}
	PremultiplyAlpha(img);
	texture_dimensions = {img.width, img.height};
	return GenerateTexture({img.rgba.data(), img.rgba.size()}, texture_dimensions);
}
