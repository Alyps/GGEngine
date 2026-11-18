#pragma once
#include "ResourceManager.h"
#include <mmreg.h>
#include <vector>

// ResourceManager владеет PCM-данными. Компоненты заимствуют их до Shutdown сцены.
class SoundResource final : public Resource {
    friend class ResourceManager;
    WAVEFORMATEXTENSIBLE format{};
    std::vector<unsigned char> samples;
    SoundResource() = default;
    bool load(const std::string& path) override;
    bool ReadFormat(const unsigned char* data, uint32_t size);
public:
    const WAVEFORMATEX& GetFormat() const { return format.Format; }
    const auto& GetSamples() const { return samples; }
};
