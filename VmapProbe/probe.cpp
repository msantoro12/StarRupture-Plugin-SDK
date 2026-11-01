// Test plugin for the loader's per-build locator selection (interface 72),
// field resolution, Game-SDK globals (GetSdkGlobals, in PluginInit) and
// IsGameThread (false in PluginInit at startup, true in the first tick).
// Run it under sr-aobscan --plugin: each request below exercises one rule of
// memory_scanner/version_map.h, and the expected verdict is in its name. Map
// keys and fields go through the SDK's plugin_map.h helpers, the probe's own
// map through plugin_version_map.h.
//
// The two patterns are BetterDrone's FUNCTION_START locators, unique on
// CL-127004 and CL-128786.
#include <windows.h>
#include <atomic>
#include "plugin_interface.h"
#include "Basic.hpp"
#include "CoreUObject_classes.hpp"
#include "Chimera_classes.hpp"
#include "version_map.generated.h"
#include "plugin_version_map.h"

namespace
{
	const char* const kP1 = "48 8B C4 48 89 50 10 48 89 48 08 48 83 EC ?? 48 89 58 18 48 89 68 F8 40 32 ED 48 89 70 F0 40 32 F6";
	const char* const kP2 = "48 89 5C 24 ?? 55 56 57 48 83 EC ?? 48 8B D9 48 8B 89";
	const char* const kNone = "CC CC CC CC CC CC CC CC 0F 0B 0F 0B 0F 0B CC CC CC CC";

	// Two real changelists, so one row is exact on either exe.
	const uint32_t kCLs[] = { 127004, 128786 };

	PluginInfo g_info = { "VmapProbe", "1.0", "test", "Per-build locator selection probe",
		PLUGIN_INTERFACE_VERSION, PLUGIN_TARGET_CLIENT };

	uintptr_t Ask(IPluginSelf* self, IPluginHookScanner* scan, const char* name, const char* pattern,
		const PluginScanCandidate* rows, uint32_t count)
	{
		PluginScanRequest req = PLUGIN_SCAN_REQUEST_INIT;
		req.hookName       = name;
		req.pattern        = pattern;
		req.kind           = PLUGIN_SCAN_FUNCTION_START;
		req.flags          = PLUGIN_SCAN_FLAG_OPTIONAL;
		req.candidates     = rows;
		req.candidateCount = count;
		return scan->Resolve(self, &req);
	}

	PluginScanCandidate Row(uint32_t cl, const char* pattern, uint32_t stamp = 0, uint32_t size = 0, uint32_t rva = 0)
	{
		return { cl, stamp, size, pattern, 0, 0, -1, rva };
	}

	// Field requests (ResolveField). Offsets are the PDB's on each build.
	bool Field(IPluginSelf* self, IPluginHookScanner* scan, const char* key, const char* className,
		const char* member, uint32_t flags, uint32_t size, int32_t builtOffset,
		const PluginFieldCandidate* rows, uint32_t count)
	{
		PluginFieldSpec field{ key, className, member, flags, size, builtOffset };
		return PluginMap::ResolveField(scan, self, field, rows, count, kCLs[0]);
	}

	// IsGameThread, once in PluginInit and once in the first tick. The
	// harness hands PluginInit a table with GetSdkGlobals alone, so both
	// slots are checked before use.
	IPluginSelf*      g_self = nullptr;
	std::atomic<bool> g_tickLogged{ false };

	// GWorld through PluginMap::Global, compared in PluginInit with the
	// loader's GetSdkGlobals.
	SDK::UWorld**     g_gworld = nullptr;

	void LogGameThread(const char* where)
	{
		const IPluginEngineEvents* engine = g_self->hooks ? g_self->hooks->Engine : nullptr;
		if (engine && engine->IsGameThread)
			g_self->logger->Info(g_self, "IsGameThread in %s: %s", where, engine->IsGameThread() ? "true" : "false");
	}

	void OnTick(float)
	{
		if (!g_tickLogged.exchange(true))
			LogGameThread("the first tick");
	}
}

extern "C" __declspec(dllexport) PluginInfo* GetPluginInfo() { return &g_info; }
// Through plugin_version_map.h, with this probe's own map (version_map.generated.h).
PluginField<SDK::UCrCraftingComponent*> g_craftComponent = SR_FIELD(SDK::UCrCraftingComponent*, ACrCrafter, CraftComponent, 0);
// Through plugin_map.h alone: the loader's map and the built offset.
PluginField<SDK::UCrCraftingComponent*> g_craftComponentMap = SR_FIELD(SDK::UCrCraftingComponent*, ACrCrafter, CraftComponent, 0);

