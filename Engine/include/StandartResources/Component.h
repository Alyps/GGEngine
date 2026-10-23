#pragma once
class Game;

class Component {
public:
    virtual ~Component() = default;
    virtual void Init(Game& game) = 0;
    // Компоненты без покадровой логики используют пустую реализацию.
    virtual void Update(Game& game, double deltaTime) {}
};
