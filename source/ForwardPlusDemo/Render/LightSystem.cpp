#include <ForwardPlusDemo/Render/LightSystem.hpp>

#include <ForwardPlusDemo/Application/Application.hpp>
#include <ForwardPlusDemo/Render/RenderSystem.hpp>
#include <ForwardPlusDemo/GraphicsAPI/GraphicsAPI.hpp>

#include <ForwardPlusDemo/Render/Math.hpp>

#include <DirectXCollision.h>

#include <d3dcompiler.h>

#include <vector>
#include <array>
#include <cmath>
#include <string>
#include <algorithm>

namespace ForwardPlusDemo
{
	// FIXME: refactor this, move things to utility headers so this file is less cluttered!
	namespace
	{
		constexpr const char* c_light_debug_shader = R"(cbuffer Camera
{
    struct
    {
        float4x4 view_projection;
    } Camera;
};

struct VS_INPUT
{
    float4 position : POSITION;
    float4 color : COLOR0;
};

struct PS_INPUT
{
    float4 position : SV_POSITION;
    float4 color : COLOR0;
};
		
PS_INPUT vertex_shader(VS_INPUT input)
{
    PS_INPUT output;
    output.position = mul(input.position, Camera.view_projection);
    output.color = input.color;
    return output;
}

float4 pixel_shader(PS_INPUT input) : SV_Target
{
    return input.color;
})";

		constexpr uint32_t c_tile_x_dim = 32;
		constexpr uint32_t c_tile_y_dim = 24;

		constexpr uint32_t c_empty_z_bin = 0xFFFF;
		constexpr uint32_t c_z_bin_min_mask = ((1 << 16) - 1);
		constexpr uint32_t c_z_bin_count = 1024;
		constexpr uint32_t c_z_binning_group_size = 128;

		constexpr uint32_t c_max_light_count = 10000;
		constexpr uint32_t c_spot_light_culling_data_stride = 6;
		constexpr uint32_t c_spot_light_max_triangle_count = 8;
		constexpr uint32_t c_tiles_per_group = 4;
		constexpr uint32_t c_light_batch_size = 32;
		constexpr uint32_t c_max_cs_thread_count = 128;

		enum class ForwardPlusShaderMacro
		{
			TILE_X_DIM,
			TILE_Y_DIM,
			Z_BIN_COUNT,
			MAX_CS_THREAD_COUNT,
			Z_BINNING_GROUP_SIZE,
			LIGHTS_PER_GROUP,
			TILES_PER_GROUP,
			MACRO_COUNT
		};

		constexpr const char* get_forward_plus_macro_name(ForwardPlusShaderMacro macro)
		{
			constexpr const char* c_forward_plus_macro_names[] = {
				"TILE_X_DIM",
				"TILE_Y_DIM",
				"Z_BIN_COUNT",
				"MAX_CS_THREAD_COUNT",
				"Z_BINNING_GROUP_SIZE",
				"LIGHTS_PER_GROUP",
				"TILES_PER_GROUP"
			};

			return c_forward_plus_macro_names[static_cast<size_t>(macro)];
		}

		constexpr const char* get_forward_plus_macro_value(ForwardPlusShaderMacro macro)
		{
			constexpr const char* c_forward_plus_macro_values[] = {
				"32",
				"24",
				"1024",
				"128",
				"128",
				"32",
				"4"
			};

			return c_forward_plus_macro_values[static_cast<size_t>(macro)];
		}

		enum class ForwardPlusComputeShader
		{
			Z_BINNING,
			SPOT_LIGHT_TRANSFORM,
			TILE_SETUP,
			TILE_CULLING,
			SHADER_COUNT
		};

		constexpr const wchar_t* get_forward_plus_shader_file_name(ForwardPlusComputeShader shader)
		{
			// FIXME: revise paths, implement file lookup system so we don't hardcode source tree paths?
			constexpr const wchar_t* c_forward_plus_shader_file_name[] = {
				L"source/ForwardPlusDemo/Render/Shaders/ForwardPlus/ZBinning.hlsl",
				L"source/ForwardPlusDemo/Render/Shaders/ForwardPlus/SpotTransform.hlsl",
				L"source/ForwardPlusDemo/Render/Shaders/ForwardPlus/TileSetup.hlsl",
				L"source/ForwardPlusDemo/Render/Shaders/ForwardPlus/TileCulling.hlsl"
			};

			return c_forward_plus_shader_file_name[static_cast<size_t>(shader)];
		}

		enum class ForwardPlusConstantBuffer
		{
			PARAMETERS,
			CS_CONSTANTS,
			Z_BINNING_CONSTANTS,
			BUFFER_COUNT
		};

		enum class ForwardPlusShaderResource
		{
			LIGHT_INFO,
			Z_BINS,
			SPOT_LIGHT_MODELS,
			SPOT_LIGHT_CULLING_DATA,
			TILE_CULLING_DATA,
			TILE_BIT_MASKS,
			LIGHT_DATA,
			RESOURCE_COUNT
		};

		constexpr uint32_t integer_division_ceil(uint32_t numerator, uint32_t denominator)
		{
			return (numerator + (denominator - 1)) / denominator;
		}

		float random_float(float min, float max)
		{
			return min + static_cast<float>(std::rand()) / (static_cast<float>(RAND_MAX / (max - min)));
		}

		constexpr uint32_t c_max_light_batch_count = integer_division_ceil(c_max_light_count, c_light_batch_size);

		std::vector<ForwardPlusShaderMacro> get_default_shader_macros()
		{
			return { ForwardPlusShaderMacro::TILE_X_DIM, ForwardPlusShaderMacro::TILE_Y_DIM, ForwardPlusShaderMacro::Z_BIN_COUNT };
		}

		std::vector<D3D_SHADER_MACRO> prepare_d3d_shader_macros(const std::vector<ForwardPlusShaderMacro>& macro_type_list)
		{
			std::vector<D3D_SHADER_MACRO> shader_macros;
#ifndef NDEBUG
			D3D_SHADER_MACRO debug_macro;
			debug_macro.Name = "DEBUG";
			debug_macro.Definition = "1";

			shader_macros.push_back(debug_macro);
#endif
			for (ForwardPlusShaderMacro current_macro : macro_type_list)
			{
				D3D_SHADER_MACRO current_d3d_macro;
				current_d3d_macro.Name = get_forward_plus_macro_name(current_macro);
				current_d3d_macro.Definition = get_forward_plus_macro_value(current_macro);

				shader_macros.push_back(current_d3d_macro);
			}

			// Add an end element
			shader_macros.push_back({ nullptr, nullptr });

			return shader_macros;
		}

		// FIXME: turn into a class so we can 
		struct LightData
		{
			using SpotLightVertexArray = std::array<XMVector, 5>;

			LightType type = LightType::POINT;

			XMMatrix transform;

			float range = 0.0f;
			float outer_angle = 0.0f;

			Vector3 diffuse = { 0, 0, 0 };
			Vector3 ambient = { 0, 0, 0 };

			float inner_angle = 0.0f;
			float linear_attenuation = 0.0f;

			DirectX::BoundingSphere bounding_sphere;

			const XMVector& get_position() const
			{
				return transform.r[3];
			}

			void update_bounds()
			{
				switch (type)
				{
				case LightType::POINT:
					bounding_sphere = DirectX::BoundingSphere(to_vector3(get_position()), range);
					break;
				case LightType::SPOT:
				{
					// Get the pyramid points
					const SpotLightVertexArray spot_vertices = generate_spot_light_vertices();
					std::array<Vector3, 5> spot_points_vec3;
					auto current_point_it = spot_points_vec3.begin();
					for (const XMVector& current_vertex : spot_vertices)
					{
						*current_point_it = to_vector3(current_vertex);
						++current_point_it;
					}

					DirectX::BoundingSphere::CreateFromPoints(bounding_sphere, spot_points_vec3.size(), spot_points_vec3.data(), sizeof(Vector3));
				}
					break;
				}
			}

