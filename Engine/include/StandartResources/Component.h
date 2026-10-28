#pragma once
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
    virtual void Init(Game& game) = 0;
    // Снимает регистрации до освобождения памяти компонента.
    virtual void Shutdown(Game& game) {}
    // Компоненты без покадровой логики используют пустую реализацию.
    virtual void Update(Game& game, double deltaTime) {}
};
