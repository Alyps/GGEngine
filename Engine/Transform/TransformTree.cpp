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
