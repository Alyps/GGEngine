#pragma once
#include "Component.h"
#include "TransformTree.h"

class TransformableComponent : public Component {
    friend struct TransformNode;
    friend class GameObject;
protected:
    // Узел владеет компонентом; обратная ссылка не владеющая.
    TransformNode* transformNode = nullptr;
    virtual void onTransformNodeChanged() {}
public:
    // UI редактора: перепривязка через передачу владения; nullptr не допускается.
    // Для ещё не закреплённого компонента из new передаёт владение узлу при успехе.
    bool bind_to_node(TransformNode* node) {
        if (!node) return false;
        return transformNode ? transformNode->move_component(this, *node) : node->add_component(*this);
    }
    const TransformNode* getTransformNode() const { return transformNode; }
};
