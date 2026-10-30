#include "TransformTree.h"
#include "Object.h"

#include <algorithm>

TransformNode::TransformNode() = default;

TransformNode* TransformNode::find_child(const std::string& value) const {
    for (TransformNode* child : children)
        if (child->name == value) return child;
    return nullptr;
}

bool TransformNode::set_name(const std::string& value) {
    if (pendingDestroy || (owner && owner->isPendingDestroy()) || value.empty()) return false;
    TransformNode* sibling = parent ? parent->find_child(value) : nullptr;
    if (sibling && sibling != this) return false;
    name = value;
    return true;
}

bool TransformNode::contains(const TransformNode* node) const {
    for (; node; node = node->parent)
        if (node == this) return true;
    return false;
}

bool TransformNode::reparent(TransformNode& newParent) {
    if (!parent || pendingDestroy || newParent.pendingDestroy
        || (owner && owner->isPendingDestroy()) || contains(&newParent)) return false;
    // Даже отдельные деревья без GameObject нельзя незаметно объединять переносом.
    const TransformNode* root = this;
    while (root->parent) root = root->parent;
    if (!root->contains(&newParent)) return false;
    if (parent == &newParent) return true;
    if (newParent.find_child(name)) return false;
    newParent.children.push_back(this);
    parent->children.erase(std::find(parent->children.begin(), parent->children.end(), this));
    parent = &newParent;
    return true;
}

bool TransformNode::add_component(TransformableComponent& component) {
    if (pendingDestroy || (owner && owner->isPendingDestroy())
        || component.owner || component.transformNode || component.pendingDestroy) return false;
    components.push_back(&component);
    component.transformNode = this;
    component.owner = owner;
    // Объект только планирует Init и последующие Update; память принадлежит узлу.
    if (owner) owner->pendingComponents.push_back(&component);
    component.onTransformNodeChanged();
    return true;
}

bool TransformNode::remove_component(TransformableComponent* component) {
    if (!component || pendingDestroy || (owner && owner->isPendingDestroy())) return false;
    const auto it = std::find(components.begin(), components.end(), component);
    if (it == components.end()) return false;
    if (owner) component->pendingDestroy = true;
    else releaseComponent(component);
    return true;
}

bool TransformNode::move_component(TransformableComponent* component, TransformNode& destination) {
    if (!component || pendingDestroy || destination.pendingDestroy
        || (owner && owner->isPendingDestroy())) return false;
    const auto it = std::find(components.begin(), components.end(), component);
    if (it == components.end() || component->pendingDestroy) return false;
    const TransformNode* root = this;
    while (root->parent) root = root->parent;
    if (!root->contains(&destination)) return false;
    if (this == &destination) return true;
    destination.components.push_back(component);
    components.erase(it);
    component->transformNode = &destination;
    // Компонент обновляет заимствованные ссылки в своих Item без потери состояния.
    component->onTransformNodeChanged();
    return true;
}

void TransformNode::releaseComponent(TransformableComponent* component) {
    components.erase(std::find(components.begin(), components.end(), component));
    // Обратная ссылка доступна деструктору, но память узла ещё не освобождается.
    delete component;
}

void TransformNode::markPendingDestroy() {
    pendingDestroy = true;
    for (TransformableComponent* component : components) component->pendingDestroy = true;
    for (TransformNode* child : children) child->markPendingDestroy();
}

bool TransformNode::isEmptySubtree() const {
    if (!components.empty()) return false;
    for (const TransformNode* child : children)
        if (!child->isEmptySubtree()) return false;
    return true;
}

void TransformNode::applyPendingChanges() {
    for (auto it = children.begin(); it != children.end();) {
        TransformNode* child = *it;
        // Shutdown мог пометить уже обработанный компонент: ждём следующего кадра.
        if (child->pendingDestroy && child->isEmptySubtree()) {
            it = children.erase(it);
            delete child;
        } else {
            child->applyPendingChanges();
            ++it;
        }
    }
}

TransformNode::TransformNode(const DXVector3& pos,
    const DXQuaternion& rot,
    const DXVector3& scl)
    : position(pos), rotation(rot), scale(scl) {
}

const DXVector3& TransformNode::get_position() const {
    return position;
}

const DXQuaternion& TransformNode::get_rotation() const {
    return rotation;
}

const DXVector3& TransformNode::get_scale() const {
    return scale;
}

void TransformNode::set_position(const DXVector3& v) {
    position = v;
}

void TransformNode::set_rotation(const DXQuaternion& q) {
    rotation = q;
}

void TransformNode::set_scale(const DXVector3& v) {
    scale = v;
}

