#pragma once
#include <windows.h>
#include <xaudio2.h>
#include <string>

// Game владеет сервером. Компоненты освобождают свои source voices раньше сервера.
class AudioServer {
    IXAudio2* engine = nullptr;
    IXAudio2MasteringVoice* output = nullptr;
    bool ownsCom = false;
    bool Init(std::string& error);
public:
    ~AudioServer();
    AudioServer() = default;
    AudioServer(const AudioServer&) = delete;
    AudioServer& operator=(const AudioServer&) = delete;
    // Устройство создаётся лениво при первом явном воспроизведении.
    bool CreateVoice(const WAVEFORMATEX& format, IXAudio2SourceVoice*& voice, std::string& error);
};
