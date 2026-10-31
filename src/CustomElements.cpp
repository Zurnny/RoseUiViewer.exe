#include "CustomElements.h"
#include <RmlUi/Core/ElementInstancer.h>
#include <RmlUi/Core/Element.h>
#include <RmlUi/Core/ElementDocument.h>
#include <RmlUi/Core/Factory.h>
#include <RmlUi/Core/Log.h>
#include <algorithm>
#include <cstdio>
#include <string>
#include <memory>
#include <vector>

namespace {

double NumAttr(Rml::Element* e, const char* name, double fallback)
{
	Rml::Variant* v = e->GetAttribute(name);
	if (!v)
		return fallback;
	Rml::String s = v->Get<Rml::String>();
	try
	{
		return s.empty() ? fallback : std::stod(s);
	} catch (...)
	{
		return fallback;
	}
}

// <gauge gvalue="75" gmax="100"> : the game draws a bar filled to gvalue/gmax.
// Here a child <gauge-fill> gets that width (or height for vertical gauges), so the
// gauge's own decorators show behind it and gauge-fill can be styled in RCSS.
class ElementGauge : public Rml::Element {
public:
	using Rml::Element::Element;

	void OnAttributeChange(const Rml::ElementAttributes& changed) override
	{
		Rml::Element::OnAttributeChange(changed);
		if (changed.count("gvalue") || changed.count("gmax") || changed.count("class"))
			UpdateFill();
	}
	void OnChildAdd(Rml::Element* child) override
	{
		Rml::Element::OnChildAdd(child);
		if (child == this)
			UpdateFill();
	}

private:
	Rml::Element* fill = nullptr;

	void UpdateFill()
	{
		if (!fill)
		{
			Rml::ElementPtr f = Rml::Factory::InstanceElement(this, "gauge-fill", "gauge-fill", Rml::XMLAttributes());
			if (!f)
				return;
			fill = AppendChild(std::move(f), false);
			fill->SetProperty("position", "absolute");
			fill->SetProperty("left", "0px");
			fill->SetProperty("bottom", "0px");
			fill->SetProperty("pointer-events", "none");
		}
		const double max = NumAttr(this, "gmax", 100.0);
		const double val = NumAttr(this, "gvalue", 0.0);
		const double pct = max > 0 ? std::clamp(val / max, 0.0, 1.0) * 100.0 : 0.0;
		const bool vertical = IsClassSet("vertical") || GetClassNames().find("vertical") != Rml::String::npos;
		char buf[32];
		snprintf(buf, sizeof(buf), "%.2f%%", pct);
		if (vertical)
		{
			fill->SetProperty("width", "100%");
			fill->SetProperty("height", buf);
		}
		else
		{
			fill->SetProperty("height", "100%");
			fill->SetProperty("width", buf);
		}
	}
};

// <item-slot item-key="..." item-id="..."> : the game looks the item up in its data files and draws its icon.
// The viewer can't read those files, so it marks a filled slot with the class "viewer-has-item"
// (the viewer stylesheet draws a placeholder for it), or shows the sprite named in item-sprite="".
class ElementItemSlot : public Rml::Element {
public:
	using Rml::Element::Element;

	void OnAttributeChange(const Rml::ElementAttributes& changed) override
	{
		Rml::Element::OnAttributeChange(changed);
		auto has = [&](const char* n) {
			Rml::Variant* v = GetAttribute(n);
			if (!v)
				return false;
			Rml::String s = v->Get<Rml::String>();
			return !s.empty() && s != "0";
		};
		const bool filled = has("item-key") || has("item-id");
		SetClass("viewer-has-item", filled);
		Rml::Variant* sprite = GetAttribute("item-sprite");
		if (sprite)
			SetProperty("decorator", "image(" + sprite->Get<Rml::String>() + " scale-none)");
	}
};

std::vector<std::unique_ptr<Rml::ElementInstancer>> g_instancers;

} // namespace

void RegisterCustomElements()
{
	auto gauge = std::make_unique<Rml::ElementInstancerGeneric<ElementGauge>>();
	Rml::Factory::RegisterElementInstancer("gauge", gauge.get());
	g_instancers.push_back(std::move(gauge));

	auto slot = std::make_unique<Rml::ElementInstancerGeneric<ElementItemSlot>>();
	Rml::Factory::RegisterElementInstancer("item-slot", slot.get());
	Rml::Factory::RegisterElementInstancer("quickbar-slot", slot.get());
	g_instancers.push_back(std::move(slot));
}
