#ifndef FORWARDPLUSDEMO_GRAPHICSAPI_COMMON_HPP
#define FORWARDPLUSDEMO_GRAPHICSAPI_COMMON_HPP
#include <d3d11.h>

#include <wrl/client.h>
using Microsoft::WRL::ComPtr;

namespace ForwardPlusDemo
{
	using D3DDevice = ID3D11Device;
	using D3DDeviceContext = ID3D11DeviceContext;

	using D3DBlob = ComPtr<ID3DBlob>;
	using D3DVertexShader = ComPtr<ID3D11VertexShader>;
	using D3DPixelShader = ComPtr<ID3D11PixelShader>;
	using D3DComputeShader = ComPtr<ID3D11ComputeShader>;
	using D3DInputLayout = ComPtr<ID3D11InputLayout>;

	using D3DBuffer = ComPtr<ID3D11Buffer>;
	using D3DShaderResourceView = ComPtr<ID3D11ShaderResourceView>;
	using D3DUnorderedAccessView = ComPtr<ID3D11UnorderedAccessView>;

	struct Shader
	{
		D3DVertexShader vertex_shader;
		D3DInputLayout input_layout;
		D3DPixelShader pixel_shader;
	};
}
#endif