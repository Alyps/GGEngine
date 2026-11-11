#include "Component.h"
#include "UtilityAIResource.h"
#include <algorithm>
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

bool UtilityCurveData::Read(const std::string& path, std::string& error)
{
    rapidjson::Document json;
    if (!Deserialization::LoadFile(path, json, error)) return false;
    const auto* array = Deserialization::Array(json, "points", error);
    if (!array) return false;
    std::vector<Point> loaded;
    for (const auto& entry : array->GetArray()) {
        Point point{};
        if (!Number(entry, "x", point.x, error) || !Number(entry, "y", point.y, error)) return false;
        loaded.push_back(point);
    }
    points = std::move(loaded);
    return true;
}

bool UtilityCurveData::Validate(std::string& error) const
{
    error.clear();
    if (points.size() < 2) { error = "Curve requires at least two points"; return false; }
    for (size_t i = 0; i < points.size(); ++i) {
        const auto& point = points[i];
        // Строго возрастающий x исключает деление на ноль при интерполяции.
        if (!std::isfinite(point.x) || !std::isfinite(point.y) || point.y < 0 || point.y > 1 ||
            (i && point.x <= points[i - 1].x)) {
            error = "Curve requires finite points, strictly increasing X and Y in [0, 1]";
            return false;
        }
    }
    return true;
}

bool UtilityCurveData::Save(const std::string& path, std::string& error) const
{
    if (!Validate(error)) return false;
    rapidjson::Document json(rapidjson::kObjectType);
    auto& allocator = json.GetAllocator();
    rapidjson::Value array(rapidjson::kArrayType);
    for (const auto& point : points) {
        rapidjson::Value entry(rapidjson::kObjectType);
        entry.AddMember("x", point.x, allocator);
        entry.AddMember("y", point.y, allocator);
        array.PushBack(entry, allocator);
    }
    json.AddMember("points", array, allocator);
    return Serialization::SaveFile(json, path, error);
}

bool UtilityCurveResource::load(const std::string& path)
{
    return data.Read(path, loadError) && data.Validate(loadError);
}

double UtilityCurveData::Evaluate(double value) const
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

bool UtilityTableData::Read(const std::string& path, std::string& error)
{
    rapidjson::Document json;
    if (!Deserialization::LoadFile(path, json, error)) return false;
    const auto* params = Deserialization::Array(json, "parameters", error);
    const auto* rows = params ? Deserialization::Array(json, "actions", error) : nullptr;
    if (!rows) return false;
    UtilityTableData loaded;
    if (!Number(json, "interval", loaded.interval, error) || !Number(json, "switch_threshold", loaded.switchThreshold, error) ||
        !Number(json, "minimum_duration", loaded.minimumDuration, error)) return false;
    for (const auto& entry : params->GetArray()) {
        UtilityParameter parameter{};
        std::string type;
        if (!Deserialization::String(entry, "name", parameter.name, error) ||
            !Deserialization::String(entry, "type", type, error) ||
            !Number(entry, "default", parameter.initialValue, error)) return false;
        if (type != "float" && type != "int") {
            error = "Parameter type must be float or int";
            return false;
        }
        parameter.type = type == "float" ? UtilityParameterType::Float : UtilityParameterType::Int;
        loaded.parameters.push_back(std::move(parameter));
    }
    for (const auto& entry : rows->GetArray()) {
        Action action;
        if (!Deserialization::String(entry, "name", action.name, error) ||
            !Deserialization::String(entry, "script", action.scriptPath, error)) return false;
        const auto* cells = Deserialization::Array(entry, "curves", error);
        if (!cells) return false;
        if (cells->Size() != loaded.parameters.size()) { error = "Curve count must match parameters"; return false; }
        for (const auto& cell : cells->GetArray()) {
            if (cell.IsNull()) { action.curvePaths.emplace_back(); continue; }
            if (!cell.IsString() || cell.GetStringLength() == 0) { error = "Curve cell must be a path or null"; return false; }
            action.curvePaths.emplace_back(cell.GetString(), cell.GetStringLength());
        }
        loaded.actions.push_back(std::move(action));
    }
    *this = std::move(loaded);
    return true;
}

