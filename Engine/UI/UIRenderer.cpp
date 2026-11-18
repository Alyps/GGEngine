#include "UIRenderer.h"
#include <RmlUi/Core/Log.h>
#include "core.h"
#include <cstring>

UIRenderer::UIRenderer(Game& owner)
    : RenderInterface_DX11(owner.getRenderServer().Device()), game(owner) {}

Rml::TextureHandle UIRenderer::LoadTexture(Rml::Vector2i& dimensions, const Rml::String& source)
{
    auto& render = game.getRenderServer();
    auto& resources = game.getResourceManager();
    auto* resource = resources.load<TextureResource>(UIServer::FilePath(source), render.Device());
    if (!resource) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "%s", resources.GetLastError().c_str());
        return 0;
    }
    // RmlUi ожидает premultiplied alpha в sRGB. Исходную текстуру 3D-материалов не меняем.
    // Копия создаётся только при загрузке изображения, затем кешируется самой RmlUi.
    const Texture& texture = resource->GetTexture();
    ID3D11Resource* gpu = nullptr;
    texture.SRV()->GetResource(&gpu);
    DirectX::ScratchImage captured, unpacked, converted;
    const HRESULT status = DirectX::CaptureTexture(render.Device(), render.Context(), gpu, captured);
    gpu->Release();
    if (FAILED(status) || captured.GetMetadata().dimension != DirectX::TEX_DIMENSION_TEXTURE2D
        || captured.GetMetadata().arraySize != 1) {
        Rml::Log::Message(Rml::Log::LT_ERROR, "UI requires a readable 2D texture: %s", source.c_str());
        return 0;
    }
    const DirectX::Image* image = captured.GetImage(0, 0, 0);
    if (DirectX::IsCompressed(image->format)) {
        if (FAILED(DirectX::Decompress(*image, DXGI_FORMAT_UNKNOWN, unpacked))) return 0;
        image = unpacked.GetImage(0, 0, 0);
    }
    // Сохраняем закодированные каналы sRGB: premultiply выполняется ниже в байтах.
    DirectX::Image encoded = *image;
    encoded.format = DirectX::MakeLinear(encoded.format);
    if (encoded.format != DXGI_FORMAT_R8G8B8A8_UNORM) {
        if (FAILED(DirectX::Convert(encoded, DXGI_FORMAT_R8G8B8A8_UNORM,
            DirectX::TEX_FILTER_DEFAULT, 0.5f, converted))) return 0;
        image = converted.GetImage(0, 0, 0);
    } else image = &encoded;

    std::vector<Rml::byte> pixels(image->width * image->height * 4);
    for (size_t y = 0; y < image->height; ++y) {
        auto* row = pixels.data() + y * image->width * 4;
        std::memcpy(row, image->pixels + y * image->rowPitch, image->width * 4);
        for (size_t x = 0; x < image->width; ++x)
            for (size_t channel = 0; channel < 3; ++channel)
                row[x * 4 + channel] = static_cast<Rml::byte>((row[x * 4 + channel] * row[x * 4 + 3]) / 255);
    }
    dimensions = { static_cast<int>(image->width), static_cast<int>(image->height) };
    return GenerateTexture({ pixels.data(), pixels.size() }, dimensions);
}
