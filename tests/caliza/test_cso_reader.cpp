// SPDX-FileCopyrightText: 2026 Caliza contributors
// SPDX-License-Identifier: GPL-3.0-or-later

#include "CDVD/CsoFileReader.h"
#include "common/Console.h"
#include "common/Error.h"
#include "common/FileSystem.h"
#include "lz4.h"

#include <zlib.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace
{
std::atomic<int> g_fseek_calls{0};
int g_failures = 0;

void Check(bool ok, const char* msg)
{
	if (ok)
	{
		std::printf("  ok    %s\n", msg);
	}
	else
	{
		std::printf("  FAIL  %s\n", msg);
		++g_failures;
	}
}
} // namespace

// Minimal host definitions for symbols referenced by CsoFileReader.cpp.
ConsoleLogWriter<LOGLEVEL_INFO> Console;

void Log::Write(LOGLEVEL, ConsoleColors, std::string_view) {}
void Log::Writef(LOGLEVEL, ConsoleColors, const char*, ...) {}
void Log::Writev(LOGLEVEL, ConsoleColors, const char*, va_list) {}
void Log::WriteFmtArgs(LOGLEVEL, ConsoleColors, fmt::string_view, fmt::format_args) {}

Error::Error() = default;
Error::~Error() = default;

void Error::SetString(Error* error, std::string message)
{
	if (error)
		error->SetString(std::move(message));
}

void Error::SetString(std::string message)
{
	m_type = Type::User;
	m_description = std::move(message);
}

std::FILE* FileSystem::OpenCFile(const char* filename, const char* mode, Error* error)
{
	std::FILE* fp = std::fopen(filename, mode);
	if (!fp && error)
		error->SetString(std::string("fopen failed: ") + filename);
	return fp;
}

size_t FileSystem::ReadFileWithProgress(std::FILE*, void*, size_t, ProgressCallback*, Error*, size_t)
{
	return 0;
}

int FileSystem::FSeek64(std::FILE* fp, s64 offset, int whence)
{
	g_fseek_calls.fetch_add(1, std::memory_order_relaxed);
	return std::fseek(fp, static_cast<long>(offset), whence);
}

s64 FileSystem::FSize64(std::FILE* fp)
{
	const long cur = std::ftell(fp);
	if (cur < 0)
		return -1;
	if (std::fseek(fp, 0, SEEK_END) != 0)
		return -1;
	const long end = std::ftell(fp);
	std::fseek(fp, cur, SEEK_SET);
	return static_cast<s64>(end);
}

ThreadedFileReader::ThreadedFileReader() = default;
ThreadedFileReader::~ThreadedFileReader() = default;
bool ThreadedFileReader::Open(std::string filename, Error* error) { return Open2(std::move(filename), error); }
void ThreadedFileReader::Close() { Close2(); }
int ThreadedFileReader::ReadSync(void* pBuffer, u32 sector, u32 count)
{
	if (count != 1)
		return -1;
	const Chunk c = ChunkForOffset(static_cast<u64>(sector) * m_blocksize);
	if (c.chunkID < 0)
		return -1;
	return ReadChunk(pBuffer, c.chunkID);
}
bool ThreadedFileReader::Precache2(ProgressCallback*, Error*) { return false; }
bool ThreadedFileReader::CheckAvailableMemoryForPrecaching(u64, Error*) { return false; }