bool TransformNode::is_inherit_position() const {
    return inherit_position;
}

bool TransformNode::is_inherit_rotation() const {
    return inherit_rotation;
}

bool TransformNode::is_inherit_scale() const {
    return inherit_scale;
}

void TransformNode::set_inherit_position(bool v) {
    inherit_position = v;
}

void TransformNode::set_inherit_rotation(bool v) {
    inherit_rotation = v;
}

void TransformNode::set_inherit_scale(bool v) {
    inherit_scale = v;
}

TransformNode* TransformNode::get_parent() const {
    return parent;
}

const std::vector<TransformNode*>& TransformNode::get_children() const {
    return children;
}

TransformNode* TransformNode::add_child(const DXVector3& pos,
    const DXQuaternion& rot,
    const DXVector3& scl) 
{
    // Автоматическое имя не конфликтует с уже существующими слотами.
    std::string childName = "Node";
    for (size_t suffix = 1; find_child(childName); ++suffix)
        childName = "Node " + std::to_string(suffix);
    return add_child(childName, pos, rot, scl);
}

TransformNode* TransformNode::add_child(const std::string& childName,
    const DXVector3& pos, const DXQuaternion& rot, const DXVector3& scl) {
    if (pendingDestroy || (owner && owner->isPendingDestroy())
        || childName.empty() || find_child(childName)) return nullptr;
    auto* child = new (std::nothrow) TransformNode(pos, rot, scl);
    if (!child) return nullptr;
    child->name = childName;
    child->owner = owner;
    child->parent = this;
    children.push_back(child);
    return child;
}

TransformNode* TransformNode::add_child() {
    return add_child(DXVector3::Zero, DXQuaternion::Identity, DXVector3::One);
}

bool TransformNode::remove_child(TransformNode* child) {
    if (!child || pendingDestroy || (owner && owner->isPendingDestroy())) {
        return false;
    }

    const auto it = std::find(children.begin(), children.end(), child);
    if (it == children.end()) {
        return false;
    }

    // Живой объект сначала снимет регистрации компонентов в конце кадра.
    child->markPendingDestroy();
    if (owner) return true;
    children.erase(it);
    child->parent = nullptr;
    delete child;
    return true;
}

void TransformNode::rotate(DXVector3 axis, float rads) {
    axis.Normalize();
    rotation = DXQuaternion::CreateFromAxisAngle(axis, rads) * rotation;
}

void TransformNode::resize(float x, float y, float z) {
    scale *= DXVector3(x, y, z);
}

void TransformNode::move(float x, float y, float z) {
    position += DXVector3(x, y, z);
}

void TransformNode::move(DXVector3 value) {
    position += value;
}

void TransformNode::look_at(const DXVector3& target, const DXVector3& upAxis) {
    DXMatrix m = DXMatrix::CreateLookAt(position, target, upAxis);
    rotation = DXQuaternion::CreateFromRotationMatrix(m);
}

DXMatrix TransformNode::local_matrix() const {
    return DXMatrix::CreateScale(scale)
        * DXMatrix::CreateFromQuaternion(rotation)
        * DXMatrix::CreateTranslation(position);
}

DirectX::XMFLOAT4X4 TransformNode::worldMatrix() const {
    DXMatrix local = DXMatrix::CreateScale(scale)
        * DXMatrix::CreateFromQuaternion(rotation)
        * DXMatrix::CreateTranslation(position);

    if (parent) {
        DXMatrix parentWorld = parent->worldMatrix();

        DXVector3 pPos, pScale;
        DXQuaternion pRot;
        parentWorld.Decompose(pScale, pRot, pPos);

        if (!inherit_position) {
            pPos = DXVector3::Zero;
        }
        if (!inherit_rotation) {
            pRot = DXQuaternion::Identity;
        }
        if (!inherit_scale) {
            pScale = DXVector3::One;
        }

        DXMatrix parentFiltered = DXMatrix::CreateScale(pScale)
            * DXMatrix::CreateFromQuaternion(pRot)
            * DXMatrix::CreateTranslation(pPos);

        local = local * parentFiltered;
    }

    return local;
}

TransformNode::~TransformNode() {
    pendingDestroy = true;
    // У узлов GameObject все компоненты уже прошли Shutdown через общий индекс.
    for (TransformableComponent* component : components) delete component;
    for (TransformNode* child : children) {
        child->parent = nullptr;
        delete child;
    }
}

