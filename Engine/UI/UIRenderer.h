#pragma once
#include "RmlUi_Renderer_DX11.h"

class Game;

// Backend владеет своей GPU-геометрией; ресурсы игры остаются у ResourceManager.
class UIRenderer final : public RenderInterface_DX11 {
    Game& game;
public:
    explicit UIRenderer(Game& owner);
    Rml::TextureHandle LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source) override;
};
