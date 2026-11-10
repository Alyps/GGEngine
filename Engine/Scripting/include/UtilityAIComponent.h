#pragma once
#include "Component.h"
#include "UtilityAIResource.h"

struct lua_State;
class UtilityAIComponent final : public Component {
    struct Impl;
    // Компонент владеет runtime и Lua-ссылками; Game живёт дольше компонента.
    Impl* impl = nullptr;
    const UtilityAITable* table = nullptr;
    std::string tablePath, lastError;
    std::vector<double> values, snapshot;
    bool enabled = true, evaluating = false;
    int current = -1;
    double elapsed = 0, decisionTime = 0;
    void Stop(const char* reason);
    void Fail(size_t action, const std::string& error);
    int Find(const std::string& name, UtilityParameterType type, std::string& error) const;
public:
    explicit UtilityAIComponent(std::string path) : tablePath(std::move(path)) {}
    ~UtilityAIComponent() override;
    const char* GetSerializationType() const override { return "UtilityAIComponent"; }
    rapidjson::Value Serialize(rapidjson::Document::AllocatorType&, std::string&) const override;
    static Component* CreateFromJson(const rapidjson::Value&, std::string&);
    void Init(Game&) override;
    void Shutdown(Game&) override;
    void Update(Game&, double dt) override;
    bool Get(const std::string&, UtilityParameterType, double&, std::string&) const;
    bool Set(const std::string&, UtilityParameterType, double, std::string&);
    const UtilityAITable* GetTable() const { return table; }
    std::string GetCurrentAction() const;
    const std::string& GetLastError() const { return lastError; }
    bool IsEnabled() const { return enabled; }
    // Остановка откладывается до Update: setter безопасен внутри Lua-callback.
    void SetEnabled(bool value) { enabled = value; }
};

void BindUtilityAI(lua_State* state);
