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

rapidjson::Value GameObject::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    error.clear();
    if (pendingDestroy) { error = "Cannot serialize an object pending destruction"; return {}; }
    rapidjson::Value json(rapidjson::kObjectType), serializedComponents(rapidjson::kArrayType);
    json.AddMember("asset_type", "GameObject", allocator);
    json.AddMember("version", 1, allocator);
    // Общий индекс содержит компоненты дерева: сохраняем здесь только обычные компоненты.
    // Очередь учитывается без Init, чтобы можно было сохранить только что собранный объект.
    for (const auto* list : { &components, &pendingComponents }) {
        for (const auto* component : *list) {
            if (component->isPendingDestroy() || dynamic_cast<const TransformableComponent*>(component)) continue;
            auto entry = component->Serialize(allocator, error);
            if (entry.IsNull()) { error = std::string(component->GetSerializationType()) + ": " + error; return {}; }
            serializedComponents.PushBack(entry, allocator);
        }
    }
    json.AddMember("components", serializedComponents, allocator);
    auto tree = transformTree.Serialize(allocator, error);
    if (tree.IsNull()) return {};
    json.AddMember("transform_tree", tree, allocator);
    return json;
}

bool GameObject::Save(const std::string& path, std::string& error) const
{
    // Документ владеет памятью всех вложенных значений до завершения записи.
    rapidjson::Document document;
    auto json = Serialize(document.GetAllocator(), error);
    if (json.IsNull()) return false;
    return Serialization::SaveFile(json, path, error);
}

GameObject* GameObject::Deserialize(const rapidjson::Value& json, const ComponentFactoryMap& factories, std::string& error)
{
    if (!Deserialization::Asset(json, "GameObject", error)) return nullptr;
    const auto* entries = Deserialization::Array(json, "components", error);
    if (!entries) return nullptr;
    const auto* tree = Deserialization::Field(json, "transform_tree", error);
    if (!tree) return nullptr;
    auto* object = new (std::nothrow) GameObject();
    if (!object) { error = "Cannot allocate GameObject"; return nullptr; }
    // Незавершённым объектом владеет этот метод; его деструктор очищает все очереди.
    for (rapidjson::SizeType i = 0; i < entries->Size(); ++i) {
        Component* component = factories.Create((*entries)[i], error);
        if (!component) {
            error = "components[" + std::to_string(i) + "]: " + error;
            delete object; return nullptr;
        }
        if (dynamic_cast<TransformableComponent*>(component) || !object->addComponent(*component)) {
            delete component;
            delete object;
            error = "components[" + std::to_string(i) + "]: expected a component without transform";
            return nullptr;
        }
    }
    if (!object->transformTree.Deserialize(*tree, factories, error)) {
        error = "transform_tree: " + error;
        delete object; return nullptr;
    }
    return object;
}
