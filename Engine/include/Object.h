#pragma once
#include "TransformTree.h"
#include <vector>
#include "Components.h"
#include <utility>
#include <new>

class Game;
class Level;

class GameObject {
    friend class Level;
private:
	TransformNode transformTree;

	std::vector<Component*> components;
	std::vector<Component*> pendingComponents;
	Level* owner = nullptr;
	Game* ownerGame = nullptr;
	bool pendingDestroy = false;

public:
	GameObject() = default;
	~GameObject();
	// Успешное добавление передаёт объекту владение компонентом из new.
	bool addComponent(Component& component);
	template<class T, class... Args>
	T* createComponent(Args&&... args) {
		if (pendingDestroy) return nullptr;
		auto* component = new (std::nothrow) T(std::forward<Args>(args)...);
		// Конструктор мог запросить удаление владельца через внешний код.
		if (component && !addComponent(*component)) {
			delete component;
			return nullptr;
		}
		return component;
	}
	void destroyComponent(Component* component);
	void applyPendingChanges(Game& game);
	bool isPendingDestroy() const { return pendingDestroy; }
	void Init(Game& game);
	void Update(Game& game, double deltaTime);
	TransformNode& getTransform() { return transformTree;  }
	// Заимствованный список для инспектора; компоненты принадлежат объекту.
	const std::vector<Component*>& getComponents() const { return components; }
};
