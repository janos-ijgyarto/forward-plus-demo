#ifndef FORWARDPLUSDEMO_RENDER_RENDERSYSTEM_HPP
#define FORWARDPLUSDEMO_RENDER_RENDERSYSTEM_HPP
#include <ForwardPlusDemo/Render/Math.hpp>
#include <memory>
namespace ForwardPlusDemo
{
	struct CameraInfo
	{
		XMVector position;
		XMVector front;
		Vector2 rotation;
		XMMatrix view;
	};

	struct CameraTransformUpdate
	{
		XMVector position;
		Vector2 rotation;
	};

	class Application;
	class Fence;
	class GraphicsAPI;

	class RenderSystem
	{
	public:
		enum class FenceState : uint64_t
		{
			WAIT_RENDERER,
			WAIT_MAIN,
			DONE
		};

		~RenderSystem();

		GraphicsAPI& get_graphics_api();

		void dispatch_events();

		void update_camera_transform(const CameraTransformUpdate& transform_update);
		void set_paused(bool paused);
		void resize_window(uint32_t width, uint32_t height);
		void toggle_light_debug_rendering();

		Fence* create_fence();

		CameraInfo get_camera_info() const;
		Vector2 get_z_near_far() const;
		XMMatrix get_camera_projection() const;
	private:
		RenderSystem(Application& application);

		bool initialize();
		void shutdown();

		struct Internal;
		std::unique_ptr<Internal> m_internal;

		friend Application;
	};
}
#endif