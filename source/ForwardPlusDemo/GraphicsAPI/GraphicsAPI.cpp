 #include <ForwardPlusDemo/GraphicsAPI/GraphicsAPI.hpp>

#include <ForwardPlusDemo/Application/Application.hpp>

#ifndef NDEBUG
#include <dxgidebug.h>
#endif

#include <array>

namespace ForwardPlusDemo
{
	struct GraphicsAPI::Internal
	{
		Application& m_application;

		ComPtr<IDXGISwapChain> m_swap_chain;
		ComPtr<ID3D11RenderTargetView> m_back_buffer_view;
		ComPtr<ID3D11Texture2D> m_back_buffer_texture;

		ComPtr<ID3D11DepthStencilView> m_depth_stencil_view;
		ComPtr<ID3D11Texture2D> m_depth_stencil_buffer;
		ComPtr<ID3D11DepthStencilState> m_depth_stencil_state;

		ComPtr<IDXGIFactory6> m_dxgi_factory;
		ComPtr<IDXGIAdapter4> m_dxgi_adapter;

		ComPtr<ID3D11Device> m_device;
		ComPtr<ID3D11DeviceContext> m_device_context;
#ifndef NDEBUG
		ComPtr<ID3D11Debug> m_d3d_debug;
		ComPtr<ID3D11InfoQueue> m_info_queue;
		ComPtr<IDXGIInfoQueue> m_dxgi_info_queue;
#endif

		Internal(Application& application)
			: m_application(application)
		{

		}

		~Internal()
		{
			// Perform explicit cleanup so we can use the debug output
			m_device_context->ClearState();
			m_device_context->Flush();

			m_device_context.Reset();
#ifndef NDEBUG
			if (m_info_queue)
			{
				m_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, false);
			}
			if (m_d3d_debug)
			{
				m_d3d_debug->ReportLiveDeviceObjects(D3D11_RLDO_DETAIL | D3D11_RLDO_SUMMARY);
				m_d3d_debug.Reset();
			}
			if (m_info_queue)
			{
				D3D11_MESSAGE_CATEGORY hide_categories[] =
				{
					D3D11_MESSAGE_CATEGORY_STATE_CREATION,
				};

				D3D11_INFO_QUEUE_FILTER filter = {};
				filter.DenyList.NumCategories = _countof(hide_categories);
				filter.DenyList.pCategoryList = hide_categories;

				m_info_queue->AddStorageFilterEntries(&filter);
				m_info_queue.Reset();
			}
#endif
			m_device.Reset();

			m_dxgi_adapter.Reset();
			m_dxgi_factory.Reset();
		}

		bool initialize()
		{
			// NOTE: feature level is 10.1, although this is not relevant, as 10.0 should also work
			// The main limitation is in the compute shader features (particularly the lack of atomics)
			constexpr D3D_FEATURE_LEVEL c_feature_level = D3D_FEATURE_LEVEL::D3D_FEATURE_LEVEL_10_1;
			constexpr UINT num_feature_levels = 1;

			// Create the DXGI factory
			{
				UINT dxgi_flags = 0;
				HRESULT result;
#ifndef NDEBUG
				dxgi_flags |= DXGI_CREATE_FACTORY_DEBUG;

				result = DXGIGetDebugInterface1(0, IID_PPV_ARGS(m_dxgi_info_queue.ReleaseAndGetAddressOf()));
				if (SUCCEEDED(result))
				{
					m_dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_ERROR, true);
					m_dxgi_info_queue->SetBreakOnSeverity(DXGI_DEBUG_ALL, DXGI_INFO_QUEUE_MESSAGE_SEVERITY_CORRUPTION, true);
				}
#endif
				result = CreateDXGIFactory2(dxgi_flags, IID_PPV_ARGS(&m_dxgi_factory));
				if (FAILED(result))
				{
					return false;
				}
			}

			// Enumerate adapters to decide which one we want to use
			UINT device_flags = 0u;
#ifndef NDEBUG
			device_flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif

