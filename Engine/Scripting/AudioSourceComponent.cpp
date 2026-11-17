#include "AudioSourceComponent.h"
#include "SoundResource.h"
#include "core.h"
#include <cmath>
#include <sol/sol.hpp>

AudioSourceComponent::~AudioSourceComponent() { Stop(); }

void AudioSourceComponent::Init(Game& owner)
{
    Stop();
    game = &owner;
    sound = nullptr;
    lastError.clear();
    if (!soundPath.empty() && LoadSound() && playOnStart && !owner.isEditorMode()) Play();
}

void AudioSourceComponent::Shutdown(Game&)
{
    // DestroyVoice ждёт завершения чтения буферов до уничтожения ResourceManager.
    Stop();
    sound = nullptr;
    game = nullptr;
}

bool AudioSourceComponent::LoadSound()
{
    if (!game || isPendingDestroy() || !getOwner() || getOwner()->isPendingDestroy()) {
        lastError = "Audio source is unavailable"; return false;
    }
    if (sound) return true;
    auto& resources = game->getResourceManager();
    sound = resources.load<SoundResource>(soundPath);
    if (!sound) lastError = resources.GetLastError();
    return sound != nullptr;
}

void AudioSourceComponent::SetSoundPath(std::string path)
{
    Stop();
    soundPath = std::move(path);
    sound = nullptr;
    lastError.clear();
    if (game && !soundPath.empty()) LoadSound();
}

bool AudioSourceComponent::SetVolume(float value)
{
    if (!std::isfinite(value) || value < 0 || value > 1) return false;
    if (voice) {
        const HRESULT status = voice->SetVolume(value);
        if (FAILED(status)) { lastError = "Cannot change audio volume"; return false; }
    }
    volume = value;
    return true;
}

void AudioSourceComponent::SetLooping(bool value)
{
    if (loop == value) return;
    Stop();
    loop = value;
}

bool AudioSourceComponent::Play()
{
    if (!LoadSound()) return false;
    if (!paused) {
        Stop();
        if (!game->getAudioServer().CreateVoice(sound->GetFormat(), voice, lastError)) return false;
        XAUDIO2_BUFFER buffer{};
        buffer.pAudioData = sound->GetSamples().data();
        buffer.AudioBytes = static_cast<UINT32>(sound->GetSamples().size());
        buffer.Flags = XAUDIO2_END_OF_STREAM;
        buffer.LoopCount = loop ? XAUDIO2_LOOP_INFINITE : 0;
        HRESULT status = voice->SetVolume(volume);
        if (SUCCEEDED(status)) status = voice->SubmitSourceBuffer(&buffer);
        if (FAILED(status)) {
            lastError = "Cannot queue audio (HRESULT " + std::to_string(status) + ")";
            Stop(); return false;
        }
    }
    const HRESULT status = voice->Start();
    if (FAILED(status)) {
        lastError = "Cannot start audio (HRESULT " + std::to_string(status) + ")";
        Stop(); return false;
    }
    paused = false;
    lastError.clear();
    return true;
}

void AudioSourceComponent::Pause()
{
    if (!IsPlaying()) return;
    const HRESULT status = voice->Stop();
    if (FAILED(status)) { lastError = "Cannot pause audio"; return; }
    paused = true;
}

void AudioSourceComponent::Stop()
{
    if (voice) voice->DestroyVoice();
    voice = nullptr;
    paused = false;
}

bool AudioSourceComponent::IsPlaying() const
{
    if (!voice || paused) return false;
    XAUDIO2_VOICE_STATE state{};
    voice->GetState(&state, XAUDIO2_VOICE_NOSAMPLESPLAYED);
    return state.BuffersQueued != 0;
}

rapidjson::Value AudioSourceComponent::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    auto json = Serialization::ComponentObject(GetSerializationType(), allocator);
    json.AddMember("sound_path", Serialization::String(soundPath, allocator), allocator);
    json.AddMember("volume", volume, allocator);
    json.AddMember("loop", loop, allocator);
    json.AddMember("play_on_start", playOnStart, allocator);
    return Serialization::Finish(std::move(json), error);
}

Component* AudioSourceComponent::CreateFromJson(const rapidjson::Value& json, std::string& error)
{
    std::string path;
    float volume;
    bool loop, automatic;
    if (!Deserialization::String(json, "sound_path", path, error) || !Deserialization::Float(json, "volume", volume, error)
        || !Deserialization::Bool(json, "loop", loop, error) || !Deserialization::Bool(json, "play_on_start", automatic, error)) return nullptr;
    if (!std::isfinite(volume) || volume < 0 || volume > 1) { error = "Audio volume must be between 0 and 1"; return nullptr; }
    auto* component = new (std::nothrow) AudioSourceComponent(std::move(path));
    if (!component) { error = "Cannot allocate audio source"; return nullptr; }
    component->volume = volume;
    component->loop = loop;
    component->playOnStart = automatic;
    return component;
}

bool AudioSourceComponent::ApplyDescription(const rapidjson::Value& json, std::string& error)
{
    // История использует ту же валидацию, что и загрузка компонента из сцены.
    auto* description = static_cast<AudioSourceComponent*>(CreateFromJson(json, error));
    if (!description) return false;
    if (!SetVolume(description->volume)) {
        error = lastError;
        delete description;
        return false;
    }
    SetLooping(description->loop);
    SetPlayOnStart(description->playOnStart);
    if (soundPath != description->soundPath) SetSoundPath(std::move(description->soundPath));
    delete description;
    return true;
}

void BindAudioSource(lua_State* state)
{
    sol::state_view lua(state);
    lua.new_usertype<AudioSourceComponent>("AudioSource", sol::no_constructor,
        "play", &AudioSourceComponent::Play, "pause", &AudioSourceComponent::Pause, "stop", &AudioSourceComponent::Stop,
        "playing", sol::property(&AudioSourceComponent::IsPlaying),
        "paused", sol::property(&AudioSourceComponent::IsPaused),
        "volume", sol::property(&AudioSourceComponent::GetVolume, &AudioSourceComponent::SetVolume),
        "loop", sol::property(&AudioSourceComponent::IsLooping, &AudioSourceComponent::SetLooping),
        "play_on_start", sol::property(&AudioSourceComponent::GetPlayOnStart, &AudioSourceComponent::SetPlayOnStart),
        "sound_path", sol::property(&AudioSourceComponent::GetSoundPath, &AudioSourceComponent::SetSoundPath),
        "last_error", sol::property(&AudioSourceComponent::GetLastError));
}
