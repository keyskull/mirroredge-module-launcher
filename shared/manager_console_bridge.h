#pragma once

#include <cstddef>

struct ManagerConsoleBridge {
	void (*writeLog)(const char *message);
	void (*clearLog)();
	void (*forEachLogLine)(void (*fn)(const char *line, void *ctx), void *ctx);
	void (*formatStatus)(char *out, size_t outSize);
	void (*formatModules)(char *out, size_t outSize);
	bool (*requestLoad)(const wchar_t *moduleId, const char *source);
	bool (*requestUnload)(const wchar_t *moduleId);
	void (*showMenu)();
	bool (*isMenuOpen)();
	bool (*areHooksInstalled)();
	bool (*isOverlayReady)();
	void (*setBlockInput)(bool block);
};
