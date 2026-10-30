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
    friend struct TransformNode;
private:
	TransformNode transformTree;

	// Для TransformableComponent эти списки — только индекс жизненного цикла.
	std::vector<Component*> components;
	std::vector<Component*> pendingComponents;
	Level* owner = nullptr;
	bool pendingDestroy = false;

public:
	GameObject() { transformTree.owner = this; }
	// Полное описание ассета; сериализация не запускает жизненный цикл.
	rapidjson::Value Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const;
	bool Save(const std::string& path, std::string& error) const;
	// Возвращает новый объект во владение вызывающему коду; Init не вызывается.
	static GameObject* Deserialize(const rapidjson::Value& json, const ComponentFactoryMap& factories, std::string& error);
	~GameObject();
	// Обычным компонентом владеет объект, TransformableComponent передаётся корневому узлу.
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
	Level* getOwner() const { return owner; }
	Game* getGame() const;
	bool isPendingDestroy() const { return pendingDestroy; }
	TransformNode& getTransform() { return transformTree;  }
	// Заимствованный общий индекс для инспектора и Lua, включая компоненты всех узлов.
	const std::vector<Component*>& getComponents() const { return components; }
private:
	void releaseComponent(Component* component);
	void applyPendingChanges();
	void Update(double deltaTime);
};
