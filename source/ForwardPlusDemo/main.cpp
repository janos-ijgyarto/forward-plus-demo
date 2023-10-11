#include <ForwardPlusDemo/Application/Application.hpp>

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	ForwardPlusDemo::Application application;
	return application.run(hInstance, hPrevInstance, lpCmdLine, nCmdShow);
}