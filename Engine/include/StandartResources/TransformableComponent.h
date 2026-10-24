#pragma once
#include "Component.h"
#include "TransformTree.h"

class TransformableComponent : public Component {
protected:
    // Узлом владеет объект; компонент только использует его.
    TransformNode* transformNode = nullptr;
public:
    void bind_to_node(TransformNode* node) { transformNode = node; }
    const TransformNode* getTransformNode() const { return transformNode; }
};