namespace
{
void WriteLe32(std::vector<u8>& out, size_t off, u32 v)
{
	out[off + 0] = static_cast<u8>(v & 0xFFu);
	out[off + 1] = static_cast<u8>((v >> 8) & 0xFFu);
	out[off + 2] = static_cast<u8>((v >> 16) & 0xFFu);
	out[off + 3] = static_cast<u8>((v >> 24) & 0xFFu);
}

void WriteLe64(std::vector<u8>& out, size_t off, u64 v)
{
	WriteLe32(out, off, static_cast<u32>(v & 0xFFFFFFFFu));
	WriteLe32(out, off + 4, static_cast<u32>(v >> 32));
}

bool WriteSyntheticCompressedImage(const std::string& path,
	bool use_lz4,
	const std::vector<std::vector<u8>>& frames,
	u32 frame_size,
	int uncompressed_frame_index,
	int corrupt_frame_index)
{
	const u32 num_frames = static_cast<u32>(frames.size());
	const size_t header_size = 24;
	const size_t index_bytes = static_cast<size_t>(num_frames + 1) * 4;
	std::vector<u8> file(header_size + index_bytes, 0);

	std::memcpy(file.data(), use_lz4 ? "ZISO" : "CISO", 4);
	WriteLe32(file, 4, 24);
	WriteLe64(file, 8, static_cast<u64>(num_frames) * frame_size);
	WriteLe32(file, 16, frame_size);
	file[20] = 1; // version 1
	file[21] = 0; // align shift 0 (contiguous bytes)

	for (u32 i = 0; i < num_frames; ++i)
	{
		const u32 pos = static_cast<u32>(file.size());
		if (static_cast<int>(i) == uncompressed_frame_index)
		{
			WriteLe32(file, header_size + static_cast<size_t>(i) * 4, pos | 0x80000000u);
			file.insert(file.end(), frames[i].begin(), frames[i].end());
			continue;
		}

		WriteLe32(file, header_size + static_cast<size_t>(i) * 4, pos);
		if (static_cast<int>(i) == corrupt_frame_index)
		{
			const u8 garbage[16] = {0xFF, 0xFE, 0xFD, 0xFC, 0x01, 0x02, 0x03, 0x04,
				0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0x11, 0x22, 0x33};
			file.insert(file.end(), garbage, garbage + sizeof(garbage));
			continue;
		}

		if (use_lz4)
		{
			std::vector<u8> comp(static_cast<size_t>(LZ4_compressBound(static_cast<int>(frame_size))));
			const int n = LZ4_compress_default(
				reinterpret_cast<const char*>(frames[i].data()),
				reinterpret_cast<char*>(comp.data()),
				static_cast<int>(frame_size),
				static_cast<int>(comp.size()));
			if (n <= 0)
				return false;
			file.insert(file.end(), comp.begin(), comp.begin() + n);
		}
		else
		{
			z_stream zs{};
			if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, -15, 8, Z_DEFAULT_STRATEGY) != Z_OK)
				return false;
			std::vector<u8> comp(frame_size + 128);
			zs.next_in = const_cast<Bytef*>(frames[i].data());
			zs.avail_in = frame_size;
			zs.next_out = comp.data();
			zs.avail_out = static_cast<uInt>(comp.size());
			const int ret = deflate(&zs, Z_FINISH);
			const size_t n = comp.size() - zs.avail_out;
			deflateEnd(&zs);
			if (ret != Z_STREAM_END)
				return false;
			file.insert(file.end(), comp.begin(), comp.begin() + n);
		}
	}

	WriteLe32(file, header_size + static_cast<size_t>(num_frames) * 4, static_cast<u32>(file.size()));

	std::FILE* fp = std::fopen(path.c_str(), "wb");
	if (!fp)
		return false;
	const bool ok = std::fwrite(file.data(), 1, file.size(), fp) == file.size();
	std::fclose(fp);
	return ok;
}

void RunSequentialCoalesceTest(const std::filesystem::path& tmpdir, bool use_lz4)
{
	const char* tag = use_lz4 ? "ZISO" : "CISO";
	const u32 frame_size = 2048;
	const u32 num_frames = 256;
	std::vector<std::vector<u8>> frames(num_frames, std::vector<u8>(frame_size, 0));

	for (u32 i = 0; i < num_frames; ++i)
	{
		if (i % 4 != 0)
		{
			for (u32 b = 0; b < frame_size; ++b)
				frames[i][b] = static_cast<u8>((i * 37u + b * 13u + (use_lz4 ? 5u : 1u)) & 0xFFu);
		}
	}

	const std::string path = (tmpdir / (use_lz4 ? "synthetic.zso" : "synthetic.cso")).string();
	const int uncompressed_idx = 17;
	Check(WriteSyntheticCompressedImage(path, use_lz4, frames, frame_size, uncompressed_idx, -1),
		(std::string(tag) + ": wrote synthetic 256-frame image").c_str());

	CsoFileReader reader;
	Error err;
	Check(reader.Open(path, &err), (std::string(tag) + ": CsoFileReader::Open succeeded").c_str());

	// Sequential scan across all 256 contiguous frames (compressed + uncompressed) should perform 0 seeks.
	g_fseek_calls.store(0, std::memory_order_relaxed);
	std::vector<u8> buf(frame_size, 0);
	bool all_match = true;
	for (u32 i = 0; i < num_frames; ++i)
	{
		const int got = reader.ReadSync(buf.data(), i, 1);
		if (got != static_cast<int>(frame_size) || std::memcmp(buf.data(), frames[i].data(), frame_size) != 0)
		{
			all_match = false;
			break;
		}
	}
	const int sequential_seeks = g_fseek_calls.load(std::memory_order_relaxed);
	Check(all_match, (std::string(tag) + ": all 256 sequential frames round-trip identically").c_str());
	Check(sequential_seeks == 0,
		(std::string(tag) + ": sequential scan skipped all FSeek64 calls (seeks=" + std::to_string(sequential_seeks) + ")").c_str());

	// Random seek followed by contiguous run should seek once per jump and 0 times for contiguous frames.
	g_fseek_calls.store(0, std::memory_order_relaxed);
	Check(reader.ReadSync(buf.data(), 200, 1) == static_cast<int>(frame_size) &&
			std::memcmp(buf.data(), frames[200].data(), frame_size) == 0,
		(std::string(tag) + ": random seek to frame 200 matches").c_str());
	Check(reader.ReadSync(buf.data(), 5, 1) == static_cast<int>(frame_size) &&
			std::memcmp(buf.data(), frames[5].data(), frame_size) == 0,
		(std::string(tag) + ": random seek back to frame 5 matches").c_str());
	Check(reader.ReadSync(buf.data(), 6, 1) == static_cast<int>(frame_size) &&
			std::memcmp(buf.data(), frames[6].data(), frame_size) == 0,
		(std::string(tag) + ": contiguous frame 6 matches").c_str());
	Check(reader.ReadSync(buf.data(), 7, 1) == static_cast<int>(frame_size) &&
			std::memcmp(buf.data(), frames[7].data(), frame_size) == 0,
		(std::string(tag) + ": contiguous frame 7 matches").c_str());
	const int random_then_seq_seeks = g_fseek_calls.load(std::memory_order_relaxed);
	Check(random_then_seq_seeks == 2,
		(std::string(tag) + ": random jump + contiguous tail did 2 seeks total (got " + std::to_string(random_then_seq_seeks) + ")").c_str());

	reader.Close();
}

