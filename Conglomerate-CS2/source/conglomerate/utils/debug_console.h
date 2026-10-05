#pragma once

namespace DebugConsole
{
	inline void initialize() {}
	inline void logf(const char* /*format*/, ...) {}
	inline void once(const char* /*key*/, const char* /*format*/, ...) {}
	inline void rateLimited(const char* /*key*/, const char* /*format*/, ...) {}
}