			for (UINT current_adapter_index = 0; DXGI_ERROR_NOT_FOUND != m_dxgi_factory->EnumAdapterByGpuPreference(current_adapter_index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&m_dxgi_adapter)); ++current_adapter_index)
			{
				DXGI_ADAPTER_DESC1 dxgi_adapter_desc;
				m_dxgi_adapter->GetDesc1(&dxgi_adapter_desc);

				if (dxgi_adapter_desc.Flags & DXGI_ADAPTER_FLAG_SOFTWARE)
				{
					// TODO: allow software drivers (for whatever reason)?
					continue;
				}

				// FIXME: add utility functions for Windows to convert from wide strings
				std::array<char, 260> adapter_desc;
				char default_char = ' ';
				WideCharToMultiByte(CP_ACP, 0, dxgi_adapter_desc.Description, -1, adapter_desc.data(), static_cast<int>(adapter_desc.size()), &default_char, NULL);

				HRESULT result = D3D11CreateDevice(m_dxgi_adapter.Get(), D3D_DRIVER_TYPE_UNKNOWN, NULL, device_flags, &c_feature_level, num_feature_levels, D3D11_SDK_VERSION, &m_device, nullptr, &m_device_context);
				if (SUCCEEDED(result))
				{
					break;
				}
			}

			if (!m_device)
			{
				return false;
			}

#ifndef NDEBUG
			if (SUCCEEDED(m_device.As(&m_d3d_debug)))
			{
				if (SUCCEEDED(m_device.As(&m_info_queue)))
				{
					// TODO: make these settings adjustable?
					m_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_WARNING, false);
					m_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_CORRUPTION, true);
					m_info_queue->SetBreakOnSeverity(D3D11_MESSAGE_SEVERITY_ERROR, true);
				}
			}
