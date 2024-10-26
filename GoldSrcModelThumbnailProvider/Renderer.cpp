#include <Windows.h>
#include <wrl/client.h>
#include <d3d11_1.h>
#include <dxgi1_2.h>
#include <DirectXMath.h>

#include <memory>
#include <string>
#include <spdlog/spdlog.h>

#include "StudioModelRenderer.hpp"

using Microsoft::WRL::ComPtr;


#ifdef _MSC_VER
#pragma comment ( lib, "d3d11.lib" )
#pragma comment ( lib, "Dxgi.lib" )
#pragma comment ( lib, "dxguid.lib" )
#endif


class Renderer
{
private:

	UINT m_ViewWidth = 256;
	UINT m_ViewHeight = 256;

	D3D_DRIVER_TYPE m_driverType = D3D_DRIVER_TYPE_NULL;
	D3D_FEATURE_LEVEL m_featureLevel = D3D_FEATURE_LEVEL_11_0;

	ComPtr<ID3D11Device> m_D3DDevice;
	ComPtr<ID3D11Device1> m_D3DDevice1;
	ComPtr<ID3D11DeviceContext> m_D3DDeviceContext;
	ComPtr<ID3D11DeviceContext1> m_D3DDeviceContext1;
	ComPtr<ID3D11RenderTargetView> m_RenderTargetView;
	ComPtr<ID3D11Texture2D> m_DepthStencilTexture;
	ComPtr<ID3D11DepthStencilView> m_DepthStencilView;
	ComPtr<ID3D11RasterizerState> m_RasterizerState;
	ComPtr<ID3D11Debug> m_D3DDebug;

	ComPtr<ID3D11Texture2D> m_RenderTargetTexture;
	ComPtr<ID3D11Texture2D> m_BufferTexture;

	std::unique_ptr<D3DStudioModel> m_d3dStudioModel;
	std::unique_ptr<D3DStudioModelRenderer> m_d3dStudioModelRenderer;