rapidjson::Value TransformNode::Serialize(rapidjson::Document::AllocatorType& allocator, std::string& error) const
{
    error.clear();
    if (pendingDestroy) { error = "Cannot serialize a transform pending destruction"; return {}; }
    rapidjson::Value json(rapidjson::kObjectType);
    json.AddMember("name", Serialization::String(name, allocator), allocator);
    // Локальная поза и наследование достаточны для восстановления мировой матрицы.
    json.AddMember("position", Serialization::Array({ position.x, position.y, position.z }, allocator), allocator);
    json.AddMember("rotation", Serialization::Array({ rotation.x, rotation.y, rotation.z, rotation.w }, allocator), allocator);
    json.AddMember("scale", Serialization::Array({ scale.x, scale.y, scale.z }, allocator), allocator);
    json.AddMember("inherit_position", inherit_position, allocator);
    json.AddMember("inherit_rotation", inherit_rotation, allocator);
    json.AddMember("inherit_scale", inherit_scale, allocator);
    // Проверяем собственные параметры до добавления уже проверенных вложенных данных.
    json = Serialization::Finish(std::move(json), error);
    if (json.IsNull()) return {};
    rapidjson::Value serializedComponents(rapidjson::kArrayType);
    for (const auto* component : components) {
        if (component->isPendingDestroy()) continue;
        auto entry = component->Serialize(allocator, error);
        if (entry.IsNull()) {
            error = name + "/" + component->GetSerializationType() + ": " + error;
            return {};
        }
        serializedComponents.PushBack(entry, allocator);
    }
    json.AddMember("components", serializedComponents, allocator);
    rapidjson::Value serializedChildren(rapidjson::kArrayType);
    for (const auto* child : children) {
        if (child->isPendingDestroy()) continue;
        auto entry = child->Serialize(allocator, error);
        if (entry.IsNull()) { error = name + "/" + error; return {}; }
        serializedChildren.PushBack(entry, allocator);
    }
    json.AddMember("children", serializedChildren, allocator);
    return json;
}

bool TransformNode::Deserialize(const rapidjson::Value& json, const ComponentFactoryMap& factories,
    std::string& error, unsigned depth)
{
    using namespace Deserialization;
    // Метод вызывается только при сборке нового объекта; ограничиваем рекурсию дерева.
    if (depth >= 256) { error = "Transform tree is too deep"; return false; }
    std::string loadedName;
    float p[3], r[4], s[3];
    bool ip, ir, is;
    if (!String(json, "name", loadedName, error) || !Floats(json, "position", p, 3, error) ||
        !Floats(json, "rotation", r, 4, error) || !Floats(json, "scale", s, 3, error) ||
        !Bool(json, "inherit_position", ip, error) || !Bool(json, "inherit_rotation", ir, error) ||
        !Bool(json, "inherit_scale", is, error)) return false;
    if (r[0] == 0 && r[1] == 0 && r[2] == 0 && r[3] == 0) {
        error = "Rotation quaternion must be nonzero"; return false;
    }
    if (!set_name(loadedName)) { error = "Empty or duplicate transform name: " + loadedName; return false; }
    position = DXVector3(p[0], p[1], p[2]);
    rotation = DXQuaternion(r[0], r[1], r[2], r[3]);
    scale = DXVector3(s[0], s[1], s[2]);
    inherit_position = ip;
    inherit_rotation = ir;
    inherit_scale = is;
    const auto* entries = Array(json, "components", error);
    if (!entries) return false;
    for (rapidjson::SizeType i = 0; i < entries->Size(); ++i) {
        Component* component = factories.Create((*entries)[i], error);
        if (!component) { error = name + "/components[" + std::to_string(i) + "]: " + error; return false; }
        auto* transformable = dynamic_cast<TransformableComponent*>(component);
        // Физические компоненты текущего движка могут работать только на корневом узле.
        const bool rootOnly = dynamic_cast<BoxColliderComponent*>(component) || dynamic_cast<RigidBodyComponent*>(component);
        if (!transformable || (parent && rootOnly) || !add_component(*transformable)) {
            delete component;
            error = name + "/components[" + std::to_string(i) + "]: incompatible component or node";
            return false;
        }
        // После add_component память принадлежит узлу, а Init остаётся в очереди объекта.
    }
    const auto* branches = Array(json, "children", error);
    if (!branches) return false;
    for (rapidjson::SizeType i = 0; i < branches->Size(); ++i) {
        std::string childName;
        if (!String((*branches)[i], "name", childName, error)) {
            error = name + "/children[" + std::to_string(i) + "]: " + error; return false;
        }
        TransformNode* child = add_child(childName);
        if (!child) { error = name + ": cannot create child (empty or duplicate name): " + childName; return false; }
        if (!child->Deserialize((*branches)[i], factories, error, depth + 1)) {
            error = name + "/" + childName + ": " + error; return false;
        }
    }
    return true;
}
