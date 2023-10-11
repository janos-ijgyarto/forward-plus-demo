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

	class Application;
	class RenderSystem
	{
	public:
		~RenderSystem();

		CameraInfo get_camera_info() const;
		Vector2 get_z_near_far() const;
		XMMatrix get_camera_projection() const;
	private:
		RenderSystem(Application& application);

		bool initialize();
		void update(float dt);
		void control_input(int key_code);

		struct Internal;
		std::unique_ptr<Internal> m_internal;

		friend Application;
	};
}
#endif