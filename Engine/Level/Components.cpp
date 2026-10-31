#include <rapidjson/prettywriter.h>
#include <rapidjson/stringbuffer.h>
#include <atomic>
#include <cmath>
#include <rapidjson/error/en.h>
#include "Components.h"

namespace Serialization {

rapidjson::Value String(const std::string& text, rapidjson::Document::AllocatorType& allocator)
{
    return rapidjson::Value(text.data(), static_cast<rapidjson::SizeType>(text.size()), allocator);
}

rapidjson::Value Array(std::initializer_list<float> values, rapidjson::Document::AllocatorType& allocator)
{
    rapidjson::Value result(rapidjson::kArrayType);
    for (float value : values) result.PushBack(value, allocator);
    return result;
}

rapidjson::Value ComponentObject(const char* type, rapidjson::Document::AllocatorType& allocator)
{
    rapidjson::Value result(rapidjson::kObjectType);
    result.AddMember("type", rapidjson::Value(type, allocator), allocator);
    return result;
}

static bool Validate(const rapidjson::Value& value, std::string& error)
{
    error.clear();
    // Проверяем UTF-8 и конечность чисел без выделения строки для всего JSON.
    struct Sink {
        using Ch = char;
        void Put(char) {}
        void Flush() {}
    } sink;
    rapidjson::Writer<Sink, rapidjson::UTF8<>, rapidjson::UTF8<>,
        rapidjson::CrtAllocator, rapidjson::kWriteValidateEncodingFlag> writer(sink);
    if (!value.Accept(writer)) {
        error = "Cannot serialize invalid UTF-8 or non-finite numbers";
        return false;
    }
    return true;
}

rapidjson::Value Finish(rapidjson::Value value, std::string& error)
{
    if (!Validate(value, error)) return {};
    return value;
}

bool SaveFile(const rapidjson::Value& value, const std::string& path, std::string& error)
{
    error.clear();
    if (!Validate(value, error)) return false;
    rapidjson::StringBuffer buffer;
    // UTF-8 проверен отдельно: PrettyWriter 1.1 не поддерживает этот флаг корректно.
    rapidjson::PrettyWriter<rapidjson::StringBuffer> writer(buffer);
    // Документ полностью готовим до открытия файла, сохраняя старый ассет при ошибке.
    if (!value.IsObject() || !value.Accept(writer)) {
        error = "Cannot serialize asset JSON";
        return false;
    }
    buffer.Put('\n');
    if (path.empty() || path.find('\0') != std::string::npos ||
        path.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        error = "Invalid or empty UTF-8 asset path";
        return false;
    }
    // Файловый API Windows получает UTF-16 без зависимости от системной кодировки.
    const int length = static_cast<int>(path.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, nullptr, 0);
    if (!count) { error = "Invalid UTF-8 asset path"; return false; }
    std::wstring wide(count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, wide.data(), count)) {
        error = "Cannot convert asset path";
        return false;
    }
    // Временный файл рядом с целевым заменяет его только после успешной записи.
    static std::atomic<unsigned long> sequence{ 0 };
    const std::wstring temporary = wide + L"." + std::to_wstring(GetCurrentProcessId()) +
        L"." + std::to_wstring(sequence.fetch_add(1)) + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr,
        CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = "Cannot create asset file (Win32 " + std::to_string(GetLastError()) + ")";
        return false;
    }
    DWORD written = 0;
    bool success = buffer.GetSize() <= (std::numeric_limits<DWORD>::max)() &&
        WriteFile(file, buffer.GetString(), static_cast<DWORD>(buffer.GetSize()), &written, nullptr) &&
        written == buffer.GetSize();
    if (success) success = FlushFileBuffers(file) != FALSE;
    if (!CloseHandle(file)) success = false;
    if (!success) {
        DeleteFileW(temporary.c_str());
        error = "Cannot write asset; original file was preserved";
        return false;
    }
    if (!MoveFileExW(temporary.c_str(), wide.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
        const DWORD code = GetLastError();
        DeleteFileW(temporary.c_str());
        error = "Cannot replace asset file (Win32 " + std::to_string(code) + ")";
        return false;
    }
    return true;
}

} // namespace Serialization

ComponentFactoryMap::ComponentFactoryMap()
{
    // Каждая таблица независимо содержит встроенные фабрики, без глобальной регистрации.
    Register("MeshComponent", &MeshComponent::CreateFromJson);
    Register("GameCamera", &GameCamera::CreateFromJson);
    Register("EditorCamera", &EditorCamera::CreateFromJson);
    Register("CameraMovementComponent", &CameraMovementComponent::CreateFromJson);
    Register("DirectionalLightComponent", &DirectionalLightComponent::CreateFromJson);
    Register("PointLightComponent", &PointLightComponent::CreateFromJson);
    Register("LuaScriptComponent", &LuaScriptComponent::CreateFromJson);
    Register("BoxColliderComponent", &BoxColliderComponent::CreateFromJson);
    Register("DynamicRigidBodyComponent", &DynamicRigidBodyComponent::CreateFromJson);
    Register("StaticRigidBodyComponent", &StaticRigidBodyComponent::CreateFromJson);
}

bool ComponentFactoryMap::Register(const std::string& type, Factory factory)
{
    if (type.empty() || !factory) return false;
    const auto [entry, inserted] = factories.emplace(type, factory);
    // Повторная регистрация той же функции допустима; чужую фабрику не заменяем.
    return inserted || entry->second == factory;
}

