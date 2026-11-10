#include "Component.h"
#include "UtilityAIResource.h"
#include <cmath>
#include <unordered_set>

namespace {
bool Number(const rapidjson::Value& json, const char* name, double& result, std::string& error)
{
    const auto* value = Deserialization::Field(json, name, error);
    if (!value) return false;
    if (!value->IsNumber() || !std::isfinite(value->GetDouble())) {
        error = std::string(name) + ": expected finite number"; return false;
    }
    result = value->GetDouble();
    return true;
}
}

bool UtilityCurveResource::load(const std::string& path)
{
    rapidjson::Document json;
    if (!Deserialization::LoadFile(path, json, loadError)) return false;
    const auto* array = Deserialization::Array(json, "points", loadError);
    if (!array) return false;
    if (array->Size() < 2) { loadError = "Curve requires at least two points"; return false; }
    for (const auto& entry : array->GetArray()) {
        Point point{};
        if (!Number(entry, "x", point.x, loadError) || !Number(entry, "y", point.y, loadError)) return false;
        // Строго возрастающий x исключает деление на ноль при интерполяции.
        if (point.y < 0 || point.y > 1 || (!points.empty() && point.x <= points.back().x)) {
            loadError = "Curve requires increasing x and y in [0,1]"; return false;
        }
        points.push_back(point);
    }
    return true;
}

double UtilityCurveResource::Evaluate(double value) const
{
    if (value <= points.front().x) return points.front().y;
    for (size_t i = 1; i < points.size(); ++i) {
        if (value > points[i].x) continue;
        const auto& a = points[i - 1];
        const auto& b = points[i];
        // Масштабируем только при переполнении, сохраняя точность малых диапазонов.
        const double width = b.x - a.x;
        const double t = std::isfinite(width) ? (value - a.x) / width :
            (value * 0.5 - a.x * 0.5) / (b.x * 0.5 - a.x * 0.5);
        return a.y + (b.y - a.y) * t;
    }
    return points.back().y;
}

bool UtilityAITable::load(const std::string& path)
{
    rapidjson::Document json;
    if (!resources || !Deserialization::LoadFile(path, json, loadError)) return false;
    const auto* params = Deserialization::Array(json, "parameters", loadError);
    if (!params) return false;
    std::unordered_set<std::string> names;
    for (const auto& entry : params->GetArray()) {
        UtilityParameter parameter;
        std::string type;
        if (!Deserialization::String(entry, "name", parameter.name, loadError) ||
            !Deserialization::String(entry, "type", type, loadError) ||
            !Number(entry, "default", parameter.initialValue, loadError)) return false;
        if (parameter.name.empty() || !names.insert(parameter.name).second || (type != "float" && type != "int")) {
            loadError = "Parameters require unique nonempty names and float/int types"; return false;
        }
        parameter.type = type == "float" ? UtilityParameterType::Float : UtilityParameterType::Int;
        // int имеет фиксированный 32-битный диапазон и точно представим в double.
        if (parameter.type == UtilityParameterType::Int && (std::trunc(parameter.initialValue) != parameter.initialValue ||
            parameter.initialValue < INT32_MIN || parameter.initialValue > INT32_MAX)) {
            loadError = "Invalid int default"; return false;
        }
        parameters.push_back(std::move(parameter));
    }
    if (!Number(json, "interval", interval, loadError) || !Number(json, "switch_threshold", switchThreshold, loadError) ||
        !Number(json, "minimum_duration", minimumDuration, loadError)) return false;
    if (interval <= 0 || switchThreshold < 0 || minimumDuration < 0) {
        loadError = "Invalid decision timing or threshold"; return false;
    }
    const auto* rows = Deserialization::Array(json, "actions", loadError);
    if (!rows) return false;
    names.clear();
    for (const auto& entry : rows->GetArray()) {
        UtilityAction action;
        if (!Deserialization::String(entry, "name", action.name, loadError) ||
            !Deserialization::String(entry, "script", action.scriptPath, loadError)) return false;
        if (action.name.empty() || !names.insert(action.name).second || !action.scriptPath.ends_with(".action.lua")) {
            loadError = "Actions require unique names and .action.lua paths"; return false;
        }
        const auto* cells = Deserialization::Array(entry, "curves", loadError);
        if (!cells) return false;
        if (cells->Size() != parameters.size()) { loadError = "Curve count must match parameters"; return false; }
        bool hasCurve = false;
        for (const auto& cell : cells->GetArray()) {
            if (cell.IsNull()) { action.curves.push_back(nullptr); continue; }
            if (!cell.IsString()) { loadError = "Curve cell must be a path or null"; return false; }
            const auto* curve = resources->load<UtilityCurveResource>(std::string(cell.GetString(), cell.GetStringLength()));
            if (!curve) { loadError = resources->GetLastError(); return false; }
            action.curves.push_back(curve);
            hasCurve = true;
        }
        if (!hasCurve) { loadError = "Action requires at least one curve"; return false; }
        actions.push_back(std::move(action));
    }
    if (actions.empty()) { loadError = "Table requires actions"; return false; }
    return true;
}
