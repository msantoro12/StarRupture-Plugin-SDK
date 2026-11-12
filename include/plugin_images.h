#pragma once

// Draws a game image from the loader's image library, or reserves its space.
//
// Include after plugin_interface.h. Call from a render callback.
//
//   PluginImages::Image(imgui, hooks, id, 20.0f, 20.0f);
//
// id comes from hooks->Images->Item, Path or FromTexture. Each call does the
// same four steps:
//
//   1. If the rectangle is not on screen, it reserves the space and does not
//      ask the library, so rows scrolled out of view never queue a read.
//   2. If the image is ready, it draws it.
//   3. Otherwise it reserves the space, so a row keeps its height while the
//      image loads.
//   4. With CollapseNoImage, an item that has no image takes no space.
//
// Where the loader has no image table (an older loader, a server or generic
// build), this reserves the space and draws nothing. A plugin built against a
// header without hooks->Images compiles the same call and gets the same result.
//
// The image is valid for this frame only. Nothing is kept: ask again on the
// next frame.

#include "plugin_interface.h"

#include <cstdint>

namespace PluginImages
{
	enum Flags : unsigned
	{
		None            = 0,
		// An item the library says has no image takes no space.
		CollapseNoImage = 1u << 0,
	};

	namespace detail
	{
		// The value of PLUGIN_IMAGE_NO_IMAGE.
		constexpr int kNoImage = 3;

		template <class Hooks>
		concept HasImageTable = requires(Hooks* hooks) { hooks->Images; };

		template <class Hooks>
		bool TableIsThere(Hooks* hooks)
		{
			if constexpr (HasImageTable<Hooks>)
				return hooks && hooks->Images;
			else
				return false;
		}
	}

	// Draws image id at the cursor, w by h, or reserves that much space.
	// hooks is the plugin's IPluginHooks; imgui is the render callback's.
	template <class Hooks>
	void Image(IModLoaderImGui* imgui, Hooks* hooks, uint32_t id, float w, float h, unsigned flags = None)
	{
		if (!imgui)
			return;

		if (!detail::TableIsThere(hooks) || !imgui->IsRectVisible(w, h))
		{
			imgui->Dummy(w, h);
			return;
		}

		if constexpr (detail::HasImageTable<Hooks>)
		{
			if (PluginTextureHandle image = hooks->Images->Get(id))
			{
				if (hooks->ImGuiTextures)
				{
					hooks->ImGuiTextures->Image(image, w, h);
					return;
				}
			}
			else if ((flags & CollapseNoImage) && hooks->Images->State(id) == detail::kNoImage)
				return;

			imgui->Dummy(w, h);
		}
	}
}
