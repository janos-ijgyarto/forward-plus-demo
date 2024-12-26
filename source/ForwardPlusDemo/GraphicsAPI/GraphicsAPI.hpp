#ifndef FORWARDPLUSDEMO_GRAPHICSAPI_GRAPHICSAPI_HPP
#define FORWARDPLUSDEMO_GRAPHICSAPI_GRAPHICSAPI_HPP
#include <ForwardPlusDemo/GraphicsAPI/Common.hpp>
#include <dxgi1_6.h>

#include <memory>
namespace ForwardPlusDemo
{
	class Application;
	class GraphicsAPI
	{
	public:
		~GraphicsAPI();

		D3DDevice* get_device() const;
		D3DDeviceContext* get_device_context() const;
	private:
		GraphicsAPI(Application& application);

		bool initialize();

		void begin_frame();
		void end_frame(); 

		struct Internal;
		std::unique_ptr<Internal> m_internal;

		friend class RenderSystem;
	};
}
#endif