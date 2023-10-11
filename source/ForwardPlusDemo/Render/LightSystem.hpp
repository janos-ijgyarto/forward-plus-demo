#ifndef FORWARDPLUSDEMO_RENDER_LIGHTSYSTEM_HPP
#define FORWARDPLUSDEMO_RENDER_LIGHTSYSTEM_HPP
#include <memory>
namespace ForwardPlusDemo
{
	enum class LightType
	{
		POINT,
		DIRECTIONAL,
		SPOT,
		TYPE_COUNT
	};

	class Application;
	class LightSystem
	{
	public:
		~LightSystem();
	private:
		LightSystem(Application& application);

		bool initialize();
		void update();

		void toggle_debug_rendering();

		struct Internal;
		std::unique_ptr<Internal> m_internal;

		friend class RenderSystem;
	};
}
#endif