#endif
			return init_window();
		}

		bool init_window()
		{
			HWND window_handle = m_application.get_window_handle();

			RECT window_rect;
			if (!GetWindowRect(window_handle, &window_rect))
			{
				return false;
			}

			DXGI_SWAP_CHAIN_DESC swap_chain_desc;
			ZeroMemory(&swap_chain_desc, sizeof(DXGI_SWAP_CHAIN_DESC));

			swap_chain_desc.BufferDesc.Width = window_rect.right - window_rect.left;
			swap_chain_desc.BufferDesc.Height = window_rect.bottom - window_rect.top;

			swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT;
			swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;

			swap_chain_desc.OutputWindow = window_handle;

			// TODO:
#if 0
			if (m_vsync_enabled)
			{
				swap_chain_desc.BufferDesc.RefreshRate.Numerator = numerator;
				swap_chain_desc.BufferDesc.RefreshRate.Denominator = denominator;
			}
			else
			{
				swap_chain_desc.BufferDesc.RefreshRate.Numerator = 0;
				swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
			}
#else
			// No vsync
			swap_chain_desc.BufferDesc.RefreshRate.Numerator = 0;
			swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;
#endif

			// TODO: configure MSAA
			swap_chain_desc.SampleDesc.Count = 1;
			swap_chain_desc.SampleDesc.Quality = 0;

			// TODO: windowed/fullscreen
#if 0
			if (fullscreen)
			{
				swap_chain_desc.Windowed = false;
			}
			else
			{
				swap_chain_desc.Windowed = true;
			}
#endif
			// TODO
			swap_chain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED;
			swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

			// TODO
			swap_chain_desc.BufferCount = 2;
			swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD;

			// TODO!!!
			swap_chain_desc.Windowed = true;
			//swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH; // Allow alt-enter switching
			swap_chain_desc.Flags = 0;

			HRESULT result = m_dxgi_factory->CreateSwapChain(m_device.Get(), &swap_chain_desc, m_swap_chain.ReleaseAndGetAddressOf());
			if (FAILED(result))
			{
				return false;
			}

			// Create a render target for the back buffer
			result = m_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)m_back_buffer_texture.ReleaseAndGetAddressOf());
			if (FAILED(result))
			{
				return false;
			}

			// Create the render target view with the back buffer pointer
			result = m_device->CreateRenderTargetView(m_back_buffer_texture.Get(), NULL, m_back_buffer_view.ReleaseAndGetAddressOf());
			if (FAILED(result))
			{
				return true;
			}

			// Create depth-stencil
			{
				D3D11_TEXTURE2D_DESC depth_buffer_desc;
				ZeroMemory(&depth_buffer_desc, sizeof(D3D11_TEXTURE2D_DESC));

				depth_buffer_desc.Width = swap_chain_desc.BufferDesc.Width;
				depth_buffer_desc.Height = swap_chain_desc.BufferDesc.Height;
				depth_buffer_desc.MipLevels = 1;
				depth_buffer_desc.ArraySize = 1;
				depth_buffer_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
				depth_buffer_desc.SampleDesc.Count = 1;
				depth_buffer_desc.SampleDesc.Quality = 0;
				depth_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
				depth_buffer_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
				depth_buffer_desc.CPUAccessFlags = 0;
				depth_buffer_desc.MiscFlags = 0;

				m_device->CreateTexture2D(&depth_buffer_desc, NULL, m_depth_stencil_buffer.ReleaseAndGetAddressOf());
				m_device->CreateDepthStencilView(m_depth_stencil_buffer.Get(), NULL, m_depth_stencil_view.ReleaseAndGetAddressOf());

				// Depth stencil state
				D3D11_DEPTH_STENCIL_DESC depth_stencil_desc;
				ZeroMemory(&depth_stencil_desc, sizeof(D3D11_DEPTH_STENCIL_DESC));

				// Depth test parameters
				depth_stencil_desc.DepthEnable = true;
				depth_stencil_desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
				depth_stencil_desc.DepthFunc = D3D11_COMPARISON_LESS;

				// Stencil test parameters
				depth_stencil_desc.StencilEnable = false;

				// Stencil operations if pixel is front-facing
				depth_stencil_desc.FrontFace.StencilFailOp = D3D11_STENCIL_OP_KEEP;
				depth_stencil_desc.FrontFace.StencilDepthFailOp = D3D11_STENCIL_OP_INCR;
				depth_stencil_desc.FrontFace.StencilPassOp = D3D11_STENCIL_OP_KEEP;
				depth_stencil_desc.FrontFace.StencilFunc = D3D11_COMPARISON_ALWAYS;

				// Stencil operations if pixel is back-facing
				depth_stencil_desc.BackFace = depth_stencil_desc.FrontFace;

				// Create depth stencil state
				m_device->CreateDepthStencilState(&depth_stencil_desc, m_depth_stencil_state.ReleaseAndGetAddressOf());
			}

			// Set the viewport (won't ever change)
			D3D11_VIEWPORT viewport;
			ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));

			viewport.TopLeftX = 0;
			viewport.TopLeftY = 0;
			viewport.Width = static_cast<float>(window_rect.right - window_rect.left);
			viewport.Height = static_cast<float>(window_rect.bottom - window_rect.top);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;

			m_device_context->RSSetViewports(1, &viewport);

			return true;
		}

		void cleanup_window()
		{
			m_back_buffer_view.Reset();
			m_back_buffer_texture.Reset();
			m_swap_chain.Reset();
		}

		void begin_frame()
		{
			// Clear back buffer
			constexpr FLOAT c_clear_color[] = { 0.0f, 0.0f, 1.0f, 1.0f };
			m_device_context->ClearRenderTargetView(m_back_buffer_view.Get(), &c_clear_color[0]);
			m_device_context->ClearDepthStencilView(m_depth_stencil_view.Get(), D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL, 1.0f, 0);

			// Set back buffer as our render target, as well as the depth stencil
			// FIXME: is this necessary every frame?
			m_device_context->OMSetRenderTargets(1, m_back_buffer_view.GetAddressOf(), m_depth_stencil_view.Get());

			// Set DS state
			m_device_context->OMSetDepthStencilState(m_depth_stencil_state.Get(), 1);
		}

		void update()
		{
			// Assume everything has been drawn, present to the swap chain
			m_swap_chain->Present(0, 0);
		}
	};

	GraphicsAPI::~GraphicsAPI() = default;

	D3DDevice* GraphicsAPI::get_device() const
	{
		return m_internal->m_device.Get();
	}

	D3DDeviceContext* GraphicsAPI::get_device_context() const
	{
		return m_internal->m_device_context.Get();
	}

	void GraphicsAPI::begin_frame()
	{
		m_internal->begin_frame();
	}

	GraphicsAPI::GraphicsAPI(Application& application)
		: m_internal(std::make_unique<Internal>(application))
	{
	}

	bool GraphicsAPI::initialize()
	{
		return m_internal->initialize();
	}

	void GraphicsAPI::update()
	{
		m_internal->update();
	}
}