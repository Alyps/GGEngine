#include "Physics/PhysicsWorld.h"
#include "Physics/PhysicsComponents.h"
#include "../Editor/PhysicsTestView.h"
#include <cmath>
#include <cstdio>
#include <limits>
#include <crtdbg.h>

namespace {
int failures = 0;
void Check(bool condition, const char* label) {
    std::printf("%s %s\n", condition ? "PASS" : "FAIL", label);
    if (!condition) ++failures;
}
bool Near(float a, float b, float tolerance = 0.002f) { return std::fabs(a - b) <= tolerance; }

class RenderProbe : public RenderComponent {
public:
    RenderProbe() { renderItem = new RenderItem; }
    void initRenderItem(Game&) override {}
    const RenderItem& Item() const { return *renderItem; }
};

struct Fixture {
    PhysicsWorld world;
    TransformNode floor{{0, -0.2f, 0}, DXQuaternion::Identity, {8, 0.4f, 8}};
    TransformNode cube{{-1, 5, 0}, DXQuaternion::CreateFromAxisAngle(DXVector3::UnitY, 0.45f)};
    TransformNode marker{{2, 1, -1}, DXQuaternion::Identity, {0.7f, 0.7f, 0.7f}};
    BoxColliderComponent collider{DXVector3::One};
    StaticRigidBodyComponent floorBody{collider};
    DynamicRigidBodyComponent cubeBody{collider, 2.0f};
    RenderProbe visual;
    bool Init(float gravity = -9.81f) {
        floorBody.bind_to_node(&floor);
        cubeBody.bind_to_node(&cube);
        visual.bind_to_node(&cube);
        return world.Init({0, gravity, 0}) && floorBody.Attach(world) && cubeBody.Attach(world);
    }
};

float Simulate(int renderHz, float gravity, bool inspect) {
    Fixture fixture;
    Check(fixture.Init(gravity), "scene initializes");
    Check(Near(fixture.cubeBody.GetMass(), 2.0f), "configured mass");
    const auto inertia = fixture.cubeBody.GetInertia();
    Check(Near(inertia.x, 1.0f / 3, 0.00001f) && Near(inertia.y, 1.0f / 3, 0.00001f) &&
        Near(inertia.z, 1.0f / 3, 0.00001f), "box inertia includes scaled dimensions");
    float fallingHeight = 0;
    int stepCount = 0;
    // Different render schedules, exactly the same 600 fixed updates, without wall-clock sleeps.
    for (int frame = 0; frame < renderHz * 10; ++frame) {
        const int steps = (frame + 1) * 60 / renderHz - frame * 60 / renderHz;
        for (int step = 0; step < steps; ++step) {
            if (!fixture.world.Step(1.0 / 60.0)) { Check(false, "fixed simulation step"); return 999; }
            if (++stepCount == 10) fallingHeight = fixture.cube.get_position().y;
        }
        fixture.visual.SyncTransform();
    }
    const auto position = fixture.cube.get_position();
    Check(fallingHeight < 5 && fallingHeight > 0.5f, "cube falls before contact");
    Check(Near(position.y, 0.5f), "cube rests at floor top plus box half height");
    Check(fixture.cubeBody.IsSleeping(), "cube reaches stable sleeping state");
    Check(fixture.floor.get_position() == DXVector3(0, -0.2f, 0), "static floor remains fixed");
    Check(fixture.marker.get_position() == DXVector3(2, 1, -1), "object without physics remains fixed");
    Check(fixture.cube.get_scale() == DXVector3::One, "dynamic scale is preserved");
    Check(Near(fixture.visual.Item().world._42, position.y) &&
        Near(fixture.visual.Item().world._11, fixture.cube.worldMatrix()._11),
        "render item follows physical transform and scale");
    const float restingHeight = position.y;
    for (int step = 0; step < 300; ++step) fixture.world.Step(1.0 / 60.0);
    Check(Near(fixture.cube.get_position().y, restingHeight, 0.00001f), "rest remains stable for five more seconds");
    if (inspect) std::printf("MEASURED gravity=%.2f steps=%d y=%.8f inertia=%.8f,%.8f,%.8f\n",
        gravity, stepCount, position.y, inertia.x, inertia.y, inertia.z);
    return position.y;
}

class PerspectiveProbe : public PhysicsTestBoxComponent {
public:
    PerspectiveProbe() : PhysicsTestBoxComponent(DXColor(1, 1, 1)) { renderItem = new RenderItem; }
    const RenderItem& Item() const { return *renderItem; }
};

void Perspective() {
    using namespace DirectX;
    Fixture fixture;
    Check(fixture.Init(), "3D fixture initializes");
    PerspectiveProbe visual;
    visual.bind_to_node(&fixture.cube);
    const DXMatrix viewProjection = PhysicsTestViewProjection(16.0f / 9.0f);
    visual.SetViewProjection(viewProjection);
    RenderComponent& component = visual;
    component.SyncTransform(); // Exercise the same virtual dispatch as Game::Run.
    const auto start = XMVector4Transform(XMVectorSet(0, 0, 0, 1), XMLoadFloat4x4(&visual.Item().world));
    const float w = XMVectorGetW(start);
    Check(w > 0 && std::fabs(XMVectorGetX(start)) < w && std::fabs(XMVectorGetY(start)) < w &&
        XMVectorGetZ(start) >= 0 && XMVectorGetZ(start) <= w, "falling cube starts inside the camera frustum");
    Check(std::fabs(visual.Item().world._14) + std::fabs(visual.Item().world._24) +
        std::fabs(visual.Item().world._34) > 0.1f, "submitted transform preserves perspective division");
    const DXVector3 eye(8, 7, -10);
    DXVector3 forward = DXVector3(0, 1.8f, 0) - eye;
    forward.Normalize();
    DXVector3 right = forward.Cross(DXVector3::UnitY);
    right.Normalize();
    const auto nearA = DXVector3::Transform(eye + forward * 4 - right * 0.5f, viewProjection);
    const auto nearB = DXVector3::Transform(eye + forward * 4 + right * 0.5f, viewProjection);
    const auto farA = DXVector3::Transform(eye + forward * 8 - right * 0.5f, viewProjection);
    const auto farB = DXVector3::Transform(eye + forward * 8 + right * 0.5f, viewProjection);
    Check(Near(std::fabs(farB.x - farA.x), std::fabs(nearB.x - nearA.x) * 0.5f, 0.0001f),
        "object at twice the distance projects to half the width");
    for (int step = 0; step < 600; ++step) fixture.world.Step(1.0 / 60.0);
    component.SyncTransform();
    const auto end = XMVector4Transform(XMVectorSet(0, 0, 0, 1), XMLoadFloat4x4(&visual.Item().world));
    Check(XMVectorGetY(end) / XMVectorGetW(end) < XMVectorGetY(start) / w,
        "physical fall moves the rendered cube downward in perspective");
    Check(Near(fixture.cube.get_position().y, 0.5f) && fixture.cube.get_scale() == DXVector3::One,
        "projection leaves physics position and scale in world coordinates");
}

void Lifetimes() {
    BoxColliderComponent collider{DXVector3::One};
    PhysicsWorld world;
    {
        DynamicRigidBodyComponent body{collider};
        {
            TransformNode node;
            body.bind_to_node(&node);
            Check(body.Attach(world), "body attaches before node destruction");
        }
        Check(!body.IsAttached() && world.Step(1.0 / 60.0), "node destruction safely removes actor");
    }
    TransformNode node;
    {
        DynamicRigidBodyComponent body{collider};
        body.bind_to_node(&node);
        Check(body.Attach(world), "component attaches before its destruction");
    }
    Check(world.Step(1.0 / 60.0), "component destruction leaves no stale registration");
    DynamicRigidBodyComponent survivor{collider};
    survivor.bind_to_node(&node);
    Check(survivor.Attach(world), "surviving component attaches");
    world.Shutdown();
    Check(!survivor.IsAttached(), "world shutdown detaches surviving component");
    Check(survivor.Attach(world), "world and component can be reinitialized");
    world.Shutdown();
    {
        BoxColliderComponent* temporary = new BoxColliderComponent({1, 2, 3});
        DynamicRigidBodyComponent independent{*temporary};
        delete temporary;
        independent.bind_to_node(&node);
        Check(independent.Attach(world), "body owns a snapshot of destroyed collider settings");
    }
    world.Shutdown();
}

void Validation() {
    const float nan = std::numeric_limits<float>::quiet_NaN();
    PhysicsWorld world;
    Check(!world.Init({0, nan, 0}) && !world.IsInitialized(), "invalid gravity rejected without partial state");
    BoxColliderComponent collider{DXVector3::One};
    DynamicRigidBodyComponent body{collider};
    Check(!body.Attach(world), "unbound body rejected");
    TransformNode root;
    TransformNode* child = root.add_child();
    child->set_inherit_position(false);
    body.bind_to_node(child);
    Check(!body.Attach(world), "parented body rejected even with inheritance disabled");
    body.bind_to_node(&root);
    root.set_scale({-1, 1, 1});
    Check(!body.Attach(world), "negative scale rejected");
    root.set_scale(DXVector3::One);
    root.set_rotation({0, 0, 0, 0});
    Check(!body.Attach(world), "invalid quaternion rejected");
    root.set_rotation(DXQuaternion::Identity);
    DynamicRigidBodyComponent badMass{collider, 0};
    badMass.bind_to_node(&root);
    Check(!badMass.Attach(world), "zero mass rejected");
    BoxColliderComponent badBox{{0, 1, 1}};
    DynamicRigidBodyComponent badSize{badBox};
    badSize.bind_to_node(&root);
    Check(!badSize.Attach(world), "zero box dimension rejected");
    BoxColliderComponent badMaterial{DXVector3::One, {0.6f, 0.5f, 1.5f}};
    DynamicRigidBodyComponent badBounce{badMaterial};
    badBounce.bind_to_node(&root);
    Check(!badBounce.Attach(world), "invalid restitution rejected");
    Check(body.Attach(world), "valid body attaches after rejected configurations");
    Check(!world.Step(0), "zero step rejected");
    root.set_scale({2, 1, 1});
    Check(!world.Step(1.0 / 60.0), "runtime scale change diagnosed");
    body.Detach();
    Check(body.Attach(world), "detach/reattach applies updated scale");
    body.Detach();
    world.Shutdown();
    // Exercise the partial-initialization failure path: PhysX permits one foundation per process.
    PhysicsWorld other;
    Check(world.Init(), "first SDK initializes");
    Check(!other.Init() && !other.IsInitialized(), "second foundation failure cleans partial state");
    world.Shutdown();
    Check(other.Init(), "initialization recovers after partial failure");
}

void Rotation() {
    PhysicsWorld world;
    TransformNode node{{1, 2, 3}, DXQuaternion::CreateFromAxisAngle(DXVector3::UnitZ, 0.5f), {2, 3, 4}};
    BoxColliderComponent collider{{1, 2, 3}};
    DynamicRigidBodyComponent body{collider, 12};
    body.bind_to_node(&node);
    Check(world.Init({0, 0, 0}) && body.Attach(world), "rotated nonuniform body initializes");
    Check(world.Step(1.0 / 60.0), "zero-gravity step completes");
    Check(Near(node.get_position().x, 1) && Near(node.get_position().y, 2) && Near(node.get_position().z, 3) &&
        Near(node.get_rotation().z, std::sin(0.25f)) && Near(node.get_rotation().w, std::cos(0.25f)),
        "current position and quaternion survive physical synchronization");
    const auto inertia = body.GetInertia();
    Check(Near(inertia.x, 180) && Near(inertia.y, 148) && Near(inertia.z, 40), "nonuniform collider inertia is correct");
}
}

int main() {
    // Warm up one-time runtime allocations before checking repeated SDK lifetimes.
    { PhysicsWorld warmup; if (!warmup.Init()) return 1; }
    _CrtMemState before{}, after{}, difference{};
    _CrtMemCheckpoint(&before);
    const float lowRate = Simulate(30, -9.81f, true);
    const float highRate = Simulate(144, -9.81f, true);
    Check(Near(lowRate, highRate, 0.00001f), "30 Hz and 144 Hz render schedules agree");
    Perspective();
    Rotation();
    Lifetimes();
    Validation();
    _CrtMemCheckpoint(&after);
    const bool leaks = _CrtMemDifference(&difference, &before, &after) != 0;
    Check(!leaks, "no net CRT heap allocations after physics lifetimes");
    if (leaks) _CrtMemDumpStatistics(&difference);
    std::printf("RESULT failures=%d\n", failures);
    return failures ? 1 : 0;
}
