#pragma once
#include "GGMath.h"
#include <string>
#include <vector>

struct TransformNode;
class NavigationServer;

// Параметры агента и Recast. Хранятся в объёме навмеша и сериализуются вместе с ним.
// Линейные величины в метрах, угол в градусах; пересчёт в ячейки делает генератор.
struct NavBuildSettings {
    // Агент: радиус, рост, максимальная ступенька и наклон, по которому он может идти.
    float agentRadius = 0.4f;
    float agentHeight = 1.8f;
    float agentMaxClimb = 0.4f;
    float agentMaxSlope = 45.0f;
    // Вокселизация: размер ячейки по XZ, высота ячейки по Y и сторона тайла в ячейках.
    float cellSize = 0.2f;
    float cellHeight = 0.1f;
    int tileSize = 64;
    // Регионы: острова меньше regionMinSize^2 ячеек удаляются, мелкие регионы сливаются.
    float regionMinSize = 8.0f;
    float regionMergeSize = 20.0f;
    // Упрощение контуров: максимальная длина ребра (м) и отклонение от исходной границы (ячейки).
    float edgeMaxLength = 12.0f;
    float edgeMaxError = 1.3f;
    // Детальный меш высот: шаг выборки (в ячейках, 0 — выключено) и допустимая ошибка (в ячейках по Y).
    float detailSampleDistance = 6.0f;
    float detailSampleMaxError = 1.0f;

    bool operator==(const NavBuildSettings&) const = default;
    // Проверяет диапазоны и вырожденные сочетания (например, рост меньше трёх ячеек).
    bool Validate(std::string& error) const;
};

// Компонент владеет записью; NavigationServer заимствует её только между Init и Shutdown.
// Объём — box размером size в пространстве узла: учитываются позиция, поворот и масштаб узла.
struct NavMeshVolumeItem {
    TransformNode* transformNode = nullptr;
    DXVector3 size = DXVector3(10.0f, 4.0f, 10.0f); // Полные локальные размеры box.
    NavBuildSettings settings;
    // Путь к запечённым данным относительно корня проекта; пустой — навмеш ещё не строился.
    std::string dataPath;

    NavMeshVolumeItem() = default;
    ~NavMeshVolumeItem();
    NavMeshVolumeItem(const NavMeshVolumeItem&) = delete;
    NavMeshVolumeItem& operator=(const NavMeshVolumeItem&) = delete;
    bool isRegistered() const { return server != nullptr; }

private:
    friend class NavigationServer;
    NavigationServer* server = nullptr;
};

enum class NavAgentState { Idle, Moving, Arrived, Failed };

// Параметры следования по пути сохраняются в сцене; скорости в м/с, поворот в градусах/с.
struct NavAgentSettings {
    float speed = 3.5f;
    float acceleration = 8.0f;
    float angularSpeed = 360.0f;
    float stoppingDistance = 0.1f;
    bool updateRotation = true;

    bool operator==(const NavAgentSettings&) const = default;
    // Проверяет конечность и диапазоны, совпадающие с полями инспектора.
    bool Validate(std::string& error) const;
};

// Компонент владеет записью. Агент независим: обхода других агентов и логики толпы нет.
struct NavAgentItem {
    TransformNode* transformNode = nullptr;
    NavAgentSettings settings;
    NavAgentState state = NavAgentState::Idle;
    DXVector3 destination = DXVector3::Zero;
    // Углы текущего пути и индекс угла, к которому агент идёт.
    std::vector<DXVector3> path;
    size_t nextCorner = 0;
    bool partial = false;
    float currentSpeed = 0.0f;
    float remainingDistance = 0.0f;
    // Обнаружение застревания: время без движения и признак уже сделанной перестройки пути.
    float stuckTime = 0.0f;
    bool replanned = false;
    DXVector3 lastPosition = DXVector3::Zero;
    // До первого Update уровень ещё инициализируется: путь откладывается на первый кадр.
    bool started = false;
    bool planPending = false;
    std::string lastError;
};
