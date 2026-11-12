// Plugin images tests. The helper in include/plugin_images.h takes the hooks
// as a template parameter, so a fake image table and fake draw calls stand in
// for the loader.
#include <plugin_images.h>

#include <cstdio>

static int g_checks = 0;
static int g_failed = 0;

static void Check(bool ok, const char* what)
{
	++g_checks;
	if (!ok)
	{
		++g_failed;
		printf("    FAIL %s\n", what);
	}
}

namespace
{
	// What the fake draw calls saw.
	struct Seen
	{
		int   dummies = 0;
		int   images  = 0;
		float w = 0.0f, h = 0.0f;
		int   gets    = 0;
		int   states  = 0;
		bool  onScreen = true;
		PluginTextureHandle handle = nullptr;
		int   state   = 0; // PLUGIN_IMAGE_NONE
	} g_seen;

	void Reset() { g_seen = Seen{}; }

	void FakeDummy(float w, float h) { ++g_seen.dummies; g_seen.w = w; g_seen.h = h; }
	bool FakeIsRectVisible(float, float) { return g_seen.onScreen; }
	void FakeImage(PluginTextureHandle, float w, float h) { ++g_seen.images; g_seen.w = w; g_seen.h = h; }
	PluginTextureHandle FakeGet(uint32_t) { ++g_seen.gets; return g_seen.handle; }
	int FakeState(uint32_t) { ++g_seen.states; return g_seen.state; }

	struct FakeImages
	{
		PluginTextureHandle (*Get)(uint32_t);
		int (*State)(uint32_t);
	};

	struct HooksWithTable
	{
		FakeImages*            Images;
		IPluginImGuiTextures*  ImGuiTextures;
	};

	struct HooksWithoutTable
	{
		IPluginImGuiTextures* ImGuiTextures;
	};

	constexpr int kWaiting = 1, kNoImage = 3; // PLUGIN_IMAGE_WAITING, PLUGIN_IMAGE_NO_IMAGE
	const float kSize = 20.0f;
}

int main()
{
	printf("plugin images\n");

	IModLoaderImGui imgui{};
	imgui.Dummy         = &FakeDummy;
	imgui.IsRectVisible = &FakeIsRectVisible;

	IPluginImGuiTextures textures{};
	textures.Image = &FakeImage;

	FakeImages images{ &FakeGet, &FakeState };
	HooksWithTable hooks{ &images, &textures };
	PluginTextureHandle handle = reinterpret_cast<PluginTextureHandle>(static_cast<size_t>(1));

	{
		Reset();
		g_seen.handle = handle;
		PluginImages::Image(&imgui, &hooks, 7, kSize, kSize);
		Check(g_seen.images == 1 && g_seen.dummies == 0, "a ready image is drawn");
		Check(g_seen.w == kSize && g_seen.h == kSize, "the image is drawn at the size asked for");
	}

	{
		Reset();
		g_seen.state = kWaiting;
		PluginImages::Image(&imgui, &hooks, 7, kSize, kSize);
		Check(g_seen.images == 0 && g_seen.dummies == 1, "an image still loading reserves its space");
		Check(g_seen.w == kSize && g_seen.h == kSize, "the reserved space is the size asked for");
	}

	{
		Reset();
		g_seen.onScreen = false;
		g_seen.handle   = handle;
		PluginImages::Image(&imgui, &hooks, 7, kSize, kSize);
		Check(g_seen.gets == 0 && g_seen.states == 0, "an off-screen rectangle does not ask the library");
		Check(g_seen.images == 0 && g_seen.dummies == 1, "an off-screen rectangle reserves its space");
	}

	{
		Reset();
		g_seen.state = kNoImage;
		PluginImages::Image(&imgui, &hooks, 7, kSize, kSize);
		Check(g_seen.dummies == 1 && g_seen.images == 0, "no image reserves its space without CollapseNoImage");
	}

	{
		Reset();
		g_seen.state = kNoImage;
		PluginImages::Image(&imgui, &hooks, 7, kSize, kSize, PluginImages::CollapseNoImage);
		Check(g_seen.dummies == 0 && g_seen.images == 0, "no image takes no space with CollapseNoImage");
	}

	{
		Reset();
		g_seen.state = kWaiting;
		PluginImages::Image(&imgui, &hooks, 7, kSize, kSize, PluginImages::CollapseNoImage);
		Check(g_seen.dummies == 1, "CollapseNoImage keeps the space of an image still loading");
	}

	{
		Reset();
		g_seen.onScreen = false;
		g_seen.state    = kNoImage;
		PluginImages::Image(&imgui, &hooks, 7, kSize, kSize, PluginImages::CollapseNoImage);
		Check(g_seen.dummies == 1, "an off-screen rectangle keeps its space even with CollapseNoImage");
	}

	{
		Reset();
		g_seen.handle = handle;
		HooksWithTable noTextures{ &images, nullptr };
		PluginImages::Image(&imgui, &noTextures, 7, kSize, kSize);
		Check(g_seen.images == 0 && g_seen.dummies == 1, "a missing texture interface reserves the space");
	}

	{
		Reset();
		HooksWithTable nullTable{ nullptr, &textures };
		PluginImages::Image(&imgui, &nullTable, 7, kSize, kSize);
		Check(g_seen.images == 0 && g_seen.dummies == 1, "a null table (server or generic build) reserves the space");
	}

	{
		Reset();
		HooksWithoutTable older{ &textures };
		PluginImages::Image(&imgui, &older, 7, kSize, kSize, PluginImages::CollapseNoImage);
		Check(g_seen.images == 0 && g_seen.dummies == 1, "hooks without the table member reserve the space");
	}

	{
		Reset();
		IPluginHooks real{};
		PluginImages::Image(&imgui, &real, 7, kSize, kSize);
		Check(g_seen.images == 0 && g_seen.dummies == 1, "the SDK hooks type compiles and reserves the space when empty");
	}

	{
		Reset();
		HooksWithTable* none = nullptr;
		PluginImages::Image(&imgui, none, 7, kSize, kSize);
		Check(g_seen.dummies == 1, "null hooks reserve the space");
		PluginImages::Image(static_cast<IModLoaderImGui*>(nullptr), &hooks, 7, kSize, kSize);
		Check(g_seen.dummies == 1 && g_seen.images == 0, "null imgui does nothing");
	}

	printf("%d checks, %d failed\n", g_checks, g_failed);
	return g_failed ? 1 : 0;
}
