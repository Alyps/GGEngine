#pragma once
// Windows может быть подключён раньше; временно убираем конфликтующие макросы.
#pragma push_macro("min")
#pragma push_macro("max")
#undef min
#undef max
#include <rapidjson/document.h>
#pragma pop_macro("max")
#pragma pop_macro("min")
#include <initializer_list>
#include <string>
#include <unordered_map>

// Значения JSON используют аллокатор итогового документа, а не временную память.
namespace Serialization {
    rapidjson::Value String(const std::string& text, rapidjson::Document::AllocatorType& allocator);
    rapidjson::Value Array(std::initializer_list<float> values, rapidjson::Document::AllocatorType& allocator);
    rapidjson::Value ComponentObject(const char* type, rapidjson::Document::AllocatorType& allocator);
    rapidjson::Value Finish(rapidjson::Value value, std::string& error);
    bool SaveFile(const rapidjson::Value& value, const std::string& path, std::string& error);
}
class Game;
class GameObject;

class Component {
    friend class GameObject;
    friend struct TransformNode;
    GameObject* owner = nullptr;
    bool pendingDestroy = false;
    bool initialized = false;
public:
    Component() = default;
    Component(const Component&) = delete;
    Component& operator=(const Component&) = delete;
    GameObject* getOwner() const { return owner; }
    bool isPendingDestroy() const { return pendingDestroy; }
    virtual ~Component() = default;
    // Стабильный тип и авторские параметры; null означает ошибку сериализации.
    virtual const char* GetSerializationType() const = 0;
    virtual rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const = 0;
    virtual void Init(Game& game) = 0;
    // Снимает регистрации до освобождения памяти компонента.
    virtual void Shutdown(Game& game) {}
    // Компоненты без покадровой логики используют пустую реализацию.
    virtual void Update(Game& game, double deltaTime) {}
};

// Game владеет таблицей по значению; фабрики возвращают ещё не закреплённые компоненты.
class ComponentFactoryMap {
public:
    using Factory = Component* (*)(const rapidjson::Value&, std::string&);
    ComponentFactoryMap();
    bool Register(const std::string& type, Factory factory);
    Component* Create(const rapidjson::Value& json, std::string& error) const;
private:
    std::unordered_map<std::string, Factory> factories;
};

namespace Deserialization {
    const rapidjson::Value* Field(const rapidjson::Value& json, const char* name, std::string& error);
    const rapidjson::Value* Array(const rapidjson::Value& json, const char* name, std::string& error);
    bool String(const rapidjson::Value& json, const char* name, std::string& value, std::string& error);
    bool Float(const rapidjson::Value& json, const char* name, float& value, std::string& error);
    bool Bool(const rapidjson::Value& json, const char* name, bool& value, std::string& error);
    bool Floats(const rapidjson::Value& json, const char* name, float* values, size_t count, std::string& error);
    bool Asset(const rapidjson::Value& json, const char* type, std::string& error);
    bool LoadFile(const std::string& path, rapidjson::Document& document, std::string& error);
}
