// Test plugin for the loader's per-build locator selection (interface 72).
// Run it under sr-aobscan --plugin: each request below exercises one rule of
// memory_scanner/version_map.h, and the expected verdict is in its name.
//
// The two patterns are BetterDrone's FUNCTION_START locators, unique on
// CL-127004 and CL-128786.
#include <windows.h>
#include "plugin_interface.h"

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
}

extern "C" __declspec(dllexport) PluginInfo* GetPluginInfo() { return &g_info; }
extern "C" __declspec(dllexport) bool PluginInit(IPluginSelf*) { return true; }
extern "C" __declspec(dllexport) void PluginShutdown() {}

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
}
