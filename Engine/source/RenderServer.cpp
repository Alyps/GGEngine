#include "RenderServer.h"
#include <algorithm>
#include <filesystem>
#include <cwctype>
#include <cstring>

namespace {
ComPtr<ID3DBlob> CompileShaderFromFile(const std::wstring& path,
    const char* entryPoint, const char* target)
{
    ComPtr<ID3DBlob> byteCode;
    ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompileFromFile(
        path.c_str(), nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE,
        entryPoint, target,
        D3DCOMPILE_DEBUG | D3DCOMPILE_SKIP_OPTIMIZATION, 0,
        byteCode.GetAddressOf(), errors.GetAddressOf());

    if (FAILED(hr)) {
        std::wcerr << L"Shader compilation failed: " << path << L'\n';
        std::cerr << entryPoint << " (" << target << "), HRESULT: " << hr << '\n';
        if (errors) {
            std::cerr.write(static_cast<const char*>(errors->GetBufferPointer()),
                static_cast<std::streamsize>(errors->GetBufferSize()));
        }
        return {};
    }
    return byteCode;
}
} // namespace

ShaderPack ShaderPack::Create(ID3D11Device* device, const std::wstring& path,
    const VertexLayout& vertexLayout)
{
	// Return Guard
    if (!device || path.empty() || !vertexLayout.elements ||
        vertexLayout.elementCount == 0 || vertexLayout.stride == 0) {
        OutputDebugStringA("ShaderPack::Create: invalid device, path or vertex layout\n");
        return ShaderPack{};
    }

	// Компиляция шейдеров
    const auto vsByteCode = CompileShaderFromFile(path, "VSMain", "vs_5_0");
    if (!vsByteCode) return ShaderPack{};
    const auto psByteCode = CompileShaderFromFile(path, "PSMain", "ps_5_0");
    if (!psByteCode) return ShaderPack{};

	// Разметка
    ShaderPack shader;
    HRESULT hr = device->CreateInputLayout(
        vertexLayout.elements, vertexLayout.elementCount,
        vsByteCode->GetBufferPointer(), vsByteCode->GetBufferSize(),
        shader.layout.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "ShaderPack::Create: input layout failed, HRESULT: " << hr << '\n';
        return ShaderPack{};
    }

	// Установка шейдеров
    hr = device->CreateVertexShader(
        vsByteCode->GetBufferPointer(), vsByteCode->GetBufferSize(),
        nullptr, shader.vertexShader.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "ShaderPack::Create: vertex shader failed, HRESULT: " << hr << '\n';
        return ShaderPack{};
    }

    hr = device->CreatePixelShader(
        psByteCode->GetBufferPointer(), psByteCode->GetBufferSize(),
        nullptr, shader.pixelShader.GetAddressOf());
    if (FAILED(hr)) {
        std::cerr << "ShaderPack::Create: pixel shader failed, HRESULT: " << hr << '\n';
        return ShaderPack{};
    }
    return shader;
}

RenderState RenderState::CreateSimple(ID3D11Device& device)
{
	RenderState rs;
	D3D11_RASTERIZER_DESC rd{};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;
	rd.DepthClipEnable = TRUE;
	if (FAILED(device.CreateRasterizerState(&rd, rs.m_raster.ReleaseAndGetAddressOf())))
		return rs;

	D3D11_DEPTH_STENCIL_DESC dd{};
	dd.DepthEnable = FALSE;
	dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dd.DepthFunc = D3D11_COMPARISON_ALWAYS;
	if (FAILED(device.CreateDepthStencilState(&dd, rs.m_depth.ReleaseAndGetAddressOf())))
		return rs;

	D3D11_BLEND_DESC bd{};
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	device.CreateBlendState(&bd, rs.m_blend.ReleaseAndGetAddressOf());
	return rs;
}

RenderState RenderState::CreateOpaque(ID3D11Device& device)
{
	RenderState rs;

	// Rasterizer: обычная заливка, отсекаем задние грани
	D3D11_RASTERIZER_DESC rd{};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_BACK;
	rd.FrontCounterClockwise = FALSE;
	rd.DepthClipEnable = TRUE;
	device.CreateRasterizerState(&rd, rs.m_raster.ReleaseAndGetAddressOf());

	// DepthStencil: тест включён, пишем в depth
	D3D11_DEPTH_STENCIL_DESC dd{};
	dd.DepthEnable = TRUE;
	dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dd.DepthFunc = D3D11_COMPARISON_LESS;
	dd.StencilEnable = FALSE;
	device.CreateDepthStencilState(&dd, rs.m_depth.ReleaseAndGetAddressOf());

	// Blend: смешивание выключено
	D3D11_BLEND_DESC bd{};
	bd.AlphaToCoverageEnable = FALSE;
	bd.IndependentBlendEnable = FALSE;
	bd.RenderTarget[0].BlendEnable = FALSE;
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	device.CreateBlendState(&bd, rs.m_blend.ReleaseAndGetAddressOf());

	return rs;
}

RenderState RenderState::CreateTransparent(ID3D11Device& device)
{
	RenderState rs;

	D3D11_RASTERIZER_DESC rd{};
	rd.FillMode = D3D11_FILL_SOLID;
	rd.CullMode = D3D11_CULL_NONE;   // обычно для листвы/стёкол
	rd.FrontCounterClockwise = FALSE;
	rd.DepthClipEnable = TRUE;
	device.CreateRasterizerState(&rd, rs.m_raster.ReleaseAndGetAddressOf());

	// Depth: тест включён, но НЕ пишем в depth
	D3D11_DEPTH_STENCIL_DESC dd{};
	dd.DepthEnable = TRUE;
	dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ZERO;
	dd.DepthFunc = D3D11_COMPARISON_LESS;
	dd.StencilEnable = FALSE;
	device.CreateDepthStencilState(&dd, rs.m_depth.ReleaseAndGetAddressOf());

	// Blend: обычная alpha-прозрачность
	D3D11_BLEND_DESC bd{};
	bd.AlphaToCoverageEnable = FALSE;
	bd.IndependentBlendEnable = FALSE;
	auto& rt = bd.RenderTarget[0];
	rt.BlendEnable = TRUE;
	rt.SrcBlend = D3D11_BLEND_SRC_ALPHA;
	rt.DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
	rt.BlendOp = D3D11_BLEND_OP_ADD;
	rt.SrcBlendAlpha = D3D11_BLEND_ONE;
	rt.DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
	rt.BlendOpAlpha = D3D11_BLEND_OP_ADD;
	rt.RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	device.CreateBlendState(&bd, rs.m_blend.ReleaseAndGetAddressOf());

	return rs;
}

RenderState RenderState::CreateWireframe(ID3D11Device& device)
{
	RenderState rs;

	D3D11_RASTERIZER_DESC rd{};
	rd.FillMode = D3D11_FILL_WIREFRAME;
	rd.CullMode = D3D11_CULL_BACK;
	rd.FrontCounterClockwise = FALSE;
	rd.DepthClipEnable = TRUE;
	device.CreateRasterizerState(&rd, rs.m_raster.ReleaseAndGetAddressOf());

	// Depth/blend — как у opaque
	D3D11_DEPTH_STENCIL_DESC dd{};
	dd.DepthEnable = TRUE;
	dd.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
	dd.DepthFunc = D3D11_COMPARISON_LESS;
	device.CreateDepthStencilState(&dd, rs.m_depth.ReleaseAndGetAddressOf());

	D3D11_BLEND_DESC bd{};
	bd.RenderTarget[0].BlendEnable = FALSE;
	bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
	device.CreateBlendState(&bd, rs.m_blend.ReleaseAndGetAddressOf());

	return rs;
}
/*
void RenderState::Bind(ID3D11DeviceContext& context) const
{
	context.RSSetState(m_raster.Get());
	context.OMSetDepthStencilState(m_depth.Get(), 0);
	const float blendFactor[4] = { 0.f, 0.f, 0.f, 0.f };
	context.OMSetBlendState(m_blend.Get(), blendFactor, 0xFFFFFFFF);
}*/

Texture Texture::LoadFromFile(ID3D11Device* device, const std::wstring& path) //ID3D11Device* device,
{

	Texture tex;

	
	const std::filesystem::path fsPath(path);
	const std::wstring wpath = fsPath.wstring();

	std::wstring ext = fsPath.extension().wstring();
	for (auto& c : ext) c = static_cast<wchar_t>(std::towlower(c));

	DirectX::TexMetadata  metadata{};
	DirectX::ScratchImage image{};


	HRESULT hr = E_FAIL;
	if (ext == L".dds") hr = DirectX::LoadFromDDSFile(wpath.c_str(), DirectX::DDS_FLAGS_NONE, &metadata, image);
	else if (ext == L".tga") hr = DirectX::LoadFromTGAFile(wpath.c_str(), DirectX::TGA_FLAGS_NONE, &metadata, image);
	else if (ext == L".hdr") hr = DirectX::LoadFromHDRFile(wpath.c_str(), &metadata, image);
	else                     hr = DirectX::LoadFromWICFile(wpath.c_str(), DirectX::WIC_FLAGS_NONE, &metadata, image);

	

	const DirectX::ScratchImage* src = &image;
	DirectX::ScratchImage        mipChain;

	if (metadata.mipLevels <= 1 &&
		metadata.dimension == DirectX::TEX_DIMENSION_TEXTURE2D)
	{
		hr = DirectX::GenerateMipMaps(
			image.GetImages(),
			image.GetImageCount(),
			metadata,
			DirectX::TEX_FILTER_DEFAULT,
			0,
			mipChain);

		if (SUCCEEDED(hr)) {
			src = &mipChain;
		}
		// при неудаче молча используем оригинал
	}

	ComPtr<ID3D11ShaderResourceView> srv;
	hr = DirectX::CreateShaderResourceView(
		device,
		src->GetImages(),
		src->GetImageCount(),
		src->GetMetadata(),
		srv.ReleaseAndGetAddressOf());

	if (FAILED(hr) || !srv) {
		return tex;
	}

	tex.m_srv = std::move(srv);
	tex.m_width = static_cast<uint32_t>(metadata.width);
	tex.m_height = static_cast<uint32_t>(metadata.height);
	return tex;


}

/*
void Material::Bind(ID3D11DeviceContext& ctx)
{
	
		if (!m_shader) return;

		if (m_state)
			m_state->Bind(ctx);

		ctx.IASetInputLayout(m_shader->InputLayout());
		ctx.IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		ctx.VSSetShader(m_shader->VertexShader(), nullptr, 0);
		ctx.PSSetShader(m_shader->PixelShader(), nullptr, 0);

		ID3D11ShaderResourceView* srv = (m_diffuse && m_diffuse->IsValid())
			? m_diffuse->SRV() : nullptr;
		ctx.PSSetShaderResources(0, 1, &srv);

		if (m_sampler)
			ctx.PSSetSamplers(0, 1, &m_sampler);
		
}*/