			XMMatrix build_spot_light_model_matrix() const
			{
				// Range == how "tall" the cone is
				const float max_range = range;

				// xy_range == tangent based on the outer/inner angle, when multiplied by range we get the radius of the cone
				const float xy_range = std::tanf(outer_angle);

				XMMatrix scale_mat = DirectX::XMMatrixScaling(xy_range * max_range, xy_range * max_range, max_range);
				return scale_mat * transform;
			}

			SpotLightVertexArray generate_spot_light_vertices() const
			{
				// Get the model matrix
				const XMMatrix spot_light_model = build_spot_light_model_matrix();

				// Compute the points of a pyramid that envelops the light cone
				SpotLightVertexArray vertices;

				vertices[0] = spot_light_model.r[3];

				// FIXME: this could be done more efficiently via a matrix multiplication?
				const XMVector base_center = vertices[0] - spot_light_model.r[2];
				const XMVector& x_offset = spot_light_model.r[0];
				const XMVector& y_offset = spot_light_model.r[1];

				vertices[1] = base_center + x_offset + y_offset;
				vertices[2] = base_center - x_offset + y_offset;
				vertices[3] = base_center - x_offset - y_offset;
				vertices[4] = base_center + x_offset - y_offset;

				return vertices;
			}
		};

		using LightDataVector = std::vector<LightData>;

		struct alignas(16) ShaderLightInfo
		{
			uint32_t type = static_cast<uint32_t>(LightType::POINT);
			uint32_t index = 0;
			uint32_t z_range = c_empty_z_bin;
			uint32_t _padding = 0;

			void init_from_light_data(const LightData& light_data, uint32_t i)
			{
				index = i;
				type = static_cast<uint32_t>(light_data.type);
			}
		};

		struct alignas(16) ShaderLightData
		{
			Vector3 position = { 0, 0, 0 };
			float inv_range = 0.0f;

			Vector3 direction = { 0, 0, 0 };
			float cos_outer_angle = 0.0f;

			Vector3 diffuse = { 0, 0, 0 };
			float inv_cos_inner_angle = 0.0f;

			Vector3 ambient = { 0, 0, 0 };
			float linear_attenuation = 0.0f;

			ShaderLightInfo light_info;

			void initialize(const LightData& light_data, const ShaderLightInfo& info)
			{
				position = to_vector3(light_data.get_position());

				if (light_data.type == LightType::SPOT)
				{
					direction = to_vector3(-light_data.transform.r[2]);
				}

				inv_range = 1.0f / light_data.range;
				cos_outer_angle = DirectX::XMScalarCos(light_data.outer_angle);
				diffuse = light_data.diffuse;
				inv_cos_inner_angle = 1.0f / DirectX::XMScalarCos(light_data.inner_angle);
				ambient = light_data.ambient;
				linear_attenuation = light_data.linear_attenuation;

				light_info = info;
			}
		};

		using ShaderLightDataVector = std::vector<ShaderLightData>;

		struct alignas(16) ForwardPlusParameters
		{
			ShaderLightData global_light;

			std::array<uint32_t, 4> light_counts; // In same order as light types

			float z_near = 0.0f;
			float z_far = 1.0f;

			Vector2i resolution;

			ForwardPlusParameters()
			{
				reset();
			}

			void reset()
			{
				light_counts.fill(0);
			}
		};

		struct alignas(16) ForwardPlusCSConstants
		{
			XMVector camera_pos;
			XMVector camera_front;

			XMVector clip_scale;

			XMMatrix view;
			XMMatrix view_projection;
		};

		struct alignas(16) ZBinningConstants
		{
			uint32_t invocation = 0; // This indicates which set of lights we are processing
			Vector3i _padding = { 0, 0, 0 };

			void update()
			{
				++invocation;
			}

			void reset()
			{
				invocation = 0;
			}
		};

		int clamp_int(int value, int min, int max)
		{
			if (value > max)
			{
				return max;
			}
			else if (value < min)
			{
				return min;
			}

			return value;
		}

		float clamp(float value, float min, float max)
		{
			return std::fmax(min, std::fmin(value, max));
		}

		Vector2i get_light_z_bin_range(const Vector2& z_range, float z_step)
		{
			Vector2i z_bin_range(static_cast<int>(z_range.x / z_step), static_cast<int>(z_range.y / z_step));
			z_bin_range.x = clamp_int(z_bin_range.x, 0, (c_z_bin_count - 1));
			z_bin_range.y = clamp_int(z_bin_range.y, 0, (c_z_bin_count - 1));

			return z_bin_range;
		}

		Vector2 get_point_light_z_range(const LightData& light, const ForwardPlusCSConstants& cs_constants)
		{
			const float z = DirectX::XMVectorGetX(DirectX::XMVector3Dot(light.get_position() - cs_constants.camera_pos, cs_constants.camera_front));

			return Vector2(z - light.range, z + light.range);
		}

		Vector2 get_spot_light_z_range(const LightData& spot_light, const ForwardPlusCSConstants& cs_constants)
		{
			float lo = std::numeric_limits<float>::infinity();
			float hi = -lo;

			const LightData::SpotLightVertexArray spot_vertices = spot_light.generate_spot_light_vertices();
			for (const XMVector& current_pos : spot_vertices)
			{
				float z = DirectX::XMVectorGetX(DirectX::XMVector3Dot(current_pos - cs_constants.camera_pos, cs_constants.camera_front));
				lo = std::fmin(z, lo);
				hi = std::fmax(z, hi);
			}

			return Vector2(lo, hi);
		}

		uint32_t convert_z_bin(const Vector2i& z_bin)
		{
			uint32_t z_bin_data = (static_cast<uint32_t>(z_bin.x) & c_z_bin_min_mask);
			z_bin_data |= (static_cast<uint32_t>(z_bin.y) << 16);

			return z_bin_data;
		}

		struct LightDebugVertex
		{
			Vector4 position;
			Vector4 color;
		};

		struct LightDebugRender
		{
			Application& application;

			Shader shader;
			bool enabled = false;

			D3DBuffer vertex_buffer;
			D3DBuffer camera_cbuffer;

			std::vector<LightDebugVertex> debug_vertices;
			uint32_t vbuffer_capacity = 0;

			LightDebugRender(Application& app) : application(app) {}

