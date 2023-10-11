#include <ForwardPlusDemo/Application/Application.hpp>

#include <ForwardPlusDemo/GraphicsAPI/GraphicsAPI.hpp>
#include <ForwardPlusDemo/Render/RenderSystem.hpp>

#include <chrono>

namespace ForwardPlusDemo
{
	struct Application::Internal
	{
		GraphicsAPI m_graphics_api;
		RenderSystem m_render_system;

		HWND m_window_handle;

		Internal(Application& application)
			: m_graphics_api(application)
			, m_render_system(application)
			, m_window_handle(nullptr)
		{

		}

		int run(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
		{
			if (!initialize(hInstance, hPrevInstance, lpCmdLine, nCmdShow))
			{
				return 1;
			}

			main_loop();
			return 0;
		}

		bool initialize(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow)
		{
			std::srand(static_cast<unsigned int>(std::time(0)));

			if (!init_window(hInstance, nCmdShow))
			{
				return false;
			}

			if (!m_graphics_api.initialize())
			{
				return false;
			}

			if (!m_render_system.initialize())
			{
				return false;
			}

			return true;
		}

		bool init_window(HINSTANCE hInstance, int nCmdShow)
		{
			WNDCLASSEX window_class;
			ZeroMemory(&window_class, sizeof(WNDCLASSEX));

			constexpr const char* c_window_class_name = "WindowClass1";

			window_class.cbSize = sizeof(WNDCLASSEX);
			window_class.style = CS_HREDRAW | CS_VREDRAW;
			window_class.lpfnWndProc = &Application::window_procedure;
			window_class.hInstance = hInstance;
			window_class.hCursor = LoadCursor(NULL, IDC_ARROW);
			window_class.hbrBackground = (HBRUSH)COLOR_WINDOW;
			window_class.lpszClassName = c_window_class_name;

			RegisterClassEx(&window_class);

			m_window_handle = CreateWindowEx(NULL,
				c_window_class_name,
				"Forward+ Demo (D3D 11)",
				WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
				100,
				100,
				1024,
				768,
				NULL,
				NULL,
				hInstance,    // application handle
				(LPVOID)this);    // used with multiple windows, NULL

			if (!m_window_handle)
			{
				return false;
			}

			ShowWindow(m_window_handle, nCmdShow);

			return true;
		}

		void main_loop()
		{
			MSG window_message = { 0 };

			std::chrono::milliseconds frame_delay_time(0);
			std::chrono::steady_clock::time_point last_frame = std::chrono::steady_clock::now();
			while (true)
			{
				if (PeekMessage(&window_message, NULL, 0, 0, PM_REMOVE))
				{
					TranslateMessage(&window_message);
					DispatchMessage(&window_message);

					// check to see if it's time to quit
					if (window_message.message == WM_QUIT)
					{
						break;
					}
				}

				// Done processing window messages, check if we can update the systems
				// Limit to 16 ms (i.e ~60 FPS)
				if (frame_delay_time.count() >= 16)
				{
					constexpr float c_delta_time = 1.0f / 60.0f;

					m_render_system.update(c_delta_time);
					m_graphics_api.update();

					frame_delay_time = std::chrono::milliseconds(0);
				}

				// End of frame, update timer
				const std::chrono::steady_clock::time_point current_frame_end = std::chrono::steady_clock::now();
				frame_delay_time = std::chrono::duration_cast<std::chrono::milliseconds>(current_frame_end - last_frame);
			}
		}

		LRESULT window_procedure(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
		{
			switch (message)
			{
			case WM_KEYUP:
			{
				m_render_system.control_input(static_cast<int>(wParam));
			}
			break;
			case WM_DESTROY:
			{
				PostQuitMessage(0);
				return 0;
			}
			break;
			}

			return DefWindowProc(hWnd, message, wParam, lParam);
		}
	};

	Application::Application()
		: m_internal(std::make_unique<Internal>(*this))
	{

	}

	Application::~Application() = default;

	int Application::run(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
	{
		return m_internal->run(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
	}

	GraphicsAPI& Application::get_graphics_api()
	{
		return m_internal->m_graphics_api;
	}

	RenderSystem& Application::get_render_system()
	{
		return m_internal->m_render_system;
	}

	HWND Application::get_window_handle()
	{
		return m_internal->m_window_handle;
	}

	LRESULT CALLBACK Application::window_procedure(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
	{
		if (message == WM_NCCREATE)
		{
			CREATESTRUCT* create_struct = (CREATESTRUCT*)lParam;
			if (SetWindowLongPtr(hWnd, GWLP_USERDATA, (LONG_PTR)(create_struct->lpCreateParams)) == 0)
			{
				if (GetLastError() != 0)
				{
					return FALSE;
				}
			}
		}

		Application::Internal* internal_ptr = (Application::Internal*)GetWindowLongPtr(hWnd, GWLP_USERDATA);
		if (internal_ptr)
		{
			return internal_ptr->window_procedure(hWnd, message, wParam, lParam);
		}

		return DefWindowProc(hWnd, message, wParam, lParam);
	}
}