#pragma once
#include "GGMath.h"
#include <vector>
#include <algorithm>

struct TransformNode {
private:
	DXVector3 position = DXVector3::Zero;
	bool inherit_position = true;

	DXQuaternion rotation = DXQuaternion::Identity;
	bool inherit_rotation = true;

	DXVector3 scale = DXVector3::One    ;
	bool inherit_scale = true;

	TransformNode* parent = nullptr;
    std::vector<TransformNode*> children;

   

public:


    TransformNode(const TransformNode&) = delete;
    TransformNode& operator=(const TransformNode&) = delete;
    TransformNode(TransformNode&&) = delete;
    TransformNode& operator=(TransformNode&&) = delete;


    //Конструкторы
    TransformNode();
    TransformNode(const DXVector3& pos,
        const DXQuaternion& rot = DXQuaternion::Identity,
        const DXVector3& scl = DXVector3::One);

    //сеттеры геттеры
    const DXVector3& get_position() const;
    const DXQuaternion& get_rotation() const;
    const DXVector3& get_scale() const;

    void set_position(const DXVector3& v);
    void set_rotation(const DXQuaternion& q);
    void set_scale(const DXVector3& v);

    bool is_inherit_position() const;
    bool is_inherit_rotation() const;
    bool is_inherit_scale() const;

    void set_inherit_position(bool v);
    void set_inherit_rotation(bool v);
    void set_inherit_scale(bool v);

    //Иерархия
    TransformNode* get_parent() const;
    const std::vector<TransformNode*>& get_children() const;

    TransformNode* add_child(const DXVector3& pos,
        const DXQuaternion& rot = DXQuaternion::Identity,
        const DXVector3& scl = DXVector3::One);

    TransformNode* add_child();

    // ВАЖНО: только через этот метод или через удаление корня.
    bool remove_child(TransformNode* child);

    // Изменение
    void rotate(DXVector3 axis, float rads);

    void resize(float x, float y, float z);

    void move(float x, float y, float z);

    void move(DXVector3 value);

    void look_at(const DXVector3& target, const DXVector3& upAxis = DXVector3::UnitY);

    //Матрица
    DXMatrix local_matrix() const;

    DirectX::XMFLOAT4X4 worldMatrix() const;

    ~TransformNode();
};