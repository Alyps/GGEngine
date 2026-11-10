#pragma once
#include "ResourceManager.h"
#include <vector>

// Менеджер владеет кривой; таблицы только заимствуют её.
class UtilityCurveResource final : public Resource {
    friend class ResourceManager;
    struct Point { double x, y; };
    std::vector<Point> points;
    UtilityCurveResource() = default;
    bool load(const std::string& path) override;
public:
    double Evaluate(double value) const;
};

enum class UtilityParameterType { Float, Int };
struct UtilityParameter {
    std::string name;
    UtilityParameterType type;
    double initialValue = 0;
};

struct UtilityAction {
    std::string name, scriptPath;
    // Позиции соответствуют параметрам; nullptr означает пустую ячейку.
    std::vector<const UtilityCurveResource*> curves;
};

class UtilityAITable final : public Resource {
    friend class ResourceManager;
    ResourceManager* resources;
    std::vector<UtilityParameter> parameters;
    std::vector<UtilityAction> actions;
    double interval = 0.2, switchThreshold = 0.05, minimumDuration = 0;
    explicit UtilityAITable(ResourceManager* manager) : resources(manager) {}
    bool load(const std::string& path) override;
public:
    const auto& GetParameters() const { return parameters; }
    const auto& GetActions() const { return actions; }
    double GetInterval() const { return interval; }
    double GetSwitchThreshold() const { return switchThreshold; }
    double GetMinimumDuration() const { return minimumDuration; }
};
