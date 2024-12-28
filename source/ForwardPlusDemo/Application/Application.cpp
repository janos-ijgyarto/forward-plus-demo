#include <ForwardPlusDemo/Application/Application.hpp>

#include <ForwardPlusDemo/GraphicsAPI/GraphicsAPI.hpp>
#include <ForwardPlusDemo/Render/RenderSystem.hpp>

#include <array>
#include <chrono>

namespace
{
	struct MainWindowState
	{
		HWND window_handle = nullptr;

		bool minimized = false;
		bool maximized = false;
		bool in_size_move = false;

		WINDOWPLACEMENT last_placement;
	};

	struct MainCameraState
	{
		enum class InputAction
		{
			MOVE_FORWARD,
			MOVE_BACK,
			MOVE_LEFT,
			MOVE_RIGHT,
			MOVE_UP,
			MOVE_DOWN,
			ROTATE_PITCH_CW,
			ROTATE_PITCH_CCW,
			ROTATE_YAW_CW,
			ROTATE_YAW_CCW,
			ACTION_COUNT
		};

		static constexpr float c_move_speed = 0.005f;
		static constexpr float c_turn_speed = 0.001f;

		ForwardPlusDemo::XMVector position = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f);
		ForwardPlusDemo::Vector2 rotation = { 0, 0 }; // Pitch & yaw

		ForwardPlusDemo::Vector3 velocity = { 0, 0, 0 };
		ForwardPlusDemo::Vector2 angular_velocity = { 0, 0 };

		std::array<bool, static_cast<size_t>(InputAction::ACTION_COUNT)> input_action_values;

		MainCameraState()
		{
			input_action_values.fill(false);
		}

		void update(float dt)
		{
			update_inputs();

			// Rotation
			float& pitch = rotation.x;

			pitch += dt * angular_velocity.x * MainCameraState::c_turn_speed;
			if (pitch > DirectX::XM_PIDIV2)
			{
				pitch = DirectX::XM_PIDIV2;
			}
			else if (pitch < -DirectX::XM_PIDIV2)
			{
				pitch = -DirectX::XM_PIDIV2;
			}

			float& yaw = rotation.y;
			yaw += dt * angular_velocity.y * MainCameraState::c_turn_speed;
			yaw = ForwardPlusDemo::clamp_angle(yaw);

			const ForwardPlusDemo::XMMatrix rpy_matrix = DirectX::XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f);
			const ForwardPlusDemo::XMVector camera_right = XMVector3TransformCoord(ForwardPlusDemo::c_camera_default_right, rpy_matrix);

			const ForwardPlusDemo::XMMatrix yaw_matrix = DirectX::XMMatrixRotationY(yaw);
			const ForwardPlusDemo::XMVector move_forward = XMVector3TransformCoord(ForwardPlusDemo::c_camera_default_forward, yaw_matrix);

			position += (velocity.x * dt * c_move_speed) * camera_right;
			position += (velocity.y * dt * c_move_speed) * ForwardPlusDemo::c_camera_default_up;
			position += (velocity.z * dt * c_move_speed) * move_forward;

			// Reset velocities
			velocity = { 0, 0, 0 };
			angular_velocity = { 0, 0 };
		}

		void update_inputs()
		{
			// Camera translation
			if (get_action_value(InputAction::MOVE_FORWARD))
			{
				velocity.z = 1.0f;
			}
			else if (get_action_value(InputAction::MOVE_BACK))
			{
				velocity.z = -1.0f;
			}

			if (get_action_value(InputAction::MOVE_LEFT))
			{
				velocity.x = -1.0f;
			}
			else if (get_action_value(InputAction::MOVE_RIGHT))
			{
				velocity.x = 1.0f;
			}

			if (get_action_value(InputAction::MOVE_UP))
			{
				velocity.y = 1.0f;
			}
			else if (get_action_value(InputAction::MOVE_DOWN))
			{
				velocity.y = -1.0f;
			}

			// Camera rotation
			// FIXME: enable mouse look!

			if (get_action_value(InputAction::ROTATE_PITCH_CW))
			{
				// Tilt up
				angular_velocity.x = 1.0f;
			}
			else if (get_action_value(InputAction::ROTATE_PITCH_CCW))
			{
				// Tilt down
				angular_velocity.x = -1.0f;
			}

			if (get_action_value(InputAction::ROTATE_YAW_CW))
			{
				// Rotate left
				angular_velocity.y = -1.0f;
			}
			else if (get_action_value(InputAction::ROTATE_YAW_CCW))
			{
				// Rotate right
				angular_velocity.y = 1.0f;
			}
		}

		void set_action_value(InputAction action, bool value)
		{
			input_action_values[static_cast<size_t>(action)] = value;
		}

		bool get_action_value(InputAction action) const
		{
			return input_action_values[static_cast<size_t>(action)];
		}
	};
}

