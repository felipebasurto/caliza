// Host tests for hostile length fields in the production StateWrapper implementation.
#include "pcsx2/StateWrapper.h"
#include "common/Console.h"

#include <cstdio>
#include <deque>
#include <iostream>
#include <string>
#include <vector>

ConsoleLogWriter<LOGLEVEL_INFO> Console;
ConsoleLogWriter<LOGLEVEL_DEV> DevCon;
#ifndef _DEBUG
NullLogWriter DbgConWriter;
#else
ConsoleLogWriter<LOGLEVEL_DEBUG> DbgConWriter;
#endif

namespace Log
{
	float GetCurrentMessageTime() { return 0.0f; }
	bool IsConsoleOutputEnabled() { return false; }
	void SetConsoleOutputLevel(LOGLEVEL) {}
	bool IsDebugOutputAvailable() { return false; }
	bool IsDebugOutputEnabled() { return false; }
	void SetDebugOutputLevel(LOGLEVEL) {}
	bool IsFileOutputEnabled() { return false; }
	bool SetFileOutputLevel(LOGLEVEL, std::string) { return false; }
	std::FILE* GetFileLogHandle() { return nullptr; }
	bool IsHostOutputEnabled() { return false; }
	void SetHostOutputLevel(LOGLEVEL, HostCallbackType) {}
	bool AreTimestampsEnabled() { return false; }
	void SetTimestampsEnabled(bool) {}
	LOGLEVEL GetMaxLevel() { return LOGLEVEL_NONE; }
	void Write(LOGLEVEL, ConsoleColors, std::string_view) {}
	void Writef(LOGLEVEL, ConsoleColors, const char*, ...) {}
	void Writev(LOGLEVEL, ConsoleColors, const char*, va_list) {}
	void WriteFmtArgs(LOGLEVEL, ConsoleColors, fmt::string_view, fmt::format_args) {}
} // namespace Log

namespace
{
	int failures = 0;

	void expect(bool condition, const char* name)
	{
		if (!condition)
		{
			++failures;
			std::cerr << "FAIL: " << name << '\n';
		}
	}

	void append32(std::vector<u8>& out, u32 value)
	{
		out.push_back(static_cast<u8>(value));
		out.push_back(static_cast<u8>(value >> 8));
		out.push_back(static_cast<u8>(value >> 16));
		out.push_back(static_cast<u8>(value >> 24));
	}

	bool string_roundtrip()
	{
		StateWrapper::VectorMemoryStream write_stream;
		StateWrapper writer(&write_stream, StateWrapper::Mode::Write, 0);
		std::string original = "abc";
		writer.Do(&original);
		if (!writer.IsGood())
			return false;

		StateWrapper::ReadOnlyMemoryStream read_stream(write_stream.GetBuffer().data(), write_stream.GetPosition());
		StateWrapper reader(&read_stream, StateWrapper::Mode::Read, 0);
		std::string loaded;
		reader.Do(&loaded);
		return reader.IsGood() && loaded == "abc";
	}

	bool malicious_string_rejected(u32 length, u32 payload_bytes)
	{
		std::vector<u8> bytes;
		append32(bytes, length);
		bytes.resize(4 + payload_bytes, 'x');
		StateWrapper::ReadOnlyMemoryStream stream(bytes.data(), static_cast<u32>(bytes.size()));
		StateWrapper reader(&stream, StateWrapper::Mode::Read, 0);
		std::string loaded;
		reader.Do(&loaded);
		return reader.HasError();
	}

	bool vector_roundtrip()
	{
		StateWrapper::VectorMemoryStream write_stream;
		StateWrapper writer(&write_stream, StateWrapper::Mode::Write, 0);
		std::vector<u8> original = {1, 2, 3, 4};
		writer.Do(&original);
		if (!writer.IsGood())
			return false;

		StateWrapper::ReadOnlyMemoryStream read_stream(write_stream.GetBuffer().data(), write_stream.GetPosition());
		StateWrapper reader(&read_stream, StateWrapper::Mode::Read, 0);
		std::vector<u8> loaded;
		reader.Do(&loaded);
		return reader.IsGood() && loaded == original;
	}

	bool malicious_vector_rejected()
	{
		std::vector<u8> bytes;
		append32(bytes, 0xffffffffu);
		StateWrapper::ReadOnlyMemoryStream stream(bytes.data(), static_cast<u32>(bytes.size()));
		StateWrapper reader(&stream, StateWrapper::Mode::Read, 0);
		std::vector<u8> loaded;
		reader.Do(&loaded);
		return reader.HasError() && loaded.empty();
	}

	bool wide_vector_exact_size_remains_valid()
	{
		std::vector<u8> bytes;
		append32(bytes, 1);
		bytes.resize(4 + sizeof(u32), 0x2a);
		StateWrapper::ReadOnlyMemoryStream stream(bytes.data(), static_cast<u32>(bytes.size()));
		StateWrapper reader(&stream, StateWrapper::Mode::Read, 0);
		std::vector<u32> loaded;
		reader.Do(&loaded);
		return reader.IsGood() && loaded.size() == 1;
	}

	bool malicious_wide_vector_rejected()
	{
		std::vector<u8> bytes;
		append32(bytes, 2);
		bytes.resize(4 + sizeof(u32), 0x2a);
		StateWrapper::ReadOnlyMemoryStream stream(bytes.data(), static_cast<u32>(bytes.size()));
		StateWrapper reader(&stream, StateWrapper::Mode::Read, 0);
		std::vector<u32> loaded;
		reader.Do(&loaded);
		return reader.HasError() && loaded.empty();
	}

	bool malicious_deque_rejected()
	{
		std::vector<u8> bytes;
		append32(bytes, 0xffffffffu);
		StateWrapper::ReadOnlyMemoryStream stream(bytes.data(), static_cast<u32>(bytes.size()));
		StateWrapper reader(&stream, StateWrapper::Mode::Read, 0);
		std::deque<u8> loaded;
		reader.Do(&loaded);
		return reader.HasError() && loaded.empty();
	}
} // namespace

int main()
{
	expect(string_roundtrip(), "valid string roundtrip");
	expect(!malicious_string_rejected(3, 3), "exact-size string remains valid");
	expect(malicious_string_rejected(4, 3), "string length beyond remaining stream rejected");
	expect(malicious_string_rejected(0xffffffffu, 0), "huge string length rejected");
	expect(vector_roundtrip(), "valid byte vector roundtrip");
	expect(malicious_vector_rejected(), "huge vector length rejected");
	expect(wide_vector_exact_size_remains_valid(), "exact-size wide vector remains valid");
	expect(malicious_wide_vector_rejected(), "wide vector length scaled by element size rejected");
	expect(malicious_deque_rejected(), "huge deque length rejected");

	if (failures == 0)
		std::cout << "statewrapper bounds: PASS\n";
	return failures == 0 ? 0 : 1;
}
