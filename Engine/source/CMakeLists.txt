# Реализация сериализации компилируется только в библиотеку движка.
target_sources(Engine PRIVATE "ResourceManager.cpp")
target_sources(Engine PUBLIC
	"components.cpp"
	"StandartResources/RenderComponent.cpp"
	"StandartResources/MeshComponent.cpp"
	"StandartResources/EditorCamera.cpp"
	"StandartResources/CameraMovementComponent.cpp"
	"StandartResources/LightComponent.cpp"
	"core.cpp"
	"Delegates.cpp"
	"Engine.cpp"
	"InputDevice.cpp"
	"StandartResources/RenderPresets.cpp"
	"Object.cpp"
	"RenderServer.cpp"
	"Time.cpp"
	"TransformTree.cpp"
	"Window.cpp"
)
