 #include <ForwardPlusDemo/GraphicsAPI/GraphicsAPI.hpp>

#include <ForwardPlusDemo/Application/Application.hpp>

#ifndef NDEBUG
#include <dxgidebug.h>
#endif

#include <array>
#include <vector>

namespace
{
	struct RenderWindowState
	{
		UINT width = 0;
		UINT height = 0;
		
		bool update_size(UINT new_width, UINT new_height)
		{
			if ((width == new_width) && (height == new_height))
			{
				return false;
			}

			width = new_width;
			height = new_height;

			return true;
		}
	};
}

namespace ForwardPlusDemo
{
	struct GraphicsAPI::Internal
	{
		Application& m_application;

		RenderWindowState m_window_state;

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
			// Depth stencil state
			{
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
				HRESULT hr = m_device->CreateDepthStencilState(&depth_stencil_desc, m_depth_stencil_state.ReleaseAndGetAddressOf());
				if (FAILED(hr))
				{
					return false;
				}
			}

			// Initialize swap chain
			{
				HWND window_handle = m_application.get_window_handle();

				DXGI_SWAP_CHAIN_DESC swap_chain_desc;
				ZeroMemory(&swap_chain_desc, sizeof(DXGI_SWAP_CHAIN_DESC));

				// Have DXGI query the required dimensions from the window
				swap_chain_desc.BufferDesc.Width = 0;
				swap_chain_desc.BufferDesc.Height = 0;

				// FIXME: this format was picked arbitrarily
				// Might want to validate w.r.t hardware just to be safe?
				swap_chain_desc.BufferDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM; // This works fine, could also be SRGB, and no need to dig into HDR formats

				swap_chain_desc.BufferDesc.RefreshRate.Numerator = 0; // Only becomes relevant in exclusive fullscreen, and we are starting in windowed
				swap_chain_desc.BufferDesc.RefreshRate.Denominator = 1;

				swap_chain_desc.BufferDesc.ScanlineOrdering = DXGI_MODE_SCANLINE_ORDER_UNSPECIFIED; // Don't even think about these
				swap_chain_desc.BufferDesc.Scaling = DXGI_MODE_SCALING_UNSPECIFIED;

				swap_chain_desc.SampleDesc.Count = 1; // Relevant for MSAA, but cannot be used with flip mode anyway
				swap_chain_desc.SampleDesc.Quality = 0;

				swap_chain_desc.BufferUsage = DXGI_USAGE_RENDER_TARGET_OUTPUT; // Always use this

				swap_chain_desc.BufferCount = 2; // 2 is enough, 3 might be better (need to combine with SetMaximumFrameLatency)

				swap_chain_desc.OutputWindow = window_handle; // Use the HWND
				swap_chain_desc.Windowed = TRUE; // Always start in windowed, programmatically change to fullscreen afterward if needed (TODO: check the HWND to make sure it's in the correct state?)

				swap_chain_desc.SwapEffect = DXGI_SWAP_EFFECT_FLIP_DISCARD; // Always use this, FLIP_SEQUENTIAL could be relevant for certain apps, but for games we want DISCARD
	
				swap_chain_desc.Flags = DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING; // We want to manage mode switching, and we should enable tearing (only becomes relevant in fullscreen)

				HRESULT result = m_dxgi_factory->CreateSwapChain(m_device.Get(), &swap_chain_desc, m_swap_chain.ReleaseAndGetAddressOf());
				if (FAILED(result))
				{
					return false;
				}

				// Prevent automatic handling of Alt+Enter
				if (FAILED(m_dxgi_factory->MakeWindowAssociation(window_handle, DXGI_MWA_NO_WINDOW_CHANGES)))
				{
					return false;
				}

				// Now we get the actual dimensions
				m_swap_chain->GetDesc(&swap_chain_desc);

				m_window_state.update_size(swap_chain_desc.BufferDesc.Width, swap_chain_desc.BufferDesc.Height);

				if (create_back_buffer_view() == false)
				{
					return false;
				}

				update_viewport();
			}

			if (create_depth_buffer() == false)
			{
				return false;
			}

			return true;
		}

		bool resize_window(UINT width, UINT height)
		{
			if (m_window_state.update_size(width, height) == false)
			{
				// Nothing to do
				return true;
			}

			// Release prior references to back buffer
			m_back_buffer_texture.Reset();
			m_back_buffer_view.Reset();
			
			// Clear state and flush just to be safe
			m_device_context->ClearState();
			m_device_context->Flush();

			// Resize back buffer, preserve format and use existing flags
			if (FAILED(m_swap_chain->ResizeBuffers(0, m_window_state.width, m_window_state.height, DXGI_FORMAT_UNKNOWN, DXGI_SWAP_CHAIN_FLAG_ALLOW_MODE_SWITCH | DXGI_SWAP_CHAIN_FLAG_ALLOW_TEARING)))
			{
				return false;
			}

			// Now we create a view for the back buffer
			if (create_back_buffer_view() == false)
			{
				return false;
			}

			// FIXME: decouple this from swap chain, we should be drawing to a separate RT and then mapping to the back buffer at the end
			if (create_depth_buffer() == false)
			{
				return false;
			}

			// Update viewport
			update_viewport();

			return true;
		}

