#include <ForwardPlusDemo/Application/Application.hpp>

#include <ForwardPlusDemo/GraphicsAPI/GraphicsAPI.hpp>
#include <ForwardPlusDemo/Render/RenderSystem.hpp>

#include <chrono>

namespace
{
	struct MainCameraState
	{
		static constexpr float c_move_speed = 0.005f;
		static constexpr float c_turn_speed = 0.001f;

		ForwardPlusDemo::XMVector position = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f);
		ForwardPlusDemo::Vector2 rotation = { 0, 0 }; // Pitch & yaw

		ForwardPlusDemo::Vector3 velocity = { 0, 0, 0 };
		ForwardPlusDemo::Vector2 angular_velocity = { 0, 0 };

		void update(float dt)
		{
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
	};
}

namespace ForwardPlusDemo
{
	struct Application::Internal
	{
		RenderSystem m_render_system;

		HWND m_window_handle;

		MainCameraState m_camera;

		Internal(Application& application)
			: m_render_system(application)
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

				handle_input();

				// Done processing events, check if we can dispatch them
				// Limit to 16 ms (i.e ~60 FPS)
				if (frame_delay_time.count() >= 16)
				{
					constexpr float c_delta_time = 1.0f / 60.0f;
					{
						m_camera.update(c_delta_time);
						CameraTransformUpdate camera_update;
						camera_update.position = m_camera.position;
						camera_update.rotation = m_camera.rotation;
						m_render_system.update_camera_transform(camera_update);
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
			case WM_KEYUP:
			{
				control_input(static_cast<int>(wParam));
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

		void handle_input()
		{
			handle_keyboard();
			handle_mouse();
		}

		void handle_keyboard()
		{
			constexpr int c_keydown_flag = 0x8000;

			// Camera translation

			if (GetKeyState('W') & c_keydown_flag)
			{
				// Move forward
				m_camera.velocity.z = 1.0f;
			}
			else if (GetKeyState('S') & c_keydown_flag)
			{
				// Move backward
				m_camera.velocity.z = -1.0f;
			}

			if (GetKeyState('A') & c_keydown_flag)
			{
				// Move left
				m_camera.velocity.x = -1.0f;
			}
			else if (GetKeyState('D') & c_keydown_flag)
			{
				// Move right
				m_camera.velocity.x = 1.0f;
			}

			if (GetKeyState(VK_SPACE) & c_keydown_flag)
			{
				// Move up
				m_camera.velocity.y = 1.0f;
			}
			else if (GetKeyState(VK_LCONTROL) & c_keydown_flag)
			{
				// Move down
				m_camera.velocity.y = -1.0f;
			}

			// Camera rotation
			// FIXME: enable mouse look!

			if (GetKeyState(VK_UP) & c_keydown_flag)
			{
				// Tilt up
				m_camera.angular_velocity.x = 1.0f;
			}
			else if (GetKeyState(VK_DOWN) & c_keydown_flag)
			{
				// Tilt down
				m_camera.angular_velocity.x = -1.0f;
			}

			if (GetKeyState(VK_LEFT) & c_keydown_flag)
			{
				// Rotate left
				m_camera.angular_velocity.y = -1.0f;
			}
			else if (GetKeyState(VK_RIGHT) & c_keydown_flag)
			{
				// Rotate right
				m_camera.angular_velocity.y = 1.0f;
			}
		}

		void handle_mouse()
		{
			// TODO!!!
		}

		// FIXME: unify with other input handling!
		void control_input(int key_code)
		{
			switch (key_code)
			{
			case 'V':
				// Toggle the debug rendering in the light system
				m_render_system.toggle_light_debug_rendering();
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