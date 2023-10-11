#ifndef FORWARDPLUSDEMO_APPLICATION_APPLICATION_HPP
#define FORWARDPLUSDEMO_APPLICATION_APPLICATION_HPP

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include <memory>
namespace ForwardPlusDemo
{
	class GraphicsAPI;
	class RenderSystem;

	class Application
	{
	public:
		Application();
		~Application();

		int run(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow);

		GraphicsAPI& get_graphics_api();
		RenderSystem& get_render_system();
		HWND get_window_handle();
	private:
		static LRESULT CALLBACK window_procedure(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

		struct Internal;
		std::unique_ptr<Internal> m_internal;
	};
}
#endif