void RenderServer::Init(const DisplayWindow32& target_window)
{


	// SwapChain
	D3D_FEATURE_LEVEL featureLevel[] = { D3D_FEATURE_LEVEL_11_1 };
	DXGI_SWAP_CHAIN_DESC swapDesc = {};
	swapDesc.BufferCount = 2;
	swapDesc.BufferDesc.Width = target_window.getWidth();
	swapDesc.BufferDesc.Height = target_window.getHeight();
	swapDesc.BufferDesc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	swapDesc.BufferDesc.RefreshRate.Numerator = 60;
	swapDesc.BufferDesc.RefreshRate.Denominator = 1;
	swapDesc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
	swapDesc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;
	swapDesc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
	swapDesc.OutputWindow = target_window.getWindow();
	swapDesc.Windowed = true;
	swapDesc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;
	swapDesc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH;
	swapDesc.SampleDesc.Count = 1;
	swapDesc.SampleDesc.Quality = 0;

	auto res = D3D11CreateDeviceAndSwapChain(
		nullptr,
		D3D_DRIVER_TYPE_HARDWARE,
		nullptr,
		D3D11_CREATE_DEVICE_DEBUG,
		featureLevel,
		1,
		D3D11_SDK_VERSION,
		&swapDesc,
		swapChain.ReleaseAndGetAddressOf(),
		device.ReleaseAndGetAddressOf(),
		nullptr,
		context.ReleaseAndGetAddressOf());
	if (FAILED(res)) {
		return;
	}

	// Создание RTV и DSV

	ComPtr<ID3D11Texture2D> backTex;
	res = swapChain->GetBuffer(0, __uuidof(ID3D11Texture2D),
		reinterpret_cast<void**>(backTex.ReleaseAndGetAddressOf()));
	if (FAILED(res)) {
		return;
	}
	res = device->CreateRenderTargetView(backTex.Get(), nullptr, rtv.ReleaseAndGetAddressOf());
	if (FAILED(res)) {
		return;
	}

	mainViewport.Width = static_cast<float>(target_window.getWidth());
	mainViewport.Height = static_cast<float>(target_window.getHeight());
	mainViewport.TopLeftX = 0;
	mainViewport.TopLeftY = 0;
	mainViewport.MinDepth = 0;
	mainViewport.MaxDepth = 1.0f;


	D3D11_TEXTURE2D_DESC depthDesc = {};
	depthDesc.Width = target_window.getWidth();
	depthDesc.Height = target_window.getHeight();
	depthDesc.MipLevels = 1;
	depthDesc.ArraySize = 1;
	depthDesc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
	depthDesc.SampleDesc.Count = 1;
	depthDesc.Usage = D3D11_USAGE_DEFAULT;
	depthDesc.BindFlags = D3D11_BIND_DEPTH_STENCIL;

	res = device->CreateTexture2D(&depthDesc, nullptr, depthStencilBuffer.ReleaseAndGetAddressOf());
	if (FAILED(res)) {
		return;
	}
	res = device->CreateDepthStencilView(depthStencilBuffer.Get(), nullptr, dsv.ReleaseAndGetAddressOf());
	if (FAILED(res)) {
		return;
	}



	// Создание CB
	// Пока что через обычное обновление
	frameCB = CreateConstantBuffer(sizeof(FrameCBData));
	objectCB = CreateConstantBuffer(sizeof(ObjectCBData));
}

ComPtr<ID3D11Buffer> RenderServer::CreateConstantBuffer(uint32_t byteWidth) const
{
	ComPtr<ID3D11Buffer> buffer;
	if (!device) return buffer;
	if (byteWidth == 0 || byteWidth % 16 != 0) {
		OutputDebugStringA("CreateConstantBuffer: byteWidth must be multiple of 16\n");
		return buffer;
	}

	D3D11_BUFFER_DESC desc = {};
	desc.ByteWidth = byteWidth;
	desc.Usage = D3D11_USAGE_DYNAMIC;
	desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
	desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
	desc.MiscFlags = 0;
	desc.StructureByteStride = 0;

	HRESULT hr = device->CreateBuffer(&desc, nullptr, buffer.ReleaseAndGetAddressOf());
	if (FAILED(hr)) {
		OutputDebugStringA("CreateConstantBuffer: CreateBuffer failed\n");
		buffer.Reset();
	}
	return buffer;
}