		bool set_fullscreen_state(bool /*fullscreen*/)
		{
			// NOTE: disabling this because it gets way too complicated with things like laptops with multiple GPUs,
			// and at the end of the day, everyone is switching to borderless fullscreen, which is much simpler.
			// Leaving the code for reference
#if 0
			if (m_window_state.update_fullscreen_state(fullscreen) == false)
			{
				// Nothing to do
				return true;
			}

			if (m_window_state.fullscreen == true)
			{
				// Going from windowed to fullscreen
				// First find out which screen we will be going to
				ComPtr<IDXGIOutput> dxgi_output;
				HRESULT hr = m_swap_chain->GetContainingOutput(dxgi_output.ReleaseAndGetAddressOf());
				if (FAILED(hr))
				{
					// Didn't work, try to just pick some output
					ComPtr<IDXGIOutput> enumerated_output;
					for (UINT current_output_index = 0; DXGI_ERROR_NOT_FOUND != m_dxgi_adapter->EnumOutputs(current_output_index, enumerated_output.ReleaseAndGetAddressOf()); ++current_output_index)
					{
						if (enumerated_output)
						{
							dxgi_output = enumerated_output;
						}
					}

					if (!dxgi_output)
					{
						return false;
					}
				}

				// FIXME: make sure to store desired DXGI format so it's shared between here and when the SC is created!
				UINT num_modes = 0;
				if (SUCCEEDED(dxgi_output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &num_modes, nullptr)))
				{
					std::vector<DXGI_MODE_DESC> mode_list(num_modes);
					if (FAILED(dxgi_output->GetDisplayModeList(DXGI_FORMAT_B8G8R8A8_UNORM, 0, &num_modes, mode_list.data())))
					{
						return false;
					}

					if (mode_list.empty())
					{
						return false;
					}

					// Just pick the one with the highest resolution
					// FIXME: more sensible logic for picking display mode
					DXGI_MODE_DESC best_mode = mode_list.front();
					for (const DXGI_MODE_DESC& current_mode : mode_list)
					{
						if ((current_mode.Width >= best_mode.Width) && (current_mode.Height >= best_mode.Height))
						{
							best_mode = current_mode;
						}
					}

					// NOTE: ResizeTarget SHOULD resize w.r.t client area, so the drawable area will be the specified dimensions
					if (FAILED(m_swap_chain->ResizeTarget(&best_mode)))
					{
						return false;
					}

					if (FAILED(m_swap_chain->SetFullscreenState(TRUE, dxgi_output.Get())))
					{
						return false;
					}
				}
				else
				{
					return false;
				}
			}
			else
			{
				// Going from fullscreen to windowed
				// Just revert from fullscreen
				if (FAILED(m_swap_chain->SetFullscreenState(FALSE, nullptr)))
				{
					return false;
				}
				// TODO: resize window?
				// NOTE: ResizeTarget SHOULD resize w.r.t client area, so the drawable area will be the specified dimensions
			}
#endif
			return true;
		}

		bool create_back_buffer_view()
		{
			// Get the back buffer
			HRESULT result = m_swap_chain->GetBuffer(0, __uuidof(ID3D11Texture2D), (LPVOID*)m_back_buffer_texture.ReleaseAndGetAddressOf());
			if (FAILED(result))
			{
				return false;
			}

			// Create the render target view with the back buffer pointer
			result = m_device->CreateRenderTargetView(m_back_buffer_texture.Get(), NULL, m_back_buffer_view.ReleaseAndGetAddressOf());
			if (FAILED(result))
			{
				return false;
			}

			return true;
		}

		bool create_depth_buffer()
		{
			D3D11_TEXTURE2D_DESC depth_buffer_desc;
			ZeroMemory(&depth_buffer_desc, sizeof(D3D11_TEXTURE2D_DESC));

			depth_buffer_desc.Width = m_window_state.width;
			depth_buffer_desc.Height = m_window_state.height;
			depth_buffer_desc.MipLevels = 1;
			depth_buffer_desc.ArraySize = 1;
			depth_buffer_desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
			depth_buffer_desc.SampleDesc.Count = 1;
			depth_buffer_desc.SampleDesc.Quality = 0;
			depth_buffer_desc.Usage = D3D11_USAGE_DEFAULT;
			depth_buffer_desc.BindFlags = D3D11_BIND_DEPTH_STENCIL;
			depth_buffer_desc.CPUAccessFlags = 0;
			depth_buffer_desc.MiscFlags = 0;

			if (FAILED(m_device->CreateTexture2D(&depth_buffer_desc, NULL, m_depth_stencil_buffer.ReleaseAndGetAddressOf())))
			{
				return false;
			}

			if (FAILED(m_device->CreateDepthStencilView(m_depth_stencil_buffer.Get(), NULL, m_depth_stencil_view.ReleaseAndGetAddressOf())))
			{
				return false;
			}

			return true;
		}

		void update_viewport()
		{
			D3D11_VIEWPORT viewport;
			ZeroMemory(&viewport, sizeof(D3D11_VIEWPORT));

			viewport.TopLeftX = 0;
			viewport.TopLeftY = 0;
			viewport.Width = static_cast<float>(m_window_state.width);
			viewport.Height = static_cast<float>(m_window_state.height);
			viewport.MinDepth = 0.0f;
			viewport.MaxDepth = 1.0f;

			m_device_context->RSSetViewports(1, &viewport);
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

		void end_frame()
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

	void GraphicsAPI::get_window_resolution(UINT& width, UINT& height)
	{
		width = m_internal->m_window_state.width;
		height = m_internal->m_window_state.height;
	}

	GraphicsAPI::GraphicsAPI(Application& application)
		: m_internal(std::make_unique<Internal>(application))
	{
	}

	bool GraphicsAPI::initialize()
	{
		return m_internal->initialize();
	}

	void GraphicsAPI::begin_frame()
	{
		m_internal->begin_frame();
	}

	void GraphicsAPI::end_frame()
	{
		m_internal->end_frame();
	}

	bool GraphicsAPI::set_fullscreen_state(bool fullscreen)
	{
		return m_internal->set_fullscreen_state(fullscreen);
	}

	bool GraphicsAPI::resize_window(UINT width, UINT height)
	{
		return m_internal->resize_window(width, height);
	}
}