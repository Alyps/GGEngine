#pragma once
#include "TransformableComponent.h"
#include <string>

class SoundResource;
struct IXAudio2SourceVoice;
struct lua_State;

// TransformNode владеет компонентом; Game и кеш звуков живут дольше него.
class AudioSourceComponent final : public TransformableComponent {
    Game* game = nullptr;
    const SoundResource* sound = nullptr;
    IXAudio2SourceVoice* voice = nullptr;
    std::string soundPath, lastError;
    float volume = 1;
    bool loop = false, playOnStart = false, paused = false;
    bool LoadSound();
public:
    explicit AudioSourceComponent(std::string path = {}) : soundPath(std::move(path)) {}
    ~AudioSourceComponent() override;
    const char* GetSerializationType() const override { return "AudioSourceComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType&, std::string&) const override;
    static Component* CreateFromJson(const rapidjson::Value&, std::string&);
    bool ApplyDescription(const rapidjson::Value&, std::string&);
    void Init(Game&) override;
    void Shutdown(Game&) override;
    const std::string& GetSoundPath() const { return soundPath; }
    void SetSoundPath(std::string path);
    float GetVolume() const { return volume; }
    bool SetVolume(float value);
    bool IsLooping() const { return loop; }
    void SetLooping(bool value);
    bool GetPlayOnStart() const { return playOnStart; }
    void SetPlayOnStart(bool value) { playOnStart = value; }
    bool Play();
    void Pause();
    void Stop();
    bool IsPlaying() const;
    bool IsPaused() const { return paused; }
    const std::string& GetLastError() const { return lastError; }
};

void BindAudioSource(lua_State* state);