bool RenderServer::UpdateConstantBuffer(ID3D11Buffer* buffer, const void* data, size_t size)
{
	D3D11_MAPPED_SUBRESOURCE mapped{};
	HRESULT hr = context->Map(buffer, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
	if (FAILED(hr)) return false;
	std::memcpy(mapped.pData, data, size);
	context->Unmap(buffer, 0);
	return true;
}

void RenderServer::DrawAll()
{
	//Проверка чтобы всё было
	if (!context || !swapChain || !rtv || !dsv || !frameCB || !objectCB)
		return;

	//Даём значения константному буферу кадра. Пока что без камеры, поэтому identity.
	DirectX::XMStoreFloat4x4(&frameCBData.view, DirectX::XMMatrixIdentity()); 
	DirectX::XMStoreFloat4x4(&frameCBData.projection, DirectX::XMMatrixIdentity());
	if (!UpdateConstantBuffer(frameCB.Get(), &frameCBData, sizeof(frameCBData)))
		return;

	// Устанавливаем RenderTarget (куда записывать итоговый результат) 
	ID3D11RenderTargetView* target = rtv.Get(); //Зачем создаётся новый?
	context->OMSetRenderTargets(1, &target, dsv.Get()); //Output Merger выход
	context->RSSetViewports(1, &mainViewport); // размеры вьюпорта для растеризатора
	const float clearColor[4] = { 0.f, 0.f, 0.f, 1.f }; //Заполняем всё одним цветом
	context->ClearRenderTargetView(target, clearColor); //чистим
	context->ClearDepthStencilView(dsv.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.f, 0); //Чистим Z-буфер

	// Контракт HLSL: b0 = view/projection, b1 = world; матрицы column_major,
	// преобразование через mul(position, matrix). World транспонируется ниже.
	// Привязываем CB к шейдерам
	ID3D11Buffer* constantBuffers[] = { frameCB.Get(), objectCB.Get() }; 
	context->VSSetConstantBuffers(0, 2, constantBuffers);
	context->PSSetConstantBuffers(0, 2, constantBuffers);

	for (const RenderItem* item : renderList) {
		if (!item || !item->mesh || !item->material) // проверка, чтобы всё было
			continue;

		const Mesh& mesh = *item->mesh;
		const Material& material = *item->material;
		const ShaderPack* shader = material.Shader();
		if (!mesh.VertexBuffer() || !mesh.IndexBuffer() || mesh.IndexCount() == 0 ||
			!shader || !shader->IsValid())
			continue; //тоже проверка

		//Устанавливаем объектный буфер
		DirectX::XMStoreFloat4x4(&objectCBData.world,
			DirectX::XMMatrixTranspose(DirectX::XMLoadFloat4x4(&item->world)));
		if (!UpdateConstantBuffer(objectCB.Get(), &objectCBData, sizeof(objectCBData)))
			continue;

		// Отсутствующий state явно включает стандартное состояние D3D11.
		//Тянем настройки конвейера из материала и устанавливаем
		const RenderState* state = material.State();
		context->RSSetState(state ? state->Raster() : nullptr);
		context->OMSetDepthStencilState(state ? state->Depth() : nullptr, 0);
		context->OMSetBlendState(state ? state->Blend() : nullptr, nullptr, 0xFFFFFFFF);

		//Тянем шейдеры и разметку из материала и устанавливаем
		context->IASetInputLayout(shader->InputLayout());
		context->VSSetShader(shader->VertexShader(), nullptr, 0);
		context->PSSetShader(shader->PixelShader(), nullptr, 0);

		//diffuse это основная текстура объекта. Устанавливаем её. И сэмплер для неё
		const Texture* diffuse = material.Diffuse();
		ID3D11ShaderResourceView* srv = diffuse ? diffuse->SRV() : nullptr;
		ID3D11SamplerState* materialSampler = material.Sampler();
		// t0/s0 задаются для каждого объекта, включая отсутствие ресурса.
		context->PSSetShaderResources(0, 1, &srv);
		context->PSSetSamplers(0, 1, &materialSampler);

		//Передача вершин на отрисовку, вместе с топологией
		ID3D11Buffer* vertexBuffer = mesh.VertexBuffer();
		const UINT stride = mesh.Layout().stride;
		const UINT offset = 0;
		context->IASetVertexBuffers(0, 1, &vertexBuffer, &stride, &offset);
		context->IASetPrimitiveTopology(mesh.Topology());
		context->IASetIndexBuffer(mesh.IndexBuffer(), DXGI_FORMAT_R32_UINT, 0);
		context->DrawIndexed(mesh.IndexCount(), 0, 0); //Сама отрисовка
	}

	//Показ кадра
	if (FAILED(swapChain->Present(1, 0)))
		OutputDebugStringA("DrawAll: Present failed\n");
}

void RenderServer::addItem(RenderItem& item)
{
	renderList.push_back(&item);
}

void RenderServer::removeItem(RenderItem& item)
{
	const auto it = std::find(renderList.begin(), renderList.end(), &item);
	if (it != renderList.end())
		renderList.erase(it);
}
