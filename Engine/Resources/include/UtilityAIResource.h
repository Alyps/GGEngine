#pragma once
#include "ResourceManager.h"
#include <vector>

struct UtilityCurveData {
    struct Point {
        double x, y;
        bool operator==(const Point&) const = default;
    };
    std::vector<Point> points;
    bool operator==(const UtilityCurveData&) const = default;
    bool Read(const std::string& path, std::string& error);
    bool Validate(std::string& error) const;
    bool Save(const std::string& path, std::string& error) const;
    // Вызывается для кривой, прошедшей Validate.
    double Evaluate(double value) const;
};

// Менеджер владеет кривой; таблицы только заимствуют её.
class UtilityCurveResource final : public Resource {
    friend class ResourceManager;
    UtilityCurveData data;
    UtilityCurveResource() = default;
    bool load(const std::string& path) override;
public:
    double Evaluate(double value) const { return data.Evaluate(value); }
};

enum class UtilityParameterType { Float, Int };
struct UtilityParameter {
    std::string name;
    UtilityParameterType type;
    double initialValue = 0;
    bool operator==(const UtilityParameter&) const = default;
};

// Черновик хранит пути от проекта, даже если файлы ещё недоступны.
struct UtilityTableData {
    struct Action {
        std::string name, scriptPath;
        std::vector<std::string> curvePaths;
        bool operator==(const Action&) const = default;
    };
    std::vector<UtilityParameter> parameters;
    std::vector<Action> actions;
    double interval = 0.2, switchThreshold = 0.05, minimumDuration = 0;
    bool operator==(const UtilityTableData&) const = default;
    bool Read(const std::string& path, std::string& error);
    bool Validate(std::string& error) const;
    bool Save(const std::string& path, std::string& error) const;
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