			bool initialize()
			{
				GraphicsAPI& graphics_api = application.get_graphics_api();
				ID3D11Device* d3d_device = graphics_api.get_device();

				// Vertex shader
				D3DBlob error_blob;
				{
					constexpr const char* c_vshader_entrypoint = "vertex_shader";

					UINT compile_flags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
#ifndef NDEBUG
					compile_flags |= D3DCOMPILE_DEBUG;
#endif				
					D3DBlob vshader_blob;
					if (FAILED(D3DCompile(c_light_debug_shader, strlen(c_light_debug_shader), nullptr, nullptr, nullptr, c_vshader_entrypoint, "vs_4_0", compile_flags, 0, vshader_blob.ReleaseAndGetAddressOf(), error_blob.ReleaseAndGetAddressOf())))
					{
						if (error_blob)
						{
							OutputDebugStringA(reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
						}
						return false;
					}

					if (d3d_device->CreateVertexShader(vshader_blob->GetBufferPointer(), vshader_blob->GetBufferSize(), nullptr, shader.vertex_shader.ReleaseAndGetAddressOf()) != S_OK)
					{
						// TODO: error
						return false;
					}

					// Input layout
					{
						// Process the element descriptions
						D3D11_INPUT_ELEMENT_DESC input_element_descs[2];

						// Position
						{
							D3D11_INPUT_ELEMENT_DESC& position_desc = input_element_descs[0];
							ZeroMemory(&position_desc, sizeof(D3D11_INPUT_ELEMENT_DESC));

							position_desc.SemanticName = "POSITION";
							position_desc.SemanticIndex = 0;
							position_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
							position_desc.InputSlot = 0;
							position_desc.AlignedByteOffset = 0;
							position_desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
						}

						// Normal
						{
							D3D11_INPUT_ELEMENT_DESC& color_desc = input_element_descs[1];
							ZeroMemory(&color_desc, sizeof(D3D11_INPUT_ELEMENT_DESC));

							color_desc.SemanticName = "COLOR";
							color_desc.SemanticIndex = 0;
							color_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
							color_desc.InputSlot = 0;
							color_desc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
							color_desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
						}

						if (d3d_device->CreateInputLayout(input_element_descs, 2, vshader_blob->GetBufferPointer(), vshader_blob->GetBufferSize(), shader.input_layout.ReleaseAndGetAddressOf()) != S_OK)
						{
							return false;
						}
					}
				}

				// Pixel shader
				{
					constexpr const char* c_pshader_entrypoint = "pixel_shader";

					UINT compile_flags = 0;
#ifndef NDEBUG
					compile_flags |= D3DCOMPILE_DEBUG;
#endif
					D3DBlob pshader_blob;
					if (FAILED(D3DCompile(c_light_debug_shader, strlen(c_light_debug_shader), nullptr, nullptr, nullptr, c_pshader_entrypoint, "ps_4_0", compile_flags, 0, pshader_blob.ReleaseAndGetAddressOf(), error_blob.ReleaseAndGetAddressOf())))
					{
						if (error_blob)
						{
							OutputDebugStringA(reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
						}
						return false;
					}

					if (d3d_device->CreatePixelShader(pshader_blob->GetBufferPointer(), pshader_blob->GetBufferSize(), nullptr, shader.pixel_shader.ReleaseAndGetAddressOf()) != S_OK)
					{
						// TODO: error
						return false;
					}
				}

				// Create camera cbuffer
				{
					D3D11_BUFFER_DESC buffer_description;
					ZeroMemory(&buffer_description, sizeof(D3D11_BUFFER_DESC));

					buffer_description.ByteWidth = sizeof(XMMatrix);
					buffer_description.Usage = D3D11_USAGE_DYNAMIC;
					buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
					buffer_description.MiscFlags = 0;
					buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

					HRESULT result = d3d_device->CreateBuffer(&buffer_description, nullptr, camera_cbuffer.ReleaseAndGetAddressOf());
					if (FAILED(result))
					{
						// TODO: error handling?
						return false;
					}
				}

				return true;
			}

			void add_visible_light(const LightData& light, const ShaderLightData& shader_data)
			{
				Vector4 light_position(shader_data.position.x, shader_data.position.y, shader_data.position.z, 1.0f);
				switch (light.type)
				{
				case LightType::POINT:
				{
					// Create a "sphere" around the light
					constexpr size_t c_circle_resolution = 36;
					constexpr float c_angle_step = DirectX::XM_2PI / c_circle_resolution;

					// Add a starting vertex, offset from light position by the range
					LightDebugVertex current_vertex;
					current_vertex.color = Vector4(light.diffuse.x, light.diffuse.y, light.diffuse.z, 1.0f);

					float current_angle = c_angle_step;
					for (size_t current_point_index = 0; current_point_index < c_circle_resolution; ++current_point_index)
					{
						if (current_point_index > 0)
						{
							// Add previous vertex a second time as the starting point for the next line segment
							const LightDebugVertex& prev_vertex = debug_vertices.back();
							debug_vertices.push_back(prev_vertex);
						}
						else
						{
							// Add first point
							current_vertex.position = light_position;
							current_vertex.position.x += light.range;
							debug_vertices.push_back(current_vertex);
						}

						// Calculate next vertex
						current_vertex.position = light_position;
						current_vertex.position.x += light.range * std::cosf(current_angle);
						current_vertex.position.z += light.range * std::sinf(current_angle);

						debug_vertices.push_back(current_vertex);

						current_angle += c_angle_step;
					}

					// Redo same as before, but now vertically
					current_angle = c_angle_step;
					for (size_t current_point_index = 0; current_point_index < c_circle_resolution; ++current_point_index)
					{
						if (current_point_index > 0)
						{
							// Add previous vertex a second time as the starting point for the next line segment
							const LightDebugVertex& prev_vertex = debug_vertices.back();
							debug_vertices.push_back(prev_vertex);
						}
						else
						{
							// Add first point
							current_vertex.position = light_position;
							current_vertex.position.x += light.range;
							debug_vertices.push_back(current_vertex);
						}

						// Calculate next vertex
						current_vertex.position = light_position;
						current_vertex.position.x += light.range * std::cosf(current_angle);
						current_vertex.position.y += light.range * std::sinf(current_angle);

						debug_vertices.push_back(current_vertex);

						current_angle += c_angle_step;
					}
				}
				break;
				case LightType::SPOT:
				{
					const LightData::SpotLightVertexArray spot_vertices = light.generate_spot_light_vertices();

					LightDebugVertex pyramid_vertices[5];
					auto spot_vertex_it = spot_vertices.begin();
					for (LightDebugVertex& current_vertex : pyramid_vertices)
					{
						current_vertex.position = to_vector4(*spot_vertex_it);
						current_vertex.color = Vector4(light.diffuse.x, light.diffuse.y, light.diffuse.z, 1.0f);

						++spot_vertex_it;
					}

					debug_vertices.push_back(pyramid_vertices[0]);
					debug_vertices.push_back(pyramid_vertices[1]);

					debug_vertices.push_back(pyramid_vertices[0]);
					debug_vertices.push_back(pyramid_vertices[2]);

					debug_vertices.push_back(pyramid_vertices[0]);
					debug_vertices.push_back(pyramid_vertices[3]);

					debug_vertices.push_back(pyramid_vertices[0]);
					debug_vertices.push_back(pyramid_vertices[4]);

					debug_vertices.push_back(pyramid_vertices[1]);
					debug_vertices.push_back(pyramid_vertices[2]);

					debug_vertices.push_back(pyramid_vertices[2]);
					debug_vertices.push_back(pyramid_vertices[3]);

					debug_vertices.push_back(pyramid_vertices[3]);
					debug_vertices.push_back(pyramid_vertices[4]);

					debug_vertices.push_back(pyramid_vertices[4]);
					debug_vertices.push_back(pyramid_vertices[1]);
				}
					break;
				}
			}

			void render()
			{
				if (!enabled)
				{
					return;
				}

				if (debug_vertices.empty())
				{
					return;
				}

				const uint32_t vertex_count = static_cast<uint32_t>(debug_vertices.size());
				buffer_data();

				GraphicsAPI& graphics_api = application.get_graphics_api();

				// Set primitive topology
				ID3D11DeviceContext* device_context = graphics_api.get_device_context();
				device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_LINELIST);

				// Set shaders and constant buffers
				{
					device_context->VSSetShader(shader.vertex_shader.Get(), nullptr, 0);
					device_context->IASetInputLayout(shader.input_layout.Get());
					device_context->VSSetConstantBuffers(0, 1, camera_cbuffer.GetAddressOf());
					device_context->PSSetShader(shader.pixel_shader.Get(), nullptr, 0);
				}

				// Set vertex buffer
				{
					constexpr UINT c_vertex_stride = sizeof(LightDebugVertex);
					constexpr UINT c_null_offset = 0;
					device_context->IASetVertexBuffers(0, 1, vertex_buffer.GetAddressOf(), &c_vertex_stride, &c_null_offset);
				}

				// Draw the debug lines
				device_context->Draw(vertex_count, 0);
			}

			void buffer_data()
			{
				GraphicsAPI& graphics_api = application.get_graphics_api();
				ID3D11DeviceContext* device_context = graphics_api.get_device_context();

				// Update the camera
				{
					const RenderSystem& render_system = application.get_render_system();
					const CameraInfo camera_info = render_system.get_camera_info();
					const XMMatrix view_projection = DirectX::XMMatrixMultiply(camera_info.view, render_system.get_camera_projection());

					D3D11_MAPPED_SUBRESOURCE mapped_subresource;
					ZeroMemory(&mapped_subresource, sizeof(D3D11_MAPPED_SUBRESOURCE));

					const HRESULT result = device_context->Map(camera_cbuffer.Get(), 0, D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
					if (SUCCEEDED(result))
					{
						memcpy(mapped_subresource.pData, &view_projection, sizeof(XMMatrix));
					}

					device_context->Unmap(camera_cbuffer.Get(), 0);
				}

				{
					const uint32_t vertex_count = static_cast<uint32_t>(debug_vertices.size());
					if (vertex_count > vbuffer_capacity)
					{
						// Need to make vertex buffer larger
						D3D11_BUFFER_DESC buffer_description;
						ZeroMemory(&buffer_description, sizeof(D3D11_BUFFER_DESC));

						buffer_description.ByteWidth = static_cast<uint32_t>(debug_vertices.size() * sizeof(LightDebugVertex));
						buffer_description.Usage = D3D11_USAGE_DYNAMIC;
						buffer_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
						buffer_description.MiscFlags = 0;
						buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

						D3D11_SUBRESOURCE_DATA subresource_data;
						ZeroMemory(&subresource_data, sizeof(D3D11_SUBRESOURCE_DATA));
						subresource_data.pSysMem = debug_vertices.data();

						HRESULT result = graphics_api.get_device()->CreateBuffer(&buffer_description, &subresource_data, vertex_buffer.ReleaseAndGetAddressOf());
						if (FAILED(result))
						{
							// TODO: error handling?
						}

						vbuffer_capacity = vertex_count;
					}
					else
					{
						// Simply map new data into vertex buffer
						D3D11_MAPPED_SUBRESOURCE mapped_subresource;
						ZeroMemory(&mapped_subresource, sizeof(D3D11_MAPPED_SUBRESOURCE));

						const HRESULT result = device_context->Map(vertex_buffer.Get(), 0, D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
						if (SUCCEEDED(result))
						{
							memcpy(mapped_subresource.pData, debug_vertices.data(), sizeof(LightDebugVertex) * debug_vertices.size());
						}

						device_context->Unmap(vertex_buffer.Get(), 0);
					}
					debug_vertices.clear();
				}
			}
		};
	}

	struct LightSystem::Internal
	{
		Application& m_application;

		ForwardPlusParameters m_forward_plus_params;
		ForwardPlusCSConstants m_cs_constants;
		ZBinningConstants m_z_binning_constants;

		LightDataVector m_active_lights;

		std::vector<Vector2> m_light_z_ranges;
		std::vector<ShaderLightInfo> m_light_info;
		std::vector<XMMatrix> m_spot_light_models;

		std::array<ShaderLightDataVector, static_cast<size_t>(LightType::TYPE_COUNT)> m_light_type_data;

		std::array<D3DComputeShader, static_cast<size_t>(ForwardPlusComputeShader::SHADER_COUNT)> m_compute_shaders;

		std::array<D3DBuffer, static_cast<size_t>(ForwardPlusConstantBuffer::BUFFER_COUNT)> m_constant_buffers;

		std::array<D3DBuffer, static_cast<size_t>(ForwardPlusShaderResource::RESOURCE_COUNT)> m_shader_resource_buffers;
		std::array<D3DShaderResourceView, static_cast<size_t>(ForwardPlusShaderResource::RESOURCE_COUNT)> m_shader_resource_views;
		std::array<D3DUnorderedAccessView, static_cast<size_t>(ForwardPlusShaderResource::RESOURCE_COUNT)> m_unordered_access_views;

		LightDebugRender m_debug_render;

		Internal(Application& application)
			: m_application(application)
			, m_debug_render(application)
		{

		}

		D3DComputeShader& get_compute_shader(ForwardPlusComputeShader shader) { return m_compute_shaders[static_cast<size_t>(shader)]; }

		D3DBuffer& get_constant_buffer(ForwardPlusConstantBuffer buffer) { return m_constant_buffers[static_cast<size_t>(buffer)]; }
		D3DBuffer& get_shader_resource_buffer(ForwardPlusShaderResource shader_resource) { return m_shader_resource_buffers[static_cast<size_t>(shader_resource)]; }
		D3DShaderResourceView& get_shader_resource_view(ForwardPlusShaderResource shader_resource) { return m_shader_resource_views[static_cast<size_t>(shader_resource)]; }
		D3DUnorderedAccessView& get_unordered_access_view(ForwardPlusShaderResource shader_resource) { return m_unordered_access_views[static_cast<size_t>(shader_resource)]; }

		uint32_t get_light_type_count(LightType type) const
		{
			const ShaderLightDataVector& light_data_vec = m_light_type_data[static_cast<size_t>(type)];
			return static_cast<uint32_t>(light_data_vec.size());
		}

		uint32_t get_total_light_count() const { return static_cast<uint32_t>(m_light_info.size()); }

		void set_compute_shader_resources(const std::vector<ForwardPlusShaderResource>& srv_resources, ForwardPlusShaderResource uav_resource)
		{
			std::vector<ID3D11ShaderResourceView*> srv_vector;
			for (ForwardPlusShaderResource current_resource : srv_resources)
			{
				const D3DShaderResourceView& current_srv = get_shader_resource_view(current_resource);
				srv_vector.push_back(current_srv.Get());
			}

			// Set the UAV
			const D3DUnorderedAccessView& current_uav = get_unordered_access_view(uav_resource);

			D3DDeviceContext* d3d_context = m_application.get_graphics_api().get_device_context();
			d3d_context->CSSetShaderResources(1, static_cast<uint32_t>(srv_vector.size()), srv_vector.data());
			d3d_context->CSSetUnorderedAccessViews(0, 1, current_uav.GetAddressOf(), nullptr);
		}

		void run_compute_shader(ForwardPlusComputeShader cs_type)
		{
			// Set the compute shader
			D3DDeviceContext* d3d_context = m_application.get_graphics_api().get_device_context();
			d3d_context->CSSetShader(get_compute_shader(cs_type).Get(), nullptr, 0);

			// Gather the resources and UAV
			std::vector<ForwardPlusShaderResource> srv_resources;

			// Unbind previously used resources
			{
				ID3D11ShaderResourceView* null_srv[2] = { nullptr };
				d3d_context->CSSetShaderResources(1, 2, null_srv);
			}

			{
				ID3D11UnorderedAccessView* null_uav[] = { nullptr };
				d3d_context->CSSetUnorderedAccessViews(0, 1, null_uav, 0);
			}

			switch (cs_type)
			{
			case ForwardPlusComputeShader::Z_BINNING:
			{
				set_compute_shader_resources(srv_resources, ForwardPlusShaderResource::Z_BINS);

				// Reset the Z bins in the UAV
				constexpr uint32_t z_bin_init[4] = { c_empty_z_bin, c_empty_z_bin, c_empty_z_bin, c_empty_z_bin };
				const D3DUnorderedAccessView& z_bins_uav = get_unordered_access_view(ForwardPlusShaderResource::Z_BINS);

				d3d_context->ClearUnorderedAccessViewUint(z_bins_uav.Get(), z_bin_init);

				// Count how many dispatches are needed to process all lights
				constexpr uint32_t group_count = integer_division_ceil(c_z_bin_count, c_z_binning_group_size);
				const uint32_t dispatch_count = integer_division_ceil(get_total_light_count(), c_z_binning_group_size);

				const D3DBuffer& z_binning_cbuffer = get_constant_buffer(ForwardPlusConstantBuffer::Z_BINNING_CONSTANTS);
				d3d_context->CSSetConstantBuffers(2, 1, z_binning_cbuffer.GetAddressOf());

				for (uint32_t current_dispatch_index = 0; current_dispatch_index < dispatch_count; ++current_dispatch_index)
				{
					// Update the cbuffer in the shader
					{
						D3D11_MAPPED_SUBRESOURCE mapped_subresource;
						ZeroMemory(&mapped_subresource, sizeof(D3D11_MAPPED_SUBRESOURCE));

						const HRESULT result = d3d_context->Map(z_binning_cbuffer.Get(), 0, D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
						if (SUCCEEDED(result))
						{
							memcpy(mapped_subresource.pData, &m_z_binning_constants, sizeof(ZBinningConstants));
						}

						d3d_context->Unmap(z_binning_cbuffer.Get(), 0);
					}

					// Dispatch the current group
					d3d_context->Dispatch(group_count, 1, 1);

					// Update the cbuffer data for the next dispatch
					m_z_binning_constants.update();
				}

				// Reset after we are done
				m_z_binning_constants.reset();
			}
			break;
			case ForwardPlusComputeShader::SPOT_LIGHT_TRANSFORM:
			{
				// Dispatch enough groups to cover all spot lights
				const uint32_t group_count = integer_division_ceil(get_light_type_count(LightType::SPOT), c_max_cs_thread_count);
				if (group_count > 0)
				{
					srv_resources.push_back(ForwardPlusShaderResource::SPOT_LIGHT_MODELS);
					set_compute_shader_resources(srv_resources, ForwardPlusShaderResource::SPOT_LIGHT_CULLING_DATA);

					d3d_context->Dispatch(group_count, 1, 1);
				}
			}
			break;
			case ForwardPlusComputeShader::TILE_SETUP:
			{
				srv_resources.push_back(ForwardPlusShaderResource::SPOT_LIGHT_CULLING_DATA);
				srv_resources.push_back(ForwardPlusShaderResource::LIGHT_DATA);
				set_compute_shader_resources(srv_resources, ForwardPlusShaderResource::TILE_CULLING_DATA);

				// Dispatch enough groups to cover all lights
				const uint32_t group_count = integer_division_ceil(get_total_light_count(), c_max_cs_thread_count);
				d3d_context->Dispatch(group_count, 1, 1);
			}
			break;
			case ForwardPlusComputeShader::TILE_CULLING:
			{
				srv_resources.push_back(ForwardPlusShaderResource::TILE_CULLING_DATA);
				set_compute_shader_resources(srv_resources, ForwardPlusShaderResource::TILE_BIT_MASKS);

				// Dispatch enough groups to cover all lights for all tiles
				const uint32_t group_x_dim = integer_division_ceil(get_total_light_count(), c_light_batch_size);
				constexpr uint32_t group_y_dim = integer_division_ceil(c_tile_x_dim * c_tile_y_dim, c_tiles_per_group);

				d3d_context->Dispatch(group_x_dim, group_y_dim, 1);
			}
			break;
			}
		}

		void set_pixel_shader_resources()
		{
			D3DDeviceContext* d3d_context = m_application.get_graphics_api().get_device_context();
			const D3DBuffer& forward_plus_params_cbuffer = get_constant_buffer(ForwardPlusConstantBuffer::PARAMETERS);
			d3d_context->PSSetConstantBuffers(0, 1, forward_plus_params_cbuffer.GetAddressOf());

			// Gather the shader resources
			std::array<ID3D11ShaderResourceView*, 3> resource_ptr_array = {};
			std::array<ForwardPlusShaderResource, 3> resource_type_array = { ForwardPlusShaderResource::Z_BINS,	ForwardPlusShaderResource::TILE_BIT_MASKS,  ForwardPlusShaderResource::LIGHT_DATA };

			size_t srv_index = 0;
			for (ForwardPlusShaderResource current_resource_type : resource_type_array)
			{
				const D3DShaderResourceView& current_srv = get_shader_resource_view(current_resource_type);
				resource_ptr_array[srv_index] = current_srv.Get();

				++srv_index;
			}

			d3d_context->PSSetShaderResources(0, static_cast<uint32_t>(resource_ptr_array.size()), resource_ptr_array.data());
		}

		bool initialize()
		{
			if (!m_debug_render.initialize())
			{
				return false;
			}

			const auto default_macros = get_default_shader_macros();

			// Create the compute shaders
			for (int current_shader_index = 0; current_shader_index < static_cast<int>(ForwardPlusComputeShader::SHADER_COUNT); ++current_shader_index)
			{
				const ForwardPlusComputeShader current_shader_type = static_cast<ForwardPlusComputeShader>(current_shader_index);
				ComPtr<ID3D11ComputeShader>& current_shader_ptr = get_compute_shader(current_shader_type);

				if (!current_shader_ptr)
				{
					auto current_shader_macros = default_macros;
					std::string debug_name = "";

					switch (current_shader_type)
					{
					case ForwardPlusComputeShader::Z_BINNING:
						debug_name = "Z Binning";
						current_shader_macros.push_back(ForwardPlusShaderMacro::MAX_CS_THREAD_COUNT);
						current_shader_macros.push_back(ForwardPlusShaderMacro::Z_BINNING_GROUP_SIZE);
						break;
					case ForwardPlusComputeShader::SPOT_LIGHT_TRANSFORM:
						debug_name = "Spot Light Transform";
						current_shader_macros.push_back(ForwardPlusShaderMacro::MAX_CS_THREAD_COUNT);
						break;
					case ForwardPlusComputeShader::TILE_SETUP:
						debug_name = "Tile Setup";
						current_shader_macros.push_back(ForwardPlusShaderMacro::MAX_CS_THREAD_COUNT);
						break;
					case ForwardPlusComputeShader::TILE_CULLING:
						debug_name = "Tile Culling";
						current_shader_macros.push_back(ForwardPlusShaderMacro::MAX_CS_THREAD_COUNT);
						current_shader_macros.push_back(ForwardPlusShaderMacro::LIGHTS_PER_GROUP);
						current_shader_macros.push_back(ForwardPlusShaderMacro::TILES_PER_GROUP);
						break;
					}

					if (!compile_compute_shader(get_forward_plus_shader_file_name(current_shader_type), "main", current_shader_macros, current_shader_ptr))
					{
						return false;
					}

					// Set debug name
					current_shader_ptr.Get()->SetPrivateData(WKPDID_D3DDebugObjectName, static_cast<UINT>(debug_name.length()), debug_name.c_str());
				}
			}

			// Create constant buffers
			for (int current_constant_buffer_index = 0; current_constant_buffer_index < static_cast<int>(ForwardPlusConstantBuffer::BUFFER_COUNT); ++current_constant_buffer_index)
			{
				if (!init_constant_buffer(static_cast<ForwardPlusConstantBuffer>(current_constant_buffer_index)))
				{
					return false;
				}
			}

			// Create shader resources
			for (int current_shader_resource_index = 0; current_shader_resource_index < static_cast<int>(ForwardPlusShaderResource::RESOURCE_COUNT); ++current_shader_resource_index)
			{
				if (!init_shader_resource(static_cast<ForwardPlusShaderResource>(current_shader_resource_index)))
				{
					return false;
				}
			}

			// Initialize some of our constants (these won't change at runtime)
			{
				HWND window_handle = m_application.get_window_handle();

				RECT window_rect;
				if (!GetWindowRect(window_handle, &window_rect))
				{
					return false;
				}

				m_forward_plus_params.resolution.x = window_rect.right - window_rect.left;
				m_forward_plus_params.resolution.y = window_rect.bottom - window_rect.top;

				RenderSystem& render_system = m_application.get_render_system();
				const Vector2 z_near_far = render_system.get_z_near_far();
				m_forward_plus_params.z_near = z_near_far.x;
				m_forward_plus_params.z_far = z_near_far.y;

				const XMMatrix xm_projection_matrix = render_system.get_camera_projection();
				const XMMatrix xm_inv_projection_matrix = DirectX::XMMatrixInverse(nullptr, xm_projection_matrix);

				const Matrix4 projection_matrix = to_matrix4(xm_projection_matrix);
				const Matrix4 inv_projection_matrix = to_matrix4(xm_inv_projection_matrix);

				m_cs_constants.clip_scale = DirectX::XMVectorSet(projection_matrix.m[0][0], -projection_matrix.m[1][1], inv_projection_matrix.m[0][0], inv_projection_matrix.m[1][1]);
			}

			generate_lights();

			return true;
		}

		void generate_lights()
		{
			constexpr size_t c_test_light_count = 10;

			for (size_t current_light_index = 0; current_light_index < c_test_light_count; ++current_light_index)
			{
				{
					LightData point_light_data;
					point_light_data.type = LightType::POINT;

					// Generate random position
					const XMVector translation = DirectX::XMVectorSet(static_cast<float>(std::rand() % 10) * 10 - 50.0f, 5.0f, static_cast<float>(current_light_index) * 10 - 50.0f, 0.0f);

					point_light_data.transform = DirectX::XMMatrixTranslationFromVector(translation);
					point_light_data.range = 25.0f;

					// Generate random color
					const float red_component = 1.0f / (1.0f + static_cast<float>(std::rand() % 10));
					const float blue_component = 1.0f / (1.0f + static_cast<float>(std::rand() % 10));
					point_light_data.diffuse = Vector3(red_component, 1.0f / (1.0f + static_cast<float>(std::rand() % 10)), std::max(1.0f - red_component, blue_component));
					point_light_data.ambient = Vector3(point_light_data.diffuse.x * 0.3f, point_light_data.diffuse.y * 0.3f, point_light_data.diffuse.z * 0.3f);

					point_light_data.update_bounds();

					m_active_lights.push_back(point_light_data);
				}
				{
					LightData spot_light_data;

					spot_light_data.type = LightType::SPOT;

					// Generate random position and rotation
					const XMVector translation = DirectX::XMVectorSet(random_float(-50.f, 50.f), 5.0f, static_cast<float>(current_light_index) * 10.f - 50.0f, 0.0f);
					const XMVector rpy_rotation = DirectX::XMVectorSet(random_float(DirectX::XMConvertToRadians(-120.f), DirectX::XMConvertToRadians(-60.f)), 0.0f, 0.0f, 0.0f);

					spot_light_data.transform = DirectX::XMMatrixRotationRollPitchYawFromVector(rpy_rotation) * DirectX::XMMatrixTranslationFromVector(translation);

					spot_light_data.outer_angle = DirectX::XMConvertToRadians(random_float(10.0f, 45.0f));
					spot_light_data.range = 20.0f;
					spot_light_data.inner_angle = DirectX::XMConvertToRadians(spot_light_data.outer_angle * 0.25f);

					// Generate random color
					const float red_component = 1.0f / (1.0f + static_cast<float>(std::rand() % 10));
					const float blue_component = 1.0f / (1.0f + static_cast<float>(std::rand() % 10));
					spot_light_data.diffuse = Vector3(red_component, 1.0f / (1.0f + static_cast<float>(std::rand() % 10)), std::max(1.0f - red_component, blue_component));
					spot_light_data.ambient = Vector3(spot_light_data.diffuse.x * 0.3f, spot_light_data.diffuse.y * 0.3f, spot_light_data.diffuse.z * 0.3f);

					spot_light_data.update_bounds();

					m_active_lights.push_back(spot_light_data);
				}
			}
		}

		bool compile_compute_shader(const wchar_t* source_file, const char* entry_point, const std::vector<ForwardPlusShaderMacro>& macros, D3DComputeShader& compute_shader)
		{
			constexpr const char* c_cs_target = "cs_4_0";

			DWORD compiler_flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
#if !defined(NDEBUG)
			compiler_flags |= D3DCOMPILE_DEBUG;
#endif			
			// Add the defines
			const std::vector<D3D_SHADER_MACRO> shader_macros = prepare_d3d_shader_macros(macros);

			D3DBlob shader_blob;
			D3DBlob error_blob;
			UINT compile_flags = 0;
#ifndef NDEBUG
			compile_flags |= D3DCOMPILE_DEBUG;
#endif			
			HRESULT result = D3DCompileFromFile(source_file, shader_macros.data(), D3D_COMPILE_STANDARD_FILE_INCLUDE, entry_point, c_cs_target, compile_flags, 0, shader_blob.ReleaseAndGetAddressOf(), error_blob.ReleaseAndGetAddressOf());
			if (FAILED(result))
			{
				if (error_blob)
				{
					OutputDebugStringA(reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
				}
				return false;
			}

			D3DDevice* d3d_device = m_application.get_graphics_api().get_device();
			if (FAILED(d3d_device->CreateComputeShader(shader_blob->GetBufferPointer(), shader_blob->GetBufferSize(), nullptr, compute_shader.ReleaseAndGetAddressOf())))
			{
				// TODO: error
				return false;
			}

			// Success!
			return true;
		}

		bool init_constant_buffer(ForwardPlusConstantBuffer constant_buffer)
		{
			D3D11_BUFFER_DESC buffer_description;
			ZeroMemory(&buffer_description, sizeof(D3D11_BUFFER_DESC));

			buffer_description.Usage = D3D11_USAGE_DYNAMIC;
			buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
			buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

			D3D11_SUBRESOURCE_DATA resource_data;
			ZeroMemory(&resource_data, sizeof(D3D11_SUBRESOURCE_DATA));

			D3DDevice* d3d_device = m_application.get_graphics_api().get_device();
			D3DBuffer& current_constant_buffer = get_constant_buffer(constant_buffer);

			switch (constant_buffer)
			{
			case ForwardPlusConstantBuffer::PARAMETERS:
			{
				buffer_description.ByteWidth = sizeof(ForwardPlusParameters);

				resource_data.pSysMem = &m_forward_plus_params;

				return SUCCEEDED(d3d_device->CreateBuffer(&buffer_description, &resource_data, current_constant_buffer.ReleaseAndGetAddressOf()));
			}
			case ForwardPlusConstantBuffer::CS_CONSTANTS:
			{
				buffer_description.ByteWidth = sizeof(ForwardPlusCSConstants);

				resource_data.pSysMem = &m_cs_constants;

				return SUCCEEDED(d3d_device->CreateBuffer(&buffer_description, &resource_data, current_constant_buffer.ReleaseAndGetAddressOf()));
			}
			case ForwardPlusConstantBuffer::Z_BINNING_CONSTANTS:
			{
				buffer_description.ByteWidth = sizeof(ZBinningConstants);

				resource_data.pSysMem = &m_z_binning_constants;

				return SUCCEEDED(d3d_device->CreateBuffer(&buffer_description, &resource_data, current_constant_buffer.ReleaseAndGetAddressOf()));
			}
			}

			return false;
		}

		bool init_shader_resource(ForwardPlusShaderResource shader_resource)
		{
			D3D11_BUFFER_DESC buffer_description;
			ZeroMemory(&buffer_description, sizeof(D3D11_BUFFER_DESC));
			buffer_description.Usage = D3D11_USAGE_DEFAULT;
			buffer_description.BindFlags = D3D11_BIND_SHADER_RESOURCE;
			buffer_description.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_STRUCTURED; // All are either structured or RW buffers

			D3D11_SUBRESOURCE_DATA resource_data;
			ZeroMemory(&resource_data, sizeof(D3D11_SUBRESOURCE_DATA));

			D3DDevice* d3d_device = m_application.get_graphics_api().get_device();
			D3DBuffer& current_resource_buffer = get_shader_resource_buffer(shader_resource);
			D3DShaderResourceView& current_resource_view = get_shader_resource_view(shader_resource);
			D3DUnorderedAccessView& current_uav = get_unordered_access_view(shader_resource);

			uint32_t buffer_element_size = 0;
			uint32_t buffer_capacity = 0;

			switch (shader_resource)
			{
			case ForwardPlusShaderResource::LIGHT_INFO:
			{
				buffer_description.Usage = D3D11_USAGE_DYNAMIC;
				buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				buffer_capacity = c_max_light_count;
				buffer_element_size = sizeof(ShaderLightInfo);
			}
			break;
			case ForwardPlusShaderResource::Z_BINS:
			{
				buffer_description.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

				buffer_capacity = c_z_bin_count;
				buffer_element_size = sizeof(uint32_t);
			}
			break;
			case ForwardPlusShaderResource::SPOT_LIGHT_MODELS:
			{
				buffer_description.Usage = D3D11_USAGE_DYNAMIC;
				buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				buffer_capacity = c_max_light_count;
				buffer_element_size = sizeof(XMMatrix);
			}
			break;
			case ForwardPlusShaderResource::SPOT_LIGHT_CULLING_DATA:
			{
				buffer_description.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

				buffer_capacity = c_max_light_count * c_spot_light_culling_data_stride;
				buffer_element_size = sizeof(Vector4);
			}
			break;
			case ForwardPlusShaderResource::TILE_CULLING_DATA:
			{
				buffer_description.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

				buffer_capacity = c_max_light_count * c_spot_light_max_triangle_count * 4;
				buffer_element_size = sizeof(Vector4);
			}
			break;
			case ForwardPlusShaderResource::TILE_BIT_MASKS:
			{
				buffer_description.BindFlags |= D3D11_BIND_UNORDERED_ACCESS;

				buffer_capacity = c_tile_x_dim * c_tile_y_dim * c_max_light_batch_count;
				buffer_element_size = sizeof(uint32_t);
			}
			break;
			case ForwardPlusShaderResource::LIGHT_DATA:
			{
				buffer_description.Usage = D3D11_USAGE_DYNAMIC;
				buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				buffer_capacity = c_max_light_count;
				buffer_element_size = sizeof(ShaderLightData);
			}
			break;
			}

			buffer_description.ByteWidth = buffer_element_size * buffer_capacity;
			buffer_description.StructureByteStride = buffer_element_size;

			if (FAILED(d3d_device->CreateBuffer(&buffer_description, nullptr, current_resource_buffer.ReleaseAndGetAddressOf())))
			{
				return false;
			}

			// Create SRV
			D3D11_SHADER_RESOURCE_VIEW_DESC srv_description;
			ZeroMemory(&srv_description, sizeof(D3D11_SHADER_RESOURCE_VIEW_DESC));

			srv_description.Format = DXGI_FORMAT_UNKNOWN;
			srv_description.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;

			srv_description.Buffer.FirstElement = 0;
			srv_description.Buffer.NumElements = buffer_capacity;

			if (FAILED(d3d_device->CreateShaderResourceView(current_resource_buffer.Get(), &srv_description, current_resource_view.ReleaseAndGetAddressOf())))
			{
				return false;
			}

			// Create UAV for RW buffers
			if (buffer_description.BindFlags & D3D11_BIND_UNORDERED_ACCESS)
			{
				// Prepare UAV as well
				D3D11_UNORDERED_ACCESS_VIEW_DESC uav_description;
				ZeroMemory(&uav_description, sizeof(D3D11_UNORDERED_ACCESS_VIEW_DESC));

				uav_description.ViewDimension = D3D11_UAV_DIMENSION_BUFFER;
				uav_description.Format = DXGI_FORMAT_UNKNOWN;
				uav_description.Buffer.FirstElement = 0;
				uav_description.Buffer.NumElements = buffer_capacity;

				if (FAILED(d3d_device->CreateUnorderedAccessView(current_resource_buffer.Get(), &uav_description, current_uav.ReleaseAndGetAddressOf())))
				{
					return false;
				}
			}

			return true;
		}

		void update()
		{
			// Update light data
			update_lights();

			m_debug_render.render();

			// Update camera
			RenderSystem& render_system = m_application.get_render_system();
			{
				const CameraInfo camera_info = render_system.get_camera_info();

				m_cs_constants.camera_pos = camera_info.position;
				m_cs_constants.camera_front = camera_info.front;

				m_cs_constants.view = DirectX::XMMatrixTranspose(camera_info.view); // Have to transpose for the compute shader

				const XMMatrix projection_matrix = render_system.get_camera_projection();
				m_cs_constants.view_projection = DirectX::XMMatrixTranspose(DirectX::XMMatrixMultiply(camera_info.view, projection_matrix));
			}

			// Gather light counts
			for (int light_type_index = static_cast<int>(LightType::POINT); light_type_index < static_cast<int>(LightType::TYPE_COUNT); ++light_type_index)
			{
				const LightType current_light_type = static_cast<LightType>(light_type_index);
				m_forward_plus_params.light_counts[light_type_index] = get_light_type_count(current_light_type);
			}

			const uint32_t total_light_count = get_total_light_count();

			// First we need to sort all the light info by the view Z coordinate
			struct LightSortInfo
			{
				uint32_t index;
				float view_z;

				bool operator<(const LightSortInfo& rhs) const { return view_z < rhs.view_z; }
			};

			std::vector<LightSortInfo> light_sort_vec(total_light_count);

			{
				auto z_range_it = m_light_z_ranges.begin();
				uint32_t current_light_index = 0;
				for (LightSortInfo& current_sort_info : light_sort_vec)
				{
					current_sort_info.index = current_light_index;
					current_sort_info.view_z = (z_range_it->x + z_range_it->y) * 0.5f; // Use midpoint of Z-range for sorting

					++current_light_index;
					++z_range_it;
				}
			}

			std::sort(light_sort_vec.begin(), light_sort_vec.end());

			// After sort, remap the info and data
			std::vector<ShaderLightInfo> sorted_light_info(total_light_count);
			ShaderLightDataVector sorted_light_data_vec(total_light_count);
			{
				const float z_distance = m_forward_plus_params.z_far - m_forward_plus_params.z_near;
				const float z_step = z_distance / c_z_bin_count;

				uint32_t current_light_index = 0;
				for (const LightSortInfo& current_sort_info : light_sort_vec)
				{
					const Vector2i light_z_bin_range = get_light_z_bin_range(m_light_z_ranges[current_sort_info.index], z_step);

					ShaderLightInfo& current_sorted_light_info = sorted_light_info[current_light_index];
					ShaderLightData& current_sorted_light_data = sorted_light_data_vec[current_light_index];

					current_sorted_light_info = m_light_info[current_sort_info.index];

					// Get the vector for the light type, remap to combined sorted data buffer using the index in the info
					const ShaderLightDataVector& light_type_data_vector = m_light_type_data[current_sorted_light_info.type];
					current_sorted_light_data = light_type_data_vector[current_sorted_light_info.index];

					current_sorted_light_info.z_range = convert_z_bin(light_z_bin_range);
					current_sorted_light_data.light_info = current_sorted_light_info;

					++current_light_index;
				}
			}

			// Update the data in the resource buffers used by the compute and pixel shaders
			{
				std::array<ForwardPlusShaderResource, 3> forward_plus_resources = { ForwardPlusShaderResource::LIGHT_INFO, ForwardPlusShaderResource::SPOT_LIGHT_MODELS, ForwardPlusShaderResource::LIGHT_DATA };

				for (ForwardPlusShaderResource current_resource_type : forward_plus_resources)
				{
					uint32_t element_size = 0;
					uint32_t element_count = 0;
					const void* data = nullptr;

					switch (current_resource_type)
					{
					case ForwardPlusShaderResource::LIGHT_INFO:
					{
						element_size = sizeof(ShaderLightInfo);
						element_count = static_cast<uint32_t>(sorted_light_info.size());
						data = sorted_light_info.data();
					}
					break;
					case ForwardPlusShaderResource::SPOT_LIGHT_MODELS:
					{
						element_size = sizeof(XMMatrix);
						element_count = static_cast<uint32_t>(m_spot_light_models.size());
						data = m_spot_light_models.data();
					}
					break;
					case ForwardPlusShaderResource::LIGHT_DATA:
					{
						element_size = sizeof(ShaderLightData);
						element_count = static_cast<uint32_t>(sorted_light_data_vec.size());
						data = sorted_light_data_vec.data();
					}
					break;
					}

					update_buffer(get_shader_resource_buffer(current_resource_type), element_size, element_count, data);
				}
			}

			D3DDeviceContext* d3d_context = m_application.get_graphics_api().get_device_context();

			// Unset resources used by pixel shader (they will need to be used by the compute shaders first)
			{
				ID3D11ShaderResourceView* null_srv[3] = { nullptr };
				d3d_context->PSSetShaderResources(0, 3, null_srv);
			}

			// Set light info resource (used in all cases)
			{
				D3DShaderResourceView& light_info_srv = get_shader_resource_view(ForwardPlusShaderResource::LIGHT_INFO);
				d3d_context->CSSetShaderResources(0, 1, light_info_srv.GetAddressOf());
			}

			// Constant buffers
			{
				// Update constant buffer data
				for (int buffer_index = static_cast<int>(ForwardPlusConstantBuffer::PARAMETERS); buffer_index < static_cast<int>(ForwardPlusConstantBuffer::BUFFER_COUNT); ++buffer_index)
				{
					const ForwardPlusConstantBuffer current_buffer_type = static_cast<ForwardPlusConstantBuffer>(buffer_index);

					switch (current_buffer_type)
					{
					case ForwardPlusConstantBuffer::PARAMETERS:
					{
						update_buffer(get_constant_buffer(current_buffer_type), sizeof(ForwardPlusParameters), 1, &m_forward_plus_params);
					}
					break;
					case ForwardPlusConstantBuffer::CS_CONSTANTS:
					{
						update_buffer(get_constant_buffer(current_buffer_type), sizeof(ForwardPlusCSConstants), 1, &m_cs_constants);
					}
					break;
					}
				}

				// Set the params and CS constants cbuffers for the shaders
				std::array<ID3D11Buffer*, 2> forward_plus_cbuffers = { get_constant_buffer(ForwardPlusConstantBuffer::PARAMETERS).Get(), get_constant_buffer(ForwardPlusConstantBuffer::CS_CONSTANTS).Get() };
				d3d_context->CSSetConstantBuffers(0, 2, forward_plus_cbuffers.data());
			}

			// Run the compute shaders
			for (int current_shader_index = 0; current_shader_index < static_cast<int>(ForwardPlusComputeShader::SHADER_COUNT); ++current_shader_index)
			{
				const ForwardPlusComputeShader current_shader_type = static_cast<ForwardPlusComputeShader>(current_shader_index);
				run_compute_shader(current_shader_type);
			}

			// Clean up after the compute shaders
			d3d_context->CSSetShader(nullptr, nullptr, 0u);

			{
				ID3D11ShaderResourceView* null_srv[3] = { nullptr };
				d3d_context->CSSetShaderResources(0, 3, null_srv);
			}

			{
				ID3D11UnorderedAccessView* null_uav[] = { nullptr };
				d3d_context->CSSetUnorderedAccessViews(0, 1, null_uav, 0);
			}

			// Set the pixel shader resources (for the actual render pass)
			set_pixel_shader_resources();
		}

		void update_buffer(const D3DBuffer& buffer, uint32_t element_size, uint32_t element_count, const void* data)
		{
			D3DDeviceContext* d3d_context = m_application.get_graphics_api().get_device_context();

			D3D11_MAPPED_SUBRESOURCE mapped_subresource;
			ZeroMemory(&mapped_subresource, sizeof(D3D11_MAPPED_SUBRESOURCE));

			const HRESULT result = d3d_context->Map(buffer.Get(), 0, D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
			if (SUCCEEDED(result))
			{
				memcpy(mapped_subresource.pData, data, element_size * element_count);
			}

			d3d_context->Unmap(buffer.Get(), 0);
		}

		void update_lights()
		{
			// Clean up previous data
			m_light_z_ranges.clear();
			m_light_info.clear();
			m_spot_light_models.clear();

			for (ShaderLightDataVector& light_data_vec : m_light_type_data)
			{
				light_data_vec.clear();
			}

			// TODO: lights with dynamic attributes (position, etc.)?

			RenderSystem& render_system = m_application.get_render_system();

			// Create camera frustum (from projection matrix)
			DirectX::BoundingFrustum bounding_frustum(render_system.get_camera_projection());

			// Transform using camera info
			const CameraInfo camera_info = render_system.get_camera_info();

			const XMVector rotation = DirectX::XMQuaternionRotationRollPitchYaw(camera_info.rotation.x, camera_info.rotation.y, 0.0f);
			bounding_frustum.Transform(bounding_frustum, 1.0f, rotation, camera_info.position);

			// Gather visible lights				
			for (const LightData& current_light : m_active_lights)
			{
				if (bounding_frustum.Intersects(current_light.bounding_sphere) == true)
				{
					// Light is visible, add to the relevant caches
					add_visible_light(current_light);
				}
			}

			// TODO: global light?
		}

		void add_visible_light(const LightData& light)
		{
			const uint32_t light_index = get_light_type_count(light.type);

			// Light info
			ShaderLightInfo light_info;
			light_info.init_from_light_data(light, light_index);
			m_light_info.push_back(light_info);

			// Shader light data
			ShaderLightDataVector& light_data_vec = m_light_type_data[static_cast<size_t>(light.type)];

			ShaderLightData shader_light_data;
			shader_light_data.initialize(light, light_info);

			light_data_vec.push_back(shader_light_data);

			if (light.type == LightType::SPOT)
			{
				m_spot_light_models.push_back(light.build_spot_light_model_matrix());
			}

			// Light Z range
			switch (light.type)
			{
			case LightType::POINT:
				m_light_z_ranges.push_back(get_point_light_z_range(light, m_cs_constants));
				break;
			case LightType::SPOT:
			{
				// Add using spot light model
				m_light_z_ranges.push_back(get_spot_light_z_range(light, m_cs_constants));
			}
			break;
			default:
				m_light_z_ranges.push_back(Vector2(0, 0));
				break;
			}

			if (m_debug_render.enabled)
			{
				m_debug_render.add_visible_light(light, shader_light_data);
			}
		}

		void toggle_debug_rendering()
		{
			m_debug_render.enabled = !m_debug_render.enabled;
		}
	};

	LightSystem::~LightSystem() = default;

	LightSystem::LightSystem(Application& application)
		: m_internal(std::make_unique<Internal>(application))
	{

	}

	bool LightSystem::initialize()
	{
		return m_internal->initialize();
	}

	void LightSystem::update()
	{
		m_internal->update();
	}

	void LightSystem::toggle_debug_rendering()
	{
		m_internal->toggle_debug_rendering();
	}
}