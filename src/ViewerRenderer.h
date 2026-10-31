#pragma once
#include "RmlUi_Renderer_GL3.h"

// The stock GL3 renderer only reads TGA. This one also reads DDS, PNG and JPG, through the game file paths.
class ViewerRenderer : public RenderInterface_GL3 {
public:
	Rml::TextureHandle LoadTexture(Rml::Vector2i& texture_dimensions, const Rml::String& source) override;
};