// The Game-SDK globals: the loader's, then this module's SDK after Init.
extern "C" __declspec(dllexport) bool PluginInit(IPluginSelf* self)
{
	const bool haveGlobals = PluginSdkRuntime::Init(self->hooks);
	g_self = self;
	LogGameThread("PluginInit");
	if (self->hooks->Engine->RegisterOnTick)
		self->hooks->Engine->RegisterOnTick(OnTick);
	if (!haveGlobals)
	{
		self->logger->Warn(self, "Globals: none from the loader; the SDK keeps its Offsets::");
		return true;
	}
	const PluginSdkGlobals* g = PluginSdkRuntime::Globals();
	const uintptr_t base = SDK::InSDKUtils::GetImageBase();
	const auto rva = [base](uintptr_t a) { return static_cast<unsigned long long>(a ? a - base : 0); };
	const uintptr_t sdkObjects = reinterpret_cast<uintptr_t>(SDK::UObject::GObjects.GetTypedPtr());
	const uintptr_t sdkAppend  = reinterpret_cast<uintptr_t>(SDK::FName::AppendString);
	self->logger->Info(self, "Globals: GObjects 0x%llX, GNames 0x%llX, AppendString 0x%llX, GWorld 0x%llX",
		rva(g->gobjects), rva(g->gnames), rva(g->appendString), rva(g->gworld));
	self->logger->Info(self, "Globals: after InitManually the SDK's GObjects 0x%llX (%s), AppendString 0x%llX (%s)",
		rva(sdkObjects), sdkObjects == g->gobjects ? "equal" : "DIFFERENT",
		rva(sdkAppend), sdkAppend == g->appendString ? "equal" : "DIFFERENT");
	self->logger->Info(self, "Globals: GWorld 0x%llX, the SDK's Offsets::GWorld 0x%X (%s); on the SDK's own build: %s",
		rva(g->gworld), SDK::Offsets::GWorld, rva(g->gworld) == static_cast<unsigned long long>(SDK::Offsets::GWorld) ? "same" : "different",
		PluginSdkRuntime::OnSdkBuild() ? "yes" : "no");
	const uintptr_t mapWorld = reinterpret_cast<uintptr_t>(g_gworld);
	self->logger->Info(self, "Helper: GWorld 0x%llX through the map (%s)",
		rva(mapWorld), mapWorld == g->gworld ? "same as GetSdkGlobals" : "DIFFERENT");
	return true;
}
extern "C" __declspec(dllexport) void PluginShutdown()
{
	if (g_self && g_self->hooks && g_self->hooks->Engine && g_self->hooks->Engine->UnregisterOnTick)
		g_self->hooks->Engine->UnregisterOnTick(OnTick);
}