Component* ComponentFactoryMap::Create(const rapidjson::Value& json, std::string& error) const
{
    error.clear();
    std::string type;
    if (!Deserialization::String(json, "type", type, error)) return nullptr;
    const auto found = factories.find(type);
    if (found == factories.end()) { error = "Unknown component type: " + type; return nullptr; }
    Component* component = found->second(json, error);
    if (!component) error = type + ": " + (error.empty() ? "Cannot create component" : error);
    return component;
}

namespace Deserialization {

const rapidjson::Value* Field(const rapidjson::Value& json, const char* name, std::string& error)
{
    if (!json.IsObject()) { error = "Expected an object"; return nullptr; }
    const rapidjson::Value* result = nullptr;
    // Повторяющиеся обязательные поля неоднозначны, поэтому не выбираем первое молча.
    for (auto it = json.MemberBegin(); it != json.MemberEnd(); ++it) {
        if (it->name != name) continue;
        if (result) { error = std::string(name) + ": duplicate field"; return nullptr; }
        result = &it->value;
    }
    if (!result) error = std::string(name) + ": missing field";
    return result;
}

const rapidjson::Value* Array(const rapidjson::Value& json, const char* name, std::string& error)
{
    const auto* field = Field(json, name, error);
    if (!field) return nullptr;
    if (!field->IsArray()) { error = std::string(name) + ": expected an array"; return nullptr; }
    return field;
}

bool String(const rapidjson::Value& json, const char* name, std::string& value, std::string& error)
{
    const auto* field = Field(json, name, error);
    if (!field) return false;
    if (!field->IsString()) { error = std::string(name) + ": expected a string"; return false; }
    value.assign(field->GetString(), field->GetStringLength());
    // Имена и файловые пути не должны содержать скрытый конец строки.
    if (value.find('\0') != std::string::npos) { error = std::string(name) + ": embedded NUL"; return false; }
    return true;
}

static bool Number(const rapidjson::Value& json, float& value)
{
    if (!json.IsNumber()) return false;
    const double number = json.GetDouble();
    if (!std::isfinite(number) || std::abs(number) > (std::numeric_limits<float>::max)()) return false;
    value = static_cast<float>(number);
    return true;
}

bool Float(const rapidjson::Value& json, const char* name, float& value, std::string& error)
{
    const auto* field = Field(json, name, error);
    if (!field) return false;
    if (!Number(*field, value)) { error = std::string(name) + ": expected a finite float"; return false; }
    return true;
}

bool Bool(const rapidjson::Value& json, const char* name, bool& value, std::string& error)
{
    const auto* field = Field(json, name, error);
    if (!field) return false;
    if (!field->IsBool()) { error = std::string(name) + ": expected a boolean"; return false; }
    value = field->GetBool();
    return true;
}

bool Floats(const rapidjson::Value& json, const char* name, float* values, size_t count, std::string& error)
{
    const auto* field = Array(json, name, error);
    if (!field) return false;
    if (field->Size() != count) { error = std::string(name) + ": wrong array size"; return false; }
    for (rapidjson::SizeType i = 0; i < count; ++i) {
        if (!Number((*field)[i], values[i])) {
            error = std::string(name) + "[" + std::to_string(i) + "]: expected a finite float";
            return false;
        }
    }
    return true;
}

bool Asset(const rapidjson::Value& json, const char* type, std::string& error)
{
    error.clear();
    std::string actual;
    if (!String(json, "asset_type", actual, error)) return false;
    if (actual != type) { error = "Expected asset type " + std::string(type); return false; }
    const auto* version = Field(json, "version", error);
    if (!version) return false;
    if (!version->IsInt() || version->GetInt() != 1) { error = "Unsupported asset version"; return false; }
    return true;
}

bool LoadFile(const std::string& path, rapidjson::Document& document, std::string& error)
{
    error.clear();
    if (path.empty() || path.find('\0') != std::string::npos ||
        path.size() > static_cast<size_t>((std::numeric_limits<int>::max)())) {
        error = "Invalid UTF-8 asset path";
        return false;
    }
    // Читаем UTF-8 пути через Unicode API, не используя системную кодовую страницу.
    const int length = static_cast<int>(path.size());
    const int count = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, nullptr, 0);
    if (!count) { error = "Invalid UTF-8 asset path"; return false; }
    std::wstring wide(count, L'\0');
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path.data(), length, wide.data(), count)) {
        error = "Cannot convert asset path"; return false;
    }
    HANDLE file = CreateFileW(wide.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) {
        error = path + ": cannot open file (Win32 " + std::to_string(GetLastError()) + ")";
        return false;
    }
    LARGE_INTEGER size{};
    if (!GetFileSizeEx(file, &size) || size.QuadPart < 0 || size.QuadPart > (std::numeric_limits<DWORD>::max)()) {
        CloseHandle(file); error = path + ": invalid or oversized asset file"; return false;
    }
    std::string bytes(static_cast<size_t>(size.QuadPart), '\0');
    DWORD read = 0;
    const bool success = ReadFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr) && read == bytes.size();
    CloseHandle(file);
    if (!success) { error = path + ": cannot read complete asset file"; return false; }
    // NUL мог бы скрыть остаток файла от парсера. Iterative ограничивает стек парсера.
    if (bytes.find('\0') != std::string::npos) { error = path + ": embedded NUL in JSON"; return false; }
    document.Parse<rapidjson::kParseValidateEncodingFlag | rapidjson::kParseIterativeFlag>(bytes.data(), bytes.size());
    if (document.HasParseError()) {
        error = path + ": JSON byte " + std::to_string(document.GetErrorOffset()) + ": " +
            rapidjson::GetParseError_En(document.GetParseError());
        return false;
    }
    return true;
}

} // namespace Deserialization
