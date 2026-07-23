// me_version.cpp — Game version detection implementation.
#include "me_version.h"

#include <windows.h>
#include <cstdint>
#include <cstring>
#include <vector>

namespace MeVersion {

namespace {
	VersionInfo g_versionInfo;
	bool g_detected = false;

	// Read VS_FIXEDFILEINFO from the EXE's version resource.
	void ReadFileVersion(VersionInfo &out) {
		const auto baseHmod = GetModuleHandleW(nullptr);
		wchar_t exePath[MAX_PATH];
		if (!GetModuleFileNameW(baseHmod, exePath, MAX_PATH))
			return;

		DWORD handle = 0;
		const DWORD size = GetFileVersionInfoSizeW(exePath, &handle);
		if (!size)
			return;

		std::vector<uint8_t> buf(size);
		if (!GetFileVersionInfoW(exePath, 0, size, buf.data()))
			return;

		VS_FIXEDFILEINFO *ffi = nullptr;
		UINT ffiLen = 0;
		if (VerQueryValueW(buf.data(), L"\\",
		                   reinterpret_cast<void **>(&ffi), &ffiLen) && ffi) {
			out.fileVersion[0] = HIWORD(ffi->dwFileVersionMS);
			out.fileVersion[1] = LOWORD(ffi->dwFileVersionMS);
			out.fileVersion[2] = HIWORD(ffi->dwFileVersionLS);
			out.fileVersion[3] = LOWORD(ffi->dwFileVersionLS);
		}
	}
}

Build Current() {
	return g_detected ? g_versionInfo.build : Build::Unknown;
}

const char *Label(Build build) {
	switch (build) {
	case Build::ME_1_0_Retail:  return "ME 1.0 Retail";
	case Build::ME_1_0_Steam:   return "ME 1.0 Steam";
	case Build::ME_1_0_EA_App:  return "ME 1.0 EA App";
	case Build::ME_1_0_GOG:     return "ME 1.0 GOG";
	default:                     return "Unknown";
	}
}

bool Detect(VersionInfo &out) {
	if (g_detected) {
		out = g_versionInfo;
		return out.build != Build::Unknown;
	}

	// Get the EXE module base
	const auto base = reinterpret_cast<const uint8_t *>(
		GetModuleHandleW(nullptr));
	if (!base)
		return false;

	// Read PE header
	const auto dos = reinterpret_cast<const IMAGE_DOS_HEADER *>(base);
	if (dos->e_magic != IMAGE_DOS_SIGNATURE)
		return false;

	const auto nt = reinterpret_cast<const IMAGE_NT_HEADERS *>(
		base + dos->e_lfanew);
	if (nt->Signature != IMAGE_NT_SIGNATURE)
		return false;

	// Extract PE metadata
	out.exeTimestamp = nt->FileHeader.TimeDateStamp;
	out.exeFileSize  = nt->OptionalHeader.SizeOfImage;

	// Extract file version from VS_VERSIONINFO resource
	ReadFileVersion(out);

	// --- Heuristic version detection ---
	//
	// Known ME 1.0 builds can be distinguished by:
	//   - EXE SizeOfImage (varies with OOA encryption, Steam stub, etc.)
	//   - PE timestamp (different build dates)
	//   - File version resource (if present)
	//
	// These are approximations; refine as more builds are tested.

	const uint32_t size = out.exeFileSize;
	const uint32_t ts   = out.exeTimestamp;

	// Standard retail: ~12-14 MB, timestamp around 2008-11
	if (size >= 0xC00000 && size <= 0xE00000 &&
	    ts   >= 0x49000000 && ts <= 0x49400000) {
		out.build = Build::ME_1_0_Retail;
	}
	// Steam: may have Steam stub wrapper, larger image
	else if (size > 0xE00000 && size <= 0x1200000) {
		out.build = Build::ME_1_0_Steam;
	}
	// EA App / Origin: OOA-encrypted, typically larger after decryption
	else if (size > 0x1200000) {
		out.build = Build::ME_1_0_EA_App;
	}
	// GOG: may have different timestamp range
	else if (ts > 0x51000000) {
		out.build = Build::ME_1_0_GOG;
	}
	else {
		out.build = Build::Unknown;
	}

	// Build the label
	char labelBuf[128];
	snprintf(labelBuf, sizeof(labelBuf),
	         "%s (Size=%08X TS=%08X Ver=%u.%u.%u.%u)",
	         Label(out.build),
	         out.exeFileSize, out.exeTimestamp,
	         out.fileVersion[0], out.fileVersion[1],
	         out.fileVersion[2], out.fileVersion[3]);
	out.buildString = labelBuf;

	g_versionInfo = out;
	g_detected = true;

	return out.build != Build::Unknown;
}

} // namespace MeVersion