bool UtilityTableData::Validate(std::string& error) const
{
    error.clear();
    if (!std::isfinite(interval) || !std::isfinite(switchThreshold) || !std::isfinite(minimumDuration) ||
        interval <= 0 || switchThreshold < 0 || minimumDuration < 0) {
        error = "Interval must be positive; switch threshold and minimum duration must be nonnegative";
        return false;
    }
    std::unordered_set<std::string> names;
    for (const auto& parameter : parameters) {
        if (parameter.name.empty() || !names.insert(parameter.name).second) {
            error = "Parameters require unique nonempty names"; return false;
        }
        if (!std::isfinite(parameter.initialValue)) { error = "Parameter default must be finite"; return false; }
        // int имеет фиксированный 32-битный диапазон и точно представим в double.
        if (parameter.type == UtilityParameterType::Int && (std::trunc(parameter.initialValue) != parameter.initialValue ||
            parameter.initialValue < INT32_MIN || parameter.initialValue > INT32_MAX)) {
            error = "Invalid int default for " + parameter.name; return false;
        }
    }
    names.clear();
    for (const auto& action : actions) {
        if (action.name.empty() || !names.insert(action.name).second || !action.scriptPath.ends_with(".action.lua")) {
            error = "Actions require unique nonempty names and .action.lua paths"; return false;
        }
        if (action.curvePaths.size() != parameters.size()) { error = "Curve count must match parameters"; return false; }
        if (std::none_of(action.curvePaths.begin(), action.curvePaths.end(), [](const auto& path) { return !path.empty(); })) {
            error = "Action requires at least one curve: " + action.name; return false;
        }
    }
    if (actions.empty()) { error = "Table requires at least one action"; return false; }
    return true;
}

bool UtilityTableData::Save(const std::string& path, std::string& error) const
{
    if (!Validate(error)) return false;
    rapidjson::Document json(rapidjson::kObjectType);
    auto& allocator = json.GetAllocator();
    rapidjson::Value params(rapidjson::kArrayType), rows(rapidjson::kArrayType);
    for (const auto& parameter : parameters) {
        rapidjson::Value entry(rapidjson::kObjectType);
        entry.AddMember("name", Serialization::String(parameter.name, allocator), allocator);
        entry.AddMember("type", Serialization::String(parameter.type == UtilityParameterType::Int ? "int" : "float", allocator), allocator);
        entry.AddMember("default", parameter.initialValue, allocator);
        params.PushBack(entry, allocator);
    }
    for (const auto& action : actions) {
        rapidjson::Value entry(rapidjson::kObjectType), cells(rapidjson::kArrayType);
        entry.AddMember("name", Serialization::String(action.name, allocator), allocator);
        entry.AddMember("script", Serialization::String(action.scriptPath, allocator), allocator);
        for (const auto& path : action.curvePaths) {
            if (path.empty()) cells.PushBack(rapidjson::Value(), allocator);
            else cells.PushBack(Serialization::String(path, allocator), allocator);
        }
        entry.AddMember("curves", cells, allocator);
        rows.PushBack(entry, allocator);
    }
    json.AddMember("parameters", params, allocator);
    json.AddMember("interval", interval, allocator);
    json.AddMember("switch_threshold", switchThreshold, allocator);
    json.AddMember("minimum_duration", minimumDuration, allocator);
    json.AddMember("actions", rows, allocator);
    return Serialization::SaveFile(json, path, error);
}

bool UtilityAITable::load(const std::string& path)
{
    UtilityTableData description;
    if (!resources || !description.Read(path, loadError) || !description.Validate(loadError)) return false;
    // В runtime ячейки содержат заимствованные ссылки на кешированные кривые.
    std::vector<UtilityAction> loaded;
    for (auto& row : description.actions) {
        UtilityAction action{ std::move(row.name), std::move(row.scriptPath), {} };
        for (const auto& curvePath : row.curvePaths) {
            if (curvePath.empty()) { action.curves.push_back(nullptr); continue; }
            const auto* curve = resources->load<UtilityCurveResource>(curvePath);
            if (!curve) { loadError = resources->GetLastError(); return false; }
            action.curves.push_back(curve);
        }
        loaded.push_back(std::move(action));
    }
    parameters = std::move(description.parameters);
    actions = std::move(loaded);
    interval = description.interval;
    switchThreshold = description.switchThreshold;
    minimumDuration = description.minimumDuration;
    return true;
}
