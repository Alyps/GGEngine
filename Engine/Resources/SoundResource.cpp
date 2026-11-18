#include "SoundResource.h"
#include <xaudio2.h>
#include <bit>
#include <cstring>
#include <filesystem>

namespace {
uint32_t ReadU32(const unsigned char* bytes)
{
    return uint32_t(bytes[0]) | (uint32_t(bytes[1]) << 8) | (uint32_t(bytes[2]) << 16) | (uint32_t(bytes[3]) << 24);
}
uint16_t ReadU16(const unsigned char* bytes)
{
    return uint16_t(bytes[0]) | (uint16_t(bytes[1]) << 8);
}
}

bool SoundResource::ReadFormat(const unsigned char* data, uint32_t size)
{
    if (size < 16) { loadError = "Invalid WAV format chunk"; return false; }
    auto& wave = format.Format;
    wave = { ReadU16(data), ReadU16(data + 2), ReadU32(data + 4), ReadU32(data + 8),
        ReadU16(data + 12), ReadU16(data + 14), 0 };
    if (wave.wFormatTag != WAVE_FORMAT_EXTENSIBLE) return true;
    if (size < 40 || ReadU16(data + 16) < 22 || uint32_t(ReadU16(data + 16)) + 18 > size) {
        loadError = "Truncated extensible WAV format"; return false;
    }
    // Расширенный PCM хранит маску каналов и число значимых бит.
    constexpr GUID pcm = { 1, 0, 0x0010, { 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71 } };
    wave.cbSize = 22;
    format.Samples.wValidBitsPerSample = ReadU16(data + 18);
    format.dwChannelMask = ReadU32(data + 20);
    std::memcpy(&format.SubFormat, data + 24, sizeof(GUID));
    if (std::memcmp(&format.SubFormat, &pcm, sizeof(GUID))
        || !format.Samples.wValidBitsPerSample || format.Samples.wValidBitsPerSample > wave.wBitsPerSample
        || (format.dwChannelMask && std::popcount(format.dwChannelMask) != wave.nChannels)) {
        loadError = "Expected extensible PCM WAV with a valid channel mask and bit depth"; return false;
    }
    return true;
}

bool SoundResource::load(const std::string& path)
{
    const auto diskPath = std::filesystem::path(std::u8string(path.begin(), path.end()));
    HANDLE file = CreateFileW(diskPath.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) { loadError = "Cannot open WAV file"; return false; }
    LARGE_INTEGER size{};
    const bool validSize = GetFileSizeEx(file, &size) && size.QuadPart >= 12 && size.QuadPart <= XAUDIO2_MAX_BUFFER_BYTES;
    if (!validSize) { CloseHandle(file); loadError = "Invalid or oversized WAV file"; return false; }
    std::vector<unsigned char> bytes(static_cast<size_t>(size.QuadPart));
    DWORD read = 0;
    const bool success = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    CloseHandle(file);
    if (!success) { loadError = "Cannot read complete WAV file"; return false; }
    if (std::memcmp(bytes.data(), "RIFF", 4) || std::memcmp(bytes.data() + 8, "WAVE", 4)) {
        loadError = "Expected RIFF WAVE audio"; return false;
    }
    const uint64_t end = uint64_t(ReadU32(bytes.data() + 4)) + 8;
    if (end < 12 || end > bytes.size()) { loadError = "Truncated WAV container"; return false; }
    bool hasFormat = false;
    size_t audioOffset = 0, audioSize = 0;
    // RIFF-чанки могут идти в разном порядке и содержать нечётный размер с padding.
    for (uint64_t offset = 12; offset < end;) {
        if (end - offset < 8) { loadError = "Truncated WAV chunk header"; return false; }
        const auto* chunk = bytes.data() + offset;
        const uint32_t length = ReadU32(chunk + 4);
        const uint64_t next = offset + 8 + length + (length & 1u);
        if (next > end) { loadError = "Truncated WAV chunk"; return false; }
        if (!std::memcmp(chunk, "fmt ", 4)) {
            if (hasFormat) { loadError = "Invalid WAV format chunk"; return false; }
            if (!ReadFormat(chunk + 8, length)) return false;
            hasFormat = true;
        } else if (!std::memcmp(chunk, "data", 4)) {
            if (audioOffset) { loadError = "Multiple WAV data chunks are unsupported"; return false; }
            audioOffset = static_cast<size_t>(offset + 8);
            audioSize = length;
        }
        offset = next;
    }
    const auto& wave = format.Format;
    const bool validBits = wave.wBitsPerSample == 8 || wave.wBitsPerSample == 16
        || wave.wBitsPerSample == 24 || wave.wBitsPerSample == 32;
    if (!hasFormat || (wave.wFormatTag != WAVE_FORMAT_PCM && wave.wFormatTag != WAVE_FORMAT_EXTENSIBLE)
        || !validBits || wave.nChannels < 1 || wave.nChannels > 2
        || wave.nSamplesPerSec < XAUDIO2_MIN_SAMPLE_RATE || wave.nSamplesPerSec > XAUDIO2_MAX_SAMPLE_RATE) {
        loadError = "Supported audio: mono/stereo WAV PCM, 8/16/24/32 bit"; return false;
    }
    const unsigned frameBytes = wave.nChannels * (wave.wBitsPerSample / 8);
    if (!audioOffset || !audioSize || wave.nBlockAlign != frameBytes || audioSize % frameBytes
        || wave.nAvgBytesPerSec != uint64_t(wave.nSamplesPerSec) * frameBytes) {
        loadError = "Invalid PCM data size or format alignment"; return false;
    }
    samples.assign(bytes.begin() + audioOffset, bytes.begin() + audioOffset + audioSize);
    return true;
}
