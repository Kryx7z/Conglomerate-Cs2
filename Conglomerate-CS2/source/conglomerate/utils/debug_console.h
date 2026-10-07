#pragma once

#if defined(CONGLOMERATE_DEBUG_CONSOLE)
#include <Windows.h>
#include <cstddef>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <mutex>

namespace DebugConsole
{
	inline constexpr std::size_t kEntryCapacity = 96;
	inline constexpr ULONGLONG kRepeatWindowMs = 5000;

	struct Entry
	{
		const char* key = nullptr;
		ULONGLONG lastOutput = 0;
		unsigned int suppressed = 0;
		bool seen = false;
	};

	inline std::mutex g_mutex;
	inline Entry g_entries[kEntryCapacity]{};
	inline bool g_attached = false;

	inline Entry* findEntry(const char* key)
	{
		Entry* empty = nullptr;
		for (Entry& entry : g_entries)
		{
			if (entry.key && std::strcmp(entry.key, key) == 0)
				return &entry;
			if (!entry.key && !empty)
				empty = &entry;
		}

		if (empty)
			empty->key = key;
		return empty;
	}

	inline void emitLocked(const char* message)
	{
		SYSTEMTIME now{};
		GetLocalTime(&now);

		char line[1280]{};
		_snprintf_s(line, sizeof(line), _TRUNCATE,
			"[%02u:%02u:%02u.%03u] %s\n",
			static_cast<unsigned int>(now.wHour), static_cast<unsigned int>(now.wMinute),
			static_cast<unsigned int>(now.wSecond), static_cast<unsigned int>(now.wMilliseconds), message);

		OutputDebugStringA(line);
		if (g_attached && stdout)
		{
			std::fputs(line, stdout);
			std::fflush(stdout);
		}
	}

	inline void initialize()
	{
		std::lock_guard<std::mutex> lock(g_mutex);
		if (g_attached)
			return;

		if (!GetConsoleWindow() && !AllocConsole())
		{
			emitLocked("[debug] AllocConsole failed; continuing with OutputDebugString only");
			return;
		}

		SetConsoleTitleA("Conglomerate Debug Console");
		FILE* stdoutStream = nullptr;
		FILE* stderrStream = nullptr;
		const int stdoutResult = freopen_s(&stdoutStream, "CONOUT$", "w", stdout);
		const int stderrResult = freopen_s(&stderrStream, "CONOUT$", "w", stderr);
		g_attached = stdoutResult == 0;
		if (g_attached)
			setvbuf(stdout, nullptr, _IONBF, 0);
		if (stderrResult == 0)
			setvbuf(stderr, nullptr, _IONBF, 0);
		if (!g_attached)
		{
			char error[160]{};
			_snprintf_s(error, sizeof(error), _TRUNCATE,
				"[debug] console stdout redirect failed (code=%d); using OutputDebugString only", stdoutResult);
			emitLocked(error);
			return;
		}
		emitLocked("[debug] console attached");
	}

	inline void logf(const char* format, ...)
	{
		char message[1024]{};
		va_list args;
		va_start(args, format);
		_vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
		va_end(args);

		std::lock_guard<std::mutex> lock(g_mutex);
		emitLocked(message);
	}

	inline void once(const char* key, const char* format, ...)
	{
		char message[1024]{};
		va_list args;
		va_start(args, format);
		_vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
		va_end(args);

		std::lock_guard<std::mutex> lock(g_mutex);
		Entry* entry = findEntry(key);
		if (!entry || entry->seen)
			return;
		entry->seen = true;
		entry->lastOutput = GetTickCount64();
		emitLocked(message);
	}

	inline void rateLimited(const char* key, const char* format, ...)
	{
		char message[1024]{};
		va_list args;
		va_start(args, format);
		_vsnprintf_s(message, sizeof(message), _TRUNCATE, format, args);
		va_end(args);

		std::lock_guard<std::mutex> lock(g_mutex);
		Entry* entry = findEntry(key);
		if (!entry)
		{
			emitLocked(message);
			return;
		}

		const ULONGLONG now = GetTickCount64();
		if (entry->seen && now - entry->lastOutput < kRepeatWindowMs)
		{
			++entry->suppressed;
			return;
		}

		if (entry->seen && entry->suppressed)
		{
			char summary[1200]{};
			_snprintf_s(summary, sizeof(summary), _TRUNCATE,
				"%s (suppressed %u repeats)", message, entry->suppressed);
			emitLocked(summary);
		}
		else
		{
			emitLocked(message);
		}

		entry->seen = true;
		entry->lastOutput = now;
		entry->suppressed = 0;
	}
}
#else
namespace DebugConsole
{
	inline void initialize() {}
	inline void logf(const char*, ...) {}
	inline void once(const char*, const char*, ...) {}
	inline void rateLimited(const char*, const char*, ...) {}
}
#endif
