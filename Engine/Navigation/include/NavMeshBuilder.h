#pragma once
#if defined(GG_EDITOR)
#include "NavigationItem.h"
#include <cstdint>
#include <string>
#include <thread>

class Level;
class NavigationServer;

// Генератор статического навмеша (Recast); существует только в редакторе, владеет им Editor.
// Start в главном потоке копирует геометрию уровня и запускает рабочий поток, который
// строит тайлы и записывает файл. Рабочий поток не обращается к уровню, компонентам
// и системам Game: он видит только скопированные данные своей задачи.
class NavMeshBuilder {
public:
    enum class State { Idle, Running, Succeeded, Failed, Cancelled };

    NavMeshBuilder() = default;
    // Отменяет и дожидается рабочего потока.
    ~NavMeshBuilder();
    NavMeshBuilder(const NavMeshBuilder&) = delete;
    NavMeshBuilder& operator=(const NavMeshBuilder&) = delete;

    // Источники геометрии: BoxCollider и Mesh объектов без DynamicRigidBody.
    // Параметры берутся у первого объёма уровня. resolvedPath — путь файла на диске (UTF-8).
    bool Start(Level& level, const std::string& resolvedPath, std::string& error);
    // Вызывать каждый кадр. Возвращает true один раз, когда задача завершилась (с любым итогом).
    bool Poll();
    // После успешного Poll передаёт тайлы системе навигации; результат одноразовый.
    bool Apply(NavigationServer& server, std::string& error);
    // Просит рабочий поток остановиться после текущего тайла; итог придёт через Poll.
    void Cancel();
    // Отменяет и ждёт поток. Вызывать перед заменой уровня и при закрытии редактора.
    void CancelAndJoin();

    State getState() const { return state; }
    bool isRunning() const { return state == State::Running; }
    int getTilesDone() const;
    int getTilesTotal() const;
    // Итог последней задачи для интерфейса и предупреждения/ошибки Recast.
    const std::string& getMessage() const { return message; }
    const std::string& getLog() const { return log; }

    // Хеш входа (геометрия, объёмы, параметры). Совпадает с хешем, записанным при генерации,
    // пока сцена не изменилась; редактор так определяет устаревший навмеш.
    static std::uint64_t ComputeInputHash(Level& level);

private:
    struct Job;
    // Владеющий указатель. Пока поток жив, он пишет только в Job; главный поток читает
    // атомарные счётчики, а результат — после finished.
    Job* job = nullptr;
    std::thread worker;
    State state = State::Idle;
    std::string message, log;
    static void Run(Job* job);
};
#endif
