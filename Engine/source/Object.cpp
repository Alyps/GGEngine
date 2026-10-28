#include "Object.h"
#include "core.h"

Game* GameObject::getGame() const
{
    return owner ? owner->getOwner() : nullptr;
}

GameObject::~GameObject()
{
    pendingDestroy = true;
    // Сначала компоненты: их Shutdown ещё может использовать Transform.
    for (Component* component : components) {
        if (component->initialized) component->Shutdown(*getGame());
    }
    // Все Shutdown завершены, поэтому освобождение не ломает ссылки между компонентами.
    for (Component* component : components) releaseComponent(component);
    for (Component* component : pendingComponents) releaseComponent(component);
}

void GameObject::releaseComponent(Component* component)
{
    if (auto* transformable = dynamic_cast<TransformableComponent*>(component))
        transformable->transformNode->releaseComponent(transformable);
    else delete component;
}

bool GameObject::addComponent(Component& component)
{
    if (pendingDestroy || component.owner || component.pendingDestroy) return false;
    if (auto* transformable = dynamic_cast<TransformableComponent*>(&component))
        return transformTree.add_component(*transformable);
    component.owner = this;
    pendingComponents.push_back(&component);
    return true;
}

void GameObject::destroyComponent(Component* component)
{
    if (component && component->owner == this) component->pendingDestroy = true;
}

void GameObject::applyPendingChanges()
{
    if (!getGame()) return;
    Game& game = *getGame();
    // Новые запросы из Init/Shutdown остаются в очереди до следующего кадра.
    std::vector<Component*> additions;
    additions.swap(pendingComponents);
    for (auto it = components.begin(); it != components.end();) {
        Component* component = *it;
        if (!component->pendingDestroy) { ++it; continue; }
        it = components.erase(it);
        if (component->initialized) component->Shutdown(game);
        releaseComponent(component);
    }
    // Сначала переносим весь пакет, чтобы Init не менял текущую очередь.
    const size_t first = components.size();
    components.insert(components.end(), additions.begin(), additions.end());
    for (size_t i = first; i < components.size();) {
        Component* component = components[i];
        if (pendingDestroy || component->pendingDestroy) {
            components.erase(components.begin() + i);
            releaseComponent(component);
            continue;
        }
        component->initialized = true;
        component->Init(game);
        ++i;
    }
    transformTree.applyPendingChanges();
}

void GameObject::Update(double deltaTime)
{
    if (!getGame()) return;
    Game& game = *getGame();
    for (Component* component : components) {
        if (pendingDestroy) break;
        if (!component->pendingDestroy) component->Update(game, deltaTime);
    }
}