	HRESULT InitD3D()
	{
		HRESULT hr;

		UINT createDeviceFlags = 0;
#ifdef _DEBUG
		createDeviceFlags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

		D3D_DRIVER_TYPE driverTypes[] =
		{
			D3D_DRIVER_TYPE_HARDWARE,
			D3D_DRIVER_TYPE_WARP,
			D3D_DRIVER_TYPE_REFERENCE,
		};
		UINT numDriverTypes = ARRAYSIZE(driverTypes);

		D3D_FEATURE_LEVEL featureLevels[] =
		{
			D3D_FEATURE_LEVEL_11_1,
			D3D_FEATURE_LEVEL_11_0,
			D3D_FEATURE_LEVEL_10_1,
			D3D_FEATURE_LEVEL_10_0,
		};
		UINT numFeatureLevels = ARRAYSIZE(featureLevels);

		for (UINT driverTypeIndex = 0; driverTypeIndex < numDriverTypes; driverTypeIndex++)
		{
			m_driverType = driverTypes[driverTypeIndex];
			hr = D3D11CreateDevice(nullptr, m_driverType, nullptr, createDeviceFlags, featureLevels, numFeatureLevels,
				D3D11_SDK_VERSION, m_D3DDevice.ReleaseAndGetAddressOf(), &m_featureLevel, m_D3DDeviceContext.ReleaseAndGetAddressOf());

			if (hr == E_INVALIDARG)
			{
				// DirectX 11.0 platforms will not recognize D3D_FEATURE_LEVEL_11_1 so we need to retry without it
				hr = D3D11CreateDevice(nullptr, m_driverType, nullptr, createDeviceFlags, &featureLevels[1], numFeatureLevels - 1,
					D3D11_SDK_VERSION, m_D3DDevice.ReleaseAndGetAddressOf(), &m_featureLevel, m_D3DDeviceContext.ReleaseAndGetAddressOf());
			}

			if (SUCCEEDED(hr))
				break;
		}

		spdlog::trace("Call D3D11CreateDevice at {} line {} result {}", __FUNCTION__, __LINE__, hr);
		if (FAILED(hr))
			return hr;

#if _DEBUG
		hr = m_D3DDevice->QueryInterface(IID_PPV_ARGS(m_D3DDebug.ReleaseAndGetAddressOf()));

		if (FAILED(hr))
			return hr;
#endif

		UINT m4xMsaaQuality;
		hr = m_D3DDevice->CheckMultisampleQualityLevels(DXGI_FORMAT_R8G8B8A8_UNORM, 4, &m4xMsaaQuality);
		spdlog::trace("Call m_D3DDevice->CheckMultisampleQualityLevels at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		if (FAILED(hr))
			return hr;

		//
		// Create a texture as render target
		//

		D3D11_TEXTURE2D_DESC desc{};
		desc.Width = m_ViewWidth;
		desc.Height = m_ViewHeight;
		desc.ArraySize = 1;
		desc.SampleDesc.Count = 1;
		desc.SampleDesc.Quality = 0;
		desc.MipLevels = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

		hr = m_D3DDevice->CreateTexture2D(&desc, NULL, m_RenderTargetTexture.ReleaseAndGetAddressOf());
		spdlog::trace("Call m_D3DDevice->CreateTexture2D at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		if (FAILED(hr))
			return hr;

		//
		// Create a buffer
		//

		desc.BindFlags = 0;
		desc.Usage = D3D11_USAGE_STAGING;
		desc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;

		hr = m_D3DDevice->CreateTexture2D(&desc, NULL, m_BufferTexture.ReleaseAndGetAddressOf());
		spdlog::trace("Call m_D3DDevice->CreateTexture2D at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		if (FAILED(hr))
			return hr;

		//
		// Create view of the render target
		//

		hr = m_D3DDevice->CreateRenderTargetView(m_RenderTargetTexture.Get(), NULL, m_RenderTargetView.ReleaseAndGetAddressOf());
		spdlog::trace("Call m_D3DDevice->CreateRenderTargetView at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		if (FAILED(hr))
			return hr;

		// 
		// Create texture for depth stencil
		//

		D3D11_TEXTURE2D_DESC dstd{};
		dstd.Width = m_ViewWidth;
		dstd.Height = m_ViewHeight;
		dstd.MipLevels = 1;
		dstd.ArraySize = 1;
		dstd.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		dstd.SampleDesc.Count = 1;
		dstd.SampleDesc.Quality = 0;
		dstd.Usage = D3D11_USAGE_DEFAULT;
		dstd.BindFlags = D3D11_BIND_DEPTH_STENCIL;
		dstd.CPUAccessFlags = 0;
		dstd.MiscFlags = 0;

		hr = m_D3DDevice->CreateTexture2D(&dstd, nullptr, m_DepthStencilTexture.ReleaseAndGetAddressOf());
		spdlog::trace("Call m_D3DDevice->CreateTexture2D at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		if (FAILED(hr))
			return hr;

		//
		// Create view of depth stencil texture
		//

		D3D11_DEPTH_STENCIL_VIEW_DESC dsvd{};
		dsvd.Format = dstd.Format;
		dsvd.ViewDimension = D3D11_DSV_DIMENSION_TEXTURE2D;
		dsvd.Texture2D.MipSlice = 0;

		hr = m_D3DDevice->CreateDepthStencilView(m_DepthStencilTexture.Get(), 0, m_DepthStencilView.ReleaseAndGetAddressOf());
		spdlog::trace("Call m_D3DDevice->CreateDepthStencilView at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		if (FAILED(hr))
			return hr;

		//
		// Our render target view and depth stencil view are ready
		//

		ID3D11RenderTargetView* renderTargetViews[] =
		{
			m_RenderTargetView.Get()
		};

		m_D3DDeviceContext->OMSetRenderTargets(ARRAYSIZE(renderTargetViews), renderTargetViews, m_DepthStencilView.Get());

		//
		// Create rasterizer state
		//

		D3D11_RASTERIZER_DESC rasterDesc{};
		rasterDesc.AntialiasedLineEnable = false;
		rasterDesc.CullMode = D3D11_CULL_BACK;
		rasterDesc.DepthBias = 0;
		rasterDesc.DepthBiasClamp = 0.0f;
		rasterDesc.DepthClipEnable = true;
		rasterDesc.FillMode = D3D11_FILL_SOLID;
		rasterDesc.FrontCounterClockwise = false;
		rasterDesc.MultisampleEnable = false;
		rasterDesc.ScissorEnable = false;
		rasterDesc.SlopeScaledDepthBias = 0.0f;

		hr = m_D3DDevice->CreateRasterizerState(&rasterDesc, m_RasterizerState.ReleaseAndGetAddressOf());
		spdlog::trace("Call m_D3DDevice->CreateRasterizerState at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		if (FAILED(hr))
			return hr;

		//
		// Use rasterizer state
		//

		m_D3DDeviceContext->RSSetState(m_RasterizerState.Get());

		//
		// Finally, we setup the viewport
		//

		D3D11_VIEWPORT viewPort{};
		viewPort.TopLeftX = 0;
		viewPort.TopLeftY = 0;
		viewPort.MinDepth = 0.0f;
		viewPort.MaxDepth = 1.0f;
		viewPort.Width = static_cast<FLOAT>(m_ViewWidth);
		viewPort.Height = static_cast<FLOAT>(m_ViewHeight);

		m_D3DDeviceContext->RSSetViewports(1, &viewPort);

		//
		// Now the window is ready
		//

		return hr;
	}


	void CleanD3D()
	{
		m_d3dStudioModelRenderer.reset();
		m_d3dStudioModel.reset();

		m_RasterizerState.Reset();
		m_DepthStencilView.Reset();
		m_DepthStencilTexture.Reset();
		m_RenderTargetView.Reset();
		m_D3DDeviceContext1.Reset();
		m_D3DDevice1.Reset();
		m_D3DDeviceContext.Reset();

		m_RenderTargetTexture.Reset();
		m_BufferTexture.Reset();

#if _DEBUG
		if (m_D3DDebug)
		{
			OutputDebugString(TEXT("Dumping DirectX 11 live objects.\n"));
			m_D3DDebug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL);
			m_D3DDebug.Reset();
		}
#endif

		m_D3DDevice.Reset();
	}


	void RenderFrame()
	{
		float clearColor[4] = { 0.2f, 0.5f, 0.698f, 1.0f };
		m_D3DDeviceContext->ClearRenderTargetView(m_RenderTargetView.Get(), clearColor);
		m_D3DDeviceContext->ClearDepthStencilView(m_DepthStencilView.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

		if (m_d3dStudioModel && m_d3dStudioModelRenderer)
		{
			m_d3dStudioModelRenderer->SetViewport(m_ViewWidth, m_ViewHeight);
			m_d3dStudioModelRenderer->SetModel(m_d3dStudioModel.get());
			m_d3dStudioModelRenderer->Draw();
		}

		m_D3DDeviceContext->Flush();
	}


	HRESULT CreateDIB(UINT width, UINT height, void* data, UINT stride, HBITMAP* result)
	{
		BITMAPINFO bmi{};

		bmi.bmiHeader.biSize = sizeof(bmi.bmiHeader);
		bmi.bmiHeader.biWidth = static_cast<LONG>(width);
		bmi.bmiHeader.biHeight = -static_cast<LONG>(height);
		bmi.bmiHeader.biPlanes = 1;
		bmi.bmiHeader.biBitCount = 32;
		bmi.bmiHeader.biCompression = BI_RGB;

		BYTE* buffer = NULL;

		auto bitmap = CreateDIBSection(NULL, &bmi, DIB_RGB_COLORS, (void**)&buffer, NULL, 0);

		if (bitmap == NULL)
			return E_OUTOFMEMORY;

		auto size = width * height * 4;

		memset(buffer, 0, size);

		auto srcPtr = reinterpret_cast<BYTE*>(data);
		auto dstPtr = buffer;

		auto srcStride = stride;
		auto dstStride = width * 4;

		for (UINT i = 0; i < height; i++)
		{
			auto srcRowPtr = srcPtr + i * srcStride;
			auto dstRowPtr = dstPtr + i * dstStride;

			for (UINT j = 0; j < width; j++)
			{
				*(dstRowPtr + 0) = *(srcRowPtr + 2);
				*(dstRowPtr + 1) = *(srcRowPtr + 1);
				*(dstRowPtr + 2) = *(srcRowPtr + 0);
				*(dstRowPtr + 3) = 0xff;

				srcRowPtr += 4;
				dstRowPtr += 4;
			}
		}

		*result = bitmap;

		return S_OK;
	}


	HRESULT SaveImage(HBITMAP* outBitmap)
	{
		HRESULT hr;

		m_D3DDeviceContext->CopyResource(m_BufferTexture.Get(), m_RenderTargetTexture.Get());

		D3D11_TEXTURE2D_DESC desc;
		m_BufferTexture->GetDesc(&desc);

		D3D11_MAPPED_SUBRESOURCE res{};
		UINT resourceId = D3D11CalcSubresource(0, 0, 0);

		hr = m_D3DDeviceContext->Map(m_BufferTexture.Get(), resourceId, D3D11_MAP_READ, 0, &res);
		spdlog::trace("Call m_D3DDeviceContext->Map at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		if (FAILED(hr))
			return hr;

		hr = CreateDIB(desc.Width, desc.Height, res.pData, res.RowPitch, outBitmap);
		spdlog::trace("Call CreateDIB at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		m_D3DDeviceContext->Unmap(m_BufferTexture.Get(), resourceId);

		return hr;
	}


	HRESULT LoadModel(const std::wstring& filePath)
	{
		HRESULT hr;

		m_d3dStudioModelRenderer = std::make_unique<D3DStudioModelRenderer>();
		m_d3dStudioModel = std::make_unique<D3DStudioModel>();

		hr = m_d3dStudioModelRenderer->Init(m_D3DDevice.Get(), m_D3DDeviceContext.Get());
		spdlog::trace("Call m_d3dStudioModelRenderer->Init at {} line {} result {}", __FUNCTION__, __LINE__, hr);

		if (FAILED(hr))
			return hr;

		m_d3dStudioModel->Load(m_D3DDevice.Get(), filePath);

		return S_OK;
	}


public:

	HRESULT RenderToBitmap(const std::wstring& filePath, UINT width, UINT height, HBITMAP* outBitmap)
	{
		HRESULT hr;

		__try
		{
			if (filePath.empty())
				return E_UNEXPECTED;

			if (width == 0 || height == 0)
				return E_UNEXPECTED;

			m_ViewWidth = width;
			m_ViewHeight = height;

			hr = InitD3D();
			spdlog::trace("Call InitD3D at {} line {} result {}", __FUNCTION__, __LINE__, hr);

			if (FAILED(hr))
				throw std::runtime_error("Failed to initialize D3D.");

			hr = LoadModel(filePath);
			spdlog::trace("Call LoadModel at {} line {} result {}", __FUNCTION__, __LINE__, hr);

			if (FAILED(hr))
				throw std::runtime_error("Failed to load model.");

			RenderFrame();

			SaveImage(outBitmap);

			CleanD3D();

			return S_OK;
		}
		__except (EXCEPTION_EXECUTE_HANDLER)
		{
			CleanD3D();
			return E_UNEXPECTED;
		}

		return E_UNEXPECTED;
	}
};


HRESULT RenderToBitmap(const std::wstring& filePath, UINT width, UINT height, HBITMAP* outBitmap)
{
	auto renderer = std::make_unique<Renderer>();

	return renderer->RenderToBitmap(filePath, width, height, outBitmap);
}
