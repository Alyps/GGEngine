#include "AudioServer.h"

AudioServer::~AudioServer()
{
    if (output) output->DestroyVoice();
    if (engine) engine->Release();
    if (ownsCom) CoUninitialize();
}

bool AudioServer::Init(std::string& error)
{
    if (engine) return true;
    // Балансируем только собственный успешный CoInitializeEx на потоке Game.
    if (!ownsCom) {
        const HRESULT status = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        if (FAILED(status) && status != RPC_E_CHANGED_MODE) {
            error = "Cannot initialize audio COM (HRESULT " + std::to_string(status) + ")";
            return false;
        }
        ownsCom = SUCCEEDED(status);
    }
    IXAudio2* candidate = nullptr;
    HRESULT status = XAudio2Create(&candidate, 0, XAUDIO2_DEFAULT_PROCESSOR);
    if (SUCCEEDED(status)) status = candidate->CreateMasteringVoice(&output);
    if (FAILED(status)) {
        if (candidate) candidate->Release();
        output = nullptr;
        error = "Cannot open audio device (HRESULT " + std::to_string(status) + ")";
        return false;
    }
    engine = candidate;
    return true;
}

bool AudioServer::CreateVoice(const WAVEFORMATEX& format, IXAudio2SourceVoice*& voice, std::string& error)
{
    error.clear();
    if (!Init(error)) return false;
    const HRESULT status = engine->CreateSourceVoice(&voice, &format);
    if (FAILED(status)) {
        error = "Cannot create audio voice (HRESULT " + std::to_string(status) + ")";
        return false;
    }
    return true;
}