namespace ForwardPlusDemo
{
	struct Application::Internal
	{
		RenderSystem m_render_system;

		MainWindowState m_window;
		MainCameraState m_camera;
		bool m_paused = false;

		Internal(Application& application)
			: m_render_system(application)
		{

		}

		int run(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
		{
			if (!initialize(hInstance, hPrevInstance, lpCmdLine, nCmdShow))
			{
				return 1;
			}

			main_loop();
			shutdown();
			return 0;
		}

		bool initialize(HINSTANCE hInstance, HINSTANCE /*hPrevInstance*/, LPSTR /*lpCmdLine*/, int nCmdShow)
		{
			std::srand(static_cast<unsigned int>(std::time(0)));

			if (!init_window(hInstance, nCmdShow))
			{
				return false;
			}

			if (!m_render_system.initialize())
			{
				return false;
			}

			return true;
		}

		void shutdown()
		{
			m_render_system.shutdown();
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

			m_window.window_handle = CreateWindowEx(NULL,
				c_window_class_name,
				"Forward+ Demo (D3D 11)",
				WS_OVERLAPPEDWINDOW,
				100,
				100,
				1024,
				768,
				NULL,
				NULL,
				hInstance,    // application handle
				(LPVOID)this);    // used with multiple windows, NULL

			if (!m_window.window_handle)
			{
				return false;
			}

			ShowWindow(m_window.window_handle, nCmdShow);

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

				// Done processing events, check if we can dispatch them
				// Limit to 16 ms (i.e ~60 FPS)
				if (frame_delay_time.count() >= 16)
				{
					if (m_paused == false)
					{
						constexpr float c_delta_time = 1.0f / 60.0f;
						{
							m_camera.update(c_delta_time);
							CameraTransformUpdate camera_update;
							camera_update.position = m_camera.position;
							camera_update.rotation = m_camera.rotation;
							m_render_system.update_camera_transform(camera_update);
						}
					}
					m_render_system.dispatch_events();
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
			case WM_SIZE:
				handle_resize(wParam, lParam);
				break;
			case WM_ENTERSIZEMOVE:
				set_paused(true);
				m_window.in_size_move = true;
				break;
			case WM_EXITSIZEMOVE:
				set_paused(false);
				check_window_size_change();
				//DXUTCheckForWindowChangingMonitors();
				m_window.in_size_move = false;
				break;
			case WM_KEYDOWN:
				handle_key(static_cast<int>(wParam), true);
				break;
			case WM_KEYUP:
				handle_key(static_cast<int>(wParam), false);
				break;
			case WM_SYSKEYUP:
				if (wParam == VK_RETURN)
				{
					toggle_fullscreen();
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

		void handle_resize(WPARAM wParam, LPARAM /*lParam*/)
		{
			// Taken from DXUT within https://github.com/walbourn/directx-sdk-samples
			if (SIZE_MINIMIZED == wParam)
			{
				set_paused(true);

				m_window.minimized = true;
				m_window.maximized = false;
			}
			else
			{
				RECT rcCurrentClient;
				GetClientRect(m_window.window_handle, &rcCurrentClient);
				if (rcCurrentClient.top == 0 && rcCurrentClient.bottom == 0)
				{
					// Rapidly clicking the task bar to minimize and restore a window
					// can cause a WM_SIZE message with SIZE_RESTORED when 
					// the window has actually become minimized due to rapid change
					// so just ignore this message
				}
				else if (SIZE_MAXIMIZED == wParam)
				{
					if (m_window.minimized)
					{
						set_paused(false); // Unpause since we're no longer minimized
					}
					m_window.minimized = false;
					m_window.maximized = true;
					check_window_size_change();
					//DXUTCheckForWindowChangingMonitors();
				}
				else if (SIZE_RESTORED == wParam)
				{
					if (m_window.maximized == true)
					{
						m_window.maximized = false;
						check_window_size_change();
						//DXUTCheckForWindowChangingMonitors();
					}
					else if (m_window.minimized == true)
					{
						set_paused(false); // Unpause since we're no longer minimized
						m_window.minimized = false;
						check_window_size_change();
						//DXUTCheckForWindowChangingMonitors();
					}
					else if (m_window.in_size_move == true)
					{
						// If we're neither maximized nor minimized, the window size 
						// is changing by the user dragging the window edges.  In this 
						// case, we don't reset the device yet -- we wait until the 
						// user stops dragging, and a WM_EXITSIZEMOVE message comes.
					}
					else
					{
						// This WM_SIZE come from resizing the window via an API like SetWindowPos() so 
						// resize and reset the device now.
						check_window_size_change();
						//DXUTCheckForWindowChangingMonitors();
					}
				}
			}
		}

		void toggle_fullscreen()
		{
			const DWORD window_style = GetWindowLong(m_window.window_handle, GWL_STYLE);
			if (window_style & WS_OVERLAPPEDWINDOW)
			{
				if (!GetWindowPlacement(m_window.window_handle, &m_window.last_placement))
				{
					return;
				}

				HMONITOR monitor = MonitorFromWindow(m_window.window_handle, MONITOR_DEFAULTTONEAREST);
				if (!monitor)
				{
					return;
				}
				MONITORINFO target;
				target.cbSize = sizeof(MONITORINFO);
				GetMonitorInfo(monitor, &target);

				SetWindowLongPtr(m_window.window_handle, GWL_STYLE, window_style & ~WS_OVERLAPPEDWINDOW);
				SetWindowPos(m_window.window_handle, HWND_TOP, target.rcMonitor.left, target.rcMonitor.top, target.rcMonitor.right - target.rcMonitor.left, target.rcMonitor.bottom - target.rcMonitor.top, SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			}
			else
			{
				SetWindowLong(m_window.window_handle, GWL_STYLE, window_style | WS_OVERLAPPEDWINDOW);
				SetWindowPlacement(m_window.window_handle, &m_window.last_placement);
				SetWindowPos(m_window.window_handle, NULL, 0, 0, 0, 0,
					SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER |
					SWP_NOOWNERZORDER | SWP_FRAMECHANGED);
			}
		}

		void check_window_size_change()
		{
			RECT rcCurrentClient;
			GetClientRect(m_window.window_handle, &rcCurrentClient);
			m_render_system.resize_window(rcCurrentClient.right - rcCurrentClient.left, rcCurrentClient.bottom - rcCurrentClient.top);
		}

		void set_paused(bool paused)
		{
			m_paused = paused;
			m_render_system.set_paused(paused);
		}

		void handle_key(int key_code, bool pressed)
		{
			switch (key_code)
			{
			case 'W':
				m_camera.set_action_value(MainCameraState::InputAction::MOVE_FORWARD, pressed);
				break;
			case 'A':
				m_camera.set_action_value(MainCameraState::InputAction::MOVE_LEFT, pressed);
				break;
			case 'S':
				m_camera.set_action_value(MainCameraState::InputAction::MOVE_BACK, pressed);
				break;
			case 'D':
				m_camera.set_action_value(MainCameraState::InputAction::MOVE_RIGHT, pressed);
				break;
			case VK_SPACE:
				m_camera.set_action_value(MainCameraState::InputAction::MOVE_UP, pressed);
				break;
			case VK_CONTROL:
				m_camera.set_action_value(MainCameraState::InputAction::MOVE_DOWN, pressed);
				break;
			case VK_UP:
				m_camera.set_action_value(MainCameraState::InputAction::ROTATE_PITCH_CW, pressed);
				break;
			case VK_DOWN:
				m_camera.set_action_value(MainCameraState::InputAction::ROTATE_PITCH_CCW, pressed);
				break;
			case VK_LEFT:
				m_camera.set_action_value(MainCameraState::InputAction::ROTATE_YAW_CW, pressed);
				break;
			case VK_RIGHT:
				m_camera.set_action_value(MainCameraState::InputAction::ROTATE_YAW_CCW, pressed);
				break;
			case 'V':
				if (pressed == false)
				{
					// Toggle the debug rendering in the light system
					m_render_system.toggle_light_debug_rendering();
				}
				break;
			}
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

	RenderSystem& Application::get_render_system()
	{
		return m_internal->m_render_system;
	}

	HWND Application::get_window_handle()
	{
		return m_internal->m_window.window_handle;
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