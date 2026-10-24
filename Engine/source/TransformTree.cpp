#include "TransformTree.h"

#include <algorithm>

TransformBinding::~TransformBinding() { bind_to_node(nullptr); }

void TransformBinding::bind_to_node(TransformNode* node) {
    if (node == transformNode) return;
    OnTransformDetached();
    if (previousBinding) previousBinding->nextBinding = nextBinding;
    else if (transformNode) transformNode->bindings = nextBinding;
    if (nextBinding) nextBinding->previousBinding = previousBinding;
    transformNode = node;
    previousBinding = nullptr;
    nextBinding = node ? node->bindings : nullptr;
    if (nextBinding) nextBinding->previousBinding = this;
    if (node) node->bindings = this;
}

TransformNode::TransformNode() = default;

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
    auto* child = new TransformNode(pos, rot, scl);
    child->parent = this;
    children.push_back(child);
    return child;
}

TransformNode* TransformNode::add_child() {
    return add_child(DXVector3::Zero, DXQuaternion::Identity, DXVector3::One);
}

bool TransformNode::remove_child(TransformNode* child) {
    if (!child) {
        return false;
    }

    const auto it = std::find(children.begin(), children.end(), child);
    if (it == children.end()) {
        return false;
    }

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
    while (bindings) bindings->bind_to_node(nullptr);
    for (TransformNode* child : children) {
        child->parent = nullptr;
        delete child;
    }
}