extern "C" __declspec(dllexport) void OnPluginLoadHooks(IPluginSelf* self, IPluginHookScanner* scan)
{
	const auto base = reinterpret_cast<const uint8_t*>(GetModuleHandleW(nullptr));
	const auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS64*>(base + reinterpret_cast<const IMAGE_DOS_HEADER*>(base)->e_lfanew);
	const uint32_t stamp = nt->FileHeader.TimeDateStamp;
	const uint32_t size  = nt->OptionalHeader.SizeOfImage;

	// The plain request, no candidates: the address the rows below must find.
	const uintptr_t p1 = Ask(self, scan, "Probe::Base", kP1, nullptr, 0);
	const uint32_t rva = p1 ? static_cast<uint32_t>(p1 - reinterpret_cast<uintptr_t>(base)) : 0;

	// 1. Exact: the built pattern is gone; the row for the running CL finds it.
	{
		const PluginScanCandidate rows[] = { Row(kCLs[0], kP1), Row(kCLs[1], kP1) };
		Ask(self, scan, "Probe::Exact", kNone, rows, 2);
	}
	// 1. Exact row's pattern missing: logged, then agreement with the built pattern.
	{
		const PluginScanCandidate rows[] = { Row(kCLs[0], kNone), Row(kCLs[1], kNone) };
		Ask(self, scan, "Probe::ExactMiss", kP1, rows, 2);
	}
	// 2. Agreement: no row for the running CL; the ones that resolve agree.
	{
		const PluginScanCandidate rows[] = { Row(1, kP1), Row(2, kNone) };
		Ask(self, scan, "Probe::Agree", kNone, rows, 2);
	}
	// 2. Agreement fails: two locators resolve to different functions. AMBIGUOUS.
	{
		const PluginScanCandidate rows[] = { Row(1, kP2) };
		Ask(self, scan, "Probe::Ambiguous", kP1, rows, 1);
	}
	// 1. Exact on the very exe: lands on the recorded RVA.
	{
		const PluginScanCandidate rows[] = { Row(kCLs[0], kP1, stamp, size, rva), Row(kCLs[1], kP1, stamp, size, rva) };
		Ask(self, scan, "Probe::RvaOk", kNone, rows, 2);
	}
	// 1. Exact on the very exe, different RVA: refused, nothing else tried. WRONGKIND.
	{
		const PluginScanCandidate rows[] = { Row(kCLs[0], kP1, stamp, size, rva + 0x10), Row(kCLs[1], kP1, stamp, size, rva + 0x10) };
		Ask(self, scan, "Probe::RvaMismatch", kP1, rows, 2);
	}
	// 1. Address-only row on the very exe.
	{
		const PluginScanCandidate rows[] = { Row(kCLs[0], nullptr, stamp, size, rva), Row(kCLs[1], nullptr, stamp, size, rva) };
		Ask(self, scan, "Probe::AddressOnly", kNone, rows, 2);
	}

	// Map keys, against the loader's own map, through plugin_map.h.
	const uint32_t optional = PLUGIN_SCAN_FLAG_OPTIONAL;
	// A map key with rows of the probe's own: PluginMap::Request plus candidates.
	const auto withRows = [&](const char* name, const PluginScanCandidate* rows, uint32_t count) -> uintptr_t
	{
		PluginScanRequest req = PluginMap::Request(name, nullptr, PLUGIN_SCAN_FUNCTION_START, optional);
		req.candidates     = rows;
		req.candidateCount = count;
		return scan->Resolve(self, &req);
	};
	{
		// A global key only: the map's row for the running build.
		const auto beginPlay = PluginMap::FunctionAs<void (*)(SDK::UWorld*)>(scan, self, "UWorld::BeginPlay", nullptr,
			PLUGIN_SCAN_FUNCTION_START, optional);
		// An own row for this very exe: agrees with the map's pick, or AMBIGUOUS.
		const uintptr_t beginPlayAt = reinterpret_cast<uintptr_t>(beginPlay);
		const uint32_t at = beginPlayAt ? static_cast<uint32_t>(beginPlayAt - reinterpret_cast<uintptr_t>(base)) : 0;
		const PluginScanCandidate same[] = { Row(kCLs[0], nullptr, stamp, size, at), Row(kCLs[1], nullptr, stamp, size, at) };
		withRows("UWorld::BeginPlay", same, 2);
		const PluginScanCandidate off[] = { Row(kCLs[0], nullptr, stamp, size, at + 0x10), Row(kCLs[1], nullptr, stamp, size, at + 0x10) };
		withRows("UWorld::BeginPlay", off, 2);
		// The map holds GWorld as DATA: asked for as a function, set aside, the
		// own pattern decides; as a global, the map's row.
		PluginMap::Function(scan, self, "GWorld", kP1, PLUGIN_SCAN_FUNCTION_START, optional);
		g_gworld = PluginMap::Global<SDK::UWorld*>(scan, self, "GWorld");
		// Not in the map: the own pattern decides; with nothing of its own, refused.
		PluginMap::Function(scan, self, "Probe::MapKeyOwnPattern", kP1, PLUGIN_SCAN_FUNCTION_START, optional);
		PluginMap::Function(scan, self, "Probe::MapKeyOnlyRefused", nullptr, PLUGIN_SCAN_FUNCTION_START, optional);
		// Own rows and no pattern: the rows decide.
		const PluginScanCandidate rows[] = { Row(kCLs[0], kP1), Row(kCLs[1], kP1) };
		withRows("Probe::MapKeyRowsOnly", rows, 2);
		// A site is this plugin's own key: the loader's ModLoader/ site of the
		// same name is not it, and a site with no pattern is refused.
		PluginMap::Site(scan, self, "HandleCrashInternal_FatalReportCallSite", kP1, PLUGIN_SCAN_IN_FUNCTION, optional);
		PluginMap::Site(scan, self, "Probe::SiteOnlyRefused", nullptr, PLUGIN_SCAN_IN_FUNCTION, optional);
		// The fingerprint vectors: the [Map] line shows fp 0xB3405899 (an
		// offset to follow at, without the flag, is not part of it), then
		// 0x2D1AC6AE with the rel32 follow.
		PluginMap::Site(scan, self, "Probe::SiteFp", "48 8b 05 ? ??  ?? ??", PLUGIN_SCAN_IN_FUNCTION, optional, -4, 3);
		PluginMap::Site(scan, self, "Probe::SiteFpFollow", "48 8b 05 ? ??  ?? ??", PLUGIN_SCAN_IN_FUNCTION,
			optional | PLUGIN_SCAN_FLAG_FOLLOW_REL32, -4, 3);
	}

	// Fields. ACrCrafter::CraftComponent moved (0x6D8 on CL-127004, 0x6E0 on
	// CL-128786); UCrUW_MainMenuWidget::ButtonsIndexes too (0x670, 0x698).
	const PluginFieldCandidate craft[] = { { kCLs[0], 0x6D8, 8, 0, nullptr }, { kCLs[1], 0x6E0, 8, 0, nullptr } };
	// Reflected, with a row per build that agrees: the running build's row.
	Field(self, scan, "Field::RowsAgree", "ACrCrafter", "CraftComponent", 0, 8, 0x6D8, craft, 2);
	// Reflected, no rows: the reflected offset.
	Field(self, scan, "Field::NoRow", "UCrUW_MainMenuWidget", "ButtonsIndexes", 0, 0x50, 0x670, nullptr, 0);
	// Reflected, by /Script/ path.
	Field(self, scan, "Field::ByPath", "/Script/Chimera.ACrCrafter", "CraftComponent", 0, 8, 0x6D8, craft, 2);
	// A whole bool, not a bitfield: mask 0xFF.
	Field(self, scan, "Field::Bool", "ACrBuildingActorBase", "bCanRename", 0, 1, 0x486, nullptr, 0);
	// A size row.
	Field(self, scan, "Field::Size", "ACrCrafter", nullptr, 0, 0x758, -1, nullptr, 0);
	// Fails: the running build's row disagrees with reflection.
	{
		const PluginFieldCandidate stale[] = { { kCLs[0], 0x6D0, 8, 0, nullptr }, { kCLs[1], 0x6D8, 8, 0, nullptr } };
		Field(self, scan, "Field::StaleRow_Fails", "ACrCrafter", "CraftComponent", 0, 8, 0x6D8, stale, 2);
	}
	// Fails: compiled for another size.
	Field(self, scan, "Field::WrongSize_Fails", "ACrCrafter", "CraftComponent", 0, 4, 0x6D8, craft, 2);
	// Fails: no such member.
	Field(self, scan, "Field::NoMember_Fails", "ACrCrafter", "NoSuchMember", 0, 8, 0x10, nullptr, 0);
	// CHECK: passes only where the compiled offset (CL-127004's) still holds.
	Field(self, scan, "Field::Check_FailsOn128786", "ACrCrafter", "CraftComponent", PLUGIN_FIELD_CHECK, 8, 0x6D8, craft, 2);
	Field(self, scan, "Field::CheckSize_FailsOn128786", "ACrCrafter", nullptr, PLUGIN_FIELD_CHECK, 0x758, -1, nullptr, 0);
	// NATIVE: the running build's row; with no row, the compiled offset on
	// the build it was compiled for and a failure anywhere else.
	{
		const PluginFieldCandidate both[] = { { kCLs[0], 0x2C0, 8, 0, nullptr }, { kCLs[1], 0x2C8, 8, 0, nullptr } };
		Field(self, scan, "Field::Native", "ACrPackageTransportReplicator", "NativeMember", PLUGIN_FIELD_NATIVE, 8, 0x2C0, both, 2);
		const PluginFieldCandidate built[] = { { kCLs[0], 0x2C0, 8, 0, nullptr } };
		Field(self, scan, "Field::NativeNoRow_FailsOn128786", "ACrPackageTransportReplicator", "NativeMember", PLUGIN_FIELD_NATIVE, 8, 0x2C0, built, 1);
	}

	// The loader map's rows come first. A native member only the map holds:
	// its row on both builds, with no row of the probe's.
	Field(self, scan, "ACrPackageTransportReplicator::SendingTime", "ACrPackageTransportReplicator", "SendingTime", PLUGIN_FIELD_NATIVE, 4, 0x2C0, nullptr, 0);
	// Fails: the probe's own row disagrees with the map's for that build.
	{
		const PluginFieldCandidate wrong[] = { { kCLs[0], 0x40, 8, 0, nullptr } };
		Field(self, scan, "UWorld::NetDriver", "UWorld", "NetDriver", 0, 8, 0x38, wrong, 1);
	}
	// The map's key names another member: its rows are set aside, and
	// reflection answers.
	Field(self, scan, "UWorld::GameState", "UWorld", "LineBatchers", 0, 0x20, 0xF0, nullptr, 0);
	// Fails: a native offset past the end of its class (0x5C8 bytes).
	{
		const PluginFieldCandidate past[] = { { kCLs[0], 0x5C4, 8, 0, nullptr }, { kCLs[1], 0x5C4, 8, 0, nullptr } };
		Field(self, scan, "Field::NativePastClass_Fails", "ACrPackageTransportReplicator", "NativeMember", PLUGIN_FIELD_NATIVE, 8, 0x5C4, past, 2);
	}

	// The same member through the helpers: with the probe's own map, then
	// with the loader's map alone.
	if (PluginVersionMap::ResolveFields(scan, self, g_craftComponent))
		self->logger->Info(self, "Helper: %s at 0x%X", g_craftComponent.key, g_craftComponent.offset);
	if (PluginMap::ResolveFields(scan, self, g_craftComponentMap))
		self->logger->Info(self, "Helper: %s at 0x%X, the loader's map alone", g_craftComponentMap.key, g_craftComponentMap.offset);
}
