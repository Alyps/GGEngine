#pragma once
#include "GGMath.h"
#include <vector>
#include <algorithm>
#include <string>
#include <new>
#include <utility>
#include <type_traits>

class GameObject;
class TransformableComponent;

struct TransformNode {
    friend class GameObject;
private:
    std::string name = "Root";
    GameObject* owner = nullptr;
    bool pendingDestroy = false;
    // Узел единолично владеет детьми и закреплёнными компонентами.
    std::vector<TransformableComponent*> components;
    void markPendingDestroy();
    void applyPendingChanges();
    void releaseComponent(TransformableComponent* component);
    bool isEmptySubtree() const;
	DXVector3 position = DXVector3::Zero;
	bool inherit_position = true;

	DXQuaternion rotation = DXQuaternion::Identity;
	bool inherit_rotation = true;

	DXVector3 scale = DXVector3::One    ;
	bool inherit_scale = true;

	TransformNode* parent = nullptr;
    std::vector<TransformNode*> children;

   

public:
    const std::string& get_name() const { return name; }
    // UI редактора: переименование слота; имя непустое и уникально среди соседей.
    bool set_name(const std::string& value);
    TransformNode* find_child(const std::string& value) const;
    GameObject* getOwner() const { return owner; }
    bool isPendingDestroy() const { return pendingDestroy; }
    bool contains(const TransformNode* node) const;
    // Заимствованный список включает ожидающие Init/удаления компоненты.
    const std::vector<TransformableComponent*>& get_components() const { return components; }

    // UI редактора: добавить компонент из new; владение передаётся только при успехе.
    bool add_component(TransformableComponent& component);
    // UI редактора: создание компонента выбранного типа непосредственно в слоте.
    template<class T, class... Args>
    T* create_component(Args&&... args) {
        static_assert(std::is_base_of_v<TransformableComponent, T>);
        if (pendingDestroy) return nullptr;
        auto* component = new (std::nothrow) T(std::forward<Args>(args)...);
        if (component && !add_component(*component)) {
            delete component;
            return nullptr;
        }
        return component;
    }
    // UI редактора: удаление компонента; у GameObject освобождение в applyPendingChanges.
    // Отдельный узел без объекта освобождает компонент сразу и не вызывает Init/Shutdown.
    bool remove_component(TransformableComponent* component);
    // UI редактора: перенос компонента между узлами одного дерева без повторного Init.
    // Ограничения конкретного компонента (например, физики на корне) остаются в силе.
    bool move_component(TransformableComponent* component, TransformNode& destination);
    // UI редактора: перенос ветки после обхода UI; локальный transform сохраняется.
    // Корень, циклы, другое дерево и совпадение имён запрещены.
    bool reparent(TransformNode& newParent);


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

    // UI редактора: создание именованного слота; при конфликте имени возвращает nullptr.
    TransformNode* add_child(const std::string& childName,
        const DXVector3& pos = DXVector3::Zero,
        const DXQuaternion& rot = DXQuaternion::Identity,
        const DXVector3& scl = DXVector3::One);

    // UI редактора: удаление ветки вместе с компонентами; корень удаляет только объект.
    // У GameObject удаление отложено; у отдельного дерева немедленное.
    // UI должен сбросить выбор/Undo до освобождения узлов.
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

    // Дочерние узлы удаляются только через remove_child или уничтожение всего дерева.
    ~TransformNode();
};