void RunCorruptFrameErrorTest(const std::filesystem::path& tmpdir)
{
	const u32 frame_size = 2048;
	std::vector<std::vector<u8>> frames(4, std::vector<u8>(frame_size, 0x42));
	const std::string path = (tmpdir / "corrupt.cso").string();
	Check(WriteSyntheticCompressedImage(path, false, frames, frame_size, -1, 2),
		"corrupt CISO: wrote synthetic image with bad frame 2");

	CsoFileReader reader;
	Error err;
	Check(reader.Open(path, &err), "corrupt CISO: Open succeeded");
	std::vector<u8> buf(frame_size, 0);
	Check(reader.ReadSync(buf.data(), 0, 1) == static_cast<int>(frame_size), "corrupt CISO: good frame 0 reads");
	Check(reader.ReadSync(buf.data(), 2, 1) == 0, "corrupt CISO: bad frame 2 returns 0");
	Check(!reader.GetLastReadError().empty(), "corrupt CISO: GetLastReadError populated on decompression failure");
	reader.Close();
}

void RunPrecacheCapTest()
{
	constexpr u64 kMiB = 1024ULL * 1024ULL;
	constexpr u64 kGiB = 1024ULL * kMiB;

	// PS4 always refuses precache even if an inflated available-memory value is supplied.
	Check(!ThreadedFileReader::CanPrecacheSize(1 * kMiB, 6 * kGiB, true),
		"precache cap: PS4 refuses 1 MiB image");
	Check(!ThreadedFileReader::CanPrecacheSize(4 * kGiB, 6 * kGiB, true),
		"precache cap: PS4 refuses 4 GiB DVD image");

	// Conservative 2 GiB Orbis available-memory value leaves 0 bytes above the 2 GiB reserve.
	Check(!ThreadedFileReader::CanPrecacheSize(1, 2 * kGiB, false),
		"precache cap: 2 GiB available leaves no precache headroom above 2 GiB reserve");
	Check(!ThreadedFileReader::CanPrecacheSize(512 * kMiB, 1 * kGiB, false),
		"precache cap: available < reserve does not underflow");

	// Host/desktop with 8 GiB available allows a 4 GiB image and refuses a 7 GiB image.
	Check(ThreadedFileReader::CanPrecacheSize(4 * kGiB, 8 * kGiB, false),
		"precache cap: 8 GiB available allows 4 GiB image");
	Check(!ThreadedFileReader::CanPrecacheSize(7 * kGiB, 8 * kGiB, false),
		"precache cap: 8 GiB available refuses 7 GiB image");
}
} // namespace

int main()
{
	std::filesystem::path tmpdir = std::filesystem::temp_directory_path() / "caliza-cso-reader-test";
	std::error_code ec;
	std::filesystem::remove_all(tmpdir, ec);
	std::filesystem::create_directories(tmpdir, ec);

	RunSequentialCoalesceTest(tmpdir, false);
	RunSequentialCoalesceTest(tmpdir, true);
	RunCorruptFrameErrorTest(tmpdir);
	RunPrecacheCapTest();

	std::filesystem::remove_all(tmpdir, ec);
	if (g_failures != 0)
	{
		std::printf("test_cso_reader: FAILED (%d)\n", g_failures);
		return 1;
	}
	std::printf("test_cso_reader: PASS\n");
	return 0;
}
