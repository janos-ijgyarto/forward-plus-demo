#include <ForwardPlusDemo/Render/RenderSystem.hpp>

#include <ForwardPlusDemo/Application/Application.hpp>
#include <ForwardPlusDemo/GraphicsAPI/GraphicsAPI.hpp>

#include <ForwardPlusDemo/Render/LightSystem.hpp>

#include <d3dcompiler.h>

#include <DirectXCollision.h>

#include <vector>
#include <array>

namespace ForwardPlusDemo
{
	namespace
	{
		constexpr float c_camera_movement_speed = 0.05f;
		constexpr float c_camera_turn_speed = 0.01f;

		// NOTE: coordinates are X,Z for horizontal coordinates, and Y for vertical (DirectXMath prefers it this way)
		constexpr std::array<Vector3, 8> c_cube_positions = {
			Vector3{ 0.5f, -0.5f, 0.5f  },
			Vector3{ -0.5f, -0.5f, 0.5f },
			Vector3{ -0.5f, -0.5f, -0.5f },
			Vector3{ 0.5f, -0.5f, -0.5f },
			Vector3{ 0.5f, 0.5f, 0.5f  },
			Vector3{ -0.5f, 0.5f, 0.5f },
			Vector3{ -0.5f, 0.5f, -0.5f },
			Vector3{ 0.5f, 0.5f, -0.5f },
		};

		constexpr std::array<Vector3, 6> c_cube_normals = {
			Vector3{ 0.0f, -1.0f, 0.0f },
			Vector3{ 1.0f, 0.0f, 0.0f },
			Vector3{ 0.0f, 0.0f, 1.0f },
			Vector3{ -1.0f, 0.0f, 0.0f },
			Vector3{ 0.0f, 0.0f, -1.0f },
			Vector3{ 0.0f, 1.0f, 0.0f }
		};

		constexpr std::array<Vector3, 5> c_pyramid_positions = {
			Vector3{ 0.5f, -0.5f, 0.5f },
			Vector3{ -0.5f, -0.5f, 0.5f },
			Vector3{ -0.5f, -0.5f, -0.5f },
			Vector3{ 0.5f, -0.5f, -0.5f },
			Vector3{ 0.0f, 0.5f, 0.0f }
		};

		constexpr std::array<Vector3, 6> c_pyramid_normals = {
			Vector3{ 0.0f, -1.0f, 0.0f },
			Vector3{ 0.5f, 0.5f, 0.0f },
			Vector3{ 0.0f, 0.5f, 0.5f },
			Vector3{ -0.5f, 0.5f, 0.0f },
			Vector3{ 0.0f, 0.5f, -0.5f },
		};

		const XMVector c_default_forward = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 0.0f);
		const XMVector c_default_right = DirectX::XMVectorSet(1.0f, 0.0f, 0.0f, 0.0f);
		const XMVector c_default_up = DirectX::XMVectorSet(0.0f, 1.0f, 0.0f, 0.0f);

		float clamp_angle(float angle)
		{
			if (angle > XM_PI)
			{
				// Wrap around
				return angle - DirectX::XM_2PI;
			}
			else if (angle < -DirectX::XM_PI)
			{
				// Wrap around
				return angle + DirectX::XM_2PI;
			}

			return angle;
		}

		enum class ObjectType
		{
			CUBE,
			PYRAMID,
			PLANE,
			TYPE_COUNT
		};

		struct ObjectInfo
		{
			uint32_t vertex_offset;
			uint32_t vertex_count;
		};

		struct alignas(16) Material
		{
			Vector4 diffuse = { 0, 0, 0, 0 };
			Vector4 ambient = { 0, 0, 0, 0 };
		};

		struct alignas(16) PerDrawData
		{
			XMMatrix model = DirectX::XMMatrixIdentity();
			XMMatrix inv_model = DirectX::XMMatrixIdentity();
			Material material;
		};

		struct ObjectInstanceInfo
		{
			ObjectType type = ObjectType::CUBE;
			DirectX::BoundingBox bounding_volume;
			PerDrawData per_draw_data;
		};

		struct CameraState
		{
			XMVector position = DirectX::XMVectorSet(0.0f, 0.0f, 1.0f, 1.0f);
			Vector2 rotation = { 0, 0 }; // Pitch & yaw

			Vector3 velocity = { 0, 0, 0 };
			Vector2 angular_velocity = { 0, 0 };

			XMMatrix view;
			XMVector forward;

			CameraInfo get_info() const { return CameraInfo{ position, forward, rotation, view }; }

			void update(float dt)
			{
				// Rotation
				float& pitch = rotation.x;

				pitch += dt * angular_velocity.x * c_camera_turn_speed;
				if (pitch > DirectX::XM_PIDIV2)
				{
					pitch = DirectX::XM_PIDIV2;
				}
				else if (pitch < -DirectX::XM_PIDIV2)
				{
					pitch = -DirectX::XM_PIDIV2;
				}

				float& yaw = rotation.y;
				yaw += dt * angular_velocity.y * c_camera_turn_speed;
				yaw = clamp_angle(yaw);
								
				const XMMatrix rpy_matrix = DirectX::XMMatrixRotationRollPitchYaw(pitch, yaw, 0.0f);
				const XMVector camera_right = XMVector3TransformCoord(c_default_right, rpy_matrix);
				forward = XMVector3TransformCoord(c_default_forward, rpy_matrix);

				const XMVector camera_up = XMVector3TransformCoord(c_default_up, rpy_matrix);

				const XMMatrix yaw_matrix = DirectX::XMMatrixRotationY(yaw);
				const XMVector move_forward = XMVector3TransformCoord(c_default_forward, yaw_matrix);

				position += (velocity.x * dt * c_camera_movement_speed) * camera_right;
				position += (velocity.y * dt * c_camera_movement_speed) * c_default_up;
				position += (velocity.z * dt * c_camera_movement_speed) * move_forward;

				// Reset velocities
				velocity = { 0, 0, 0 };
				angular_velocity = { 0, 0 };

				// Compute view matrix
				const XMVector camera_target = forward + position;
				view = DirectX::XMMatrixLookAtLH(position, camera_target, camera_up);
			}
		};

		struct alignas(16) Camera
		{
			XMVector world_position = { 0, 0, 0, 0 };
			XMMatrix view = DirectX::XMMatrixIdentity();
			XMMatrix view_projection = DirectX::XMMatrixIdentity();
		};

		struct Vertex
		{
			Vector4 position = { 0, 0, 0, 0 };
			Vector4 normal = { 0, 0, 0, 0 };
		};
	}

	struct RenderSystem::Internal 
	{
		Application& m_application;

		LightSystem m_light_system;

		Shader m_shader;

		D3DBuffer m_vertex_buffer;

		D3DBuffer m_forward_plus_cbuffer;
		D3DBuffer m_camera_buffer;
		D3DBuffer m_per_draw_cbuffer;

		std::array<ObjectInfo, static_cast<size_t>(ObjectType::TYPE_COUNT)> m_object_info;
		std::vector<ObjectInstanceInfo> m_object_instances;
		
		CameraState m_camera;
		XMMatrix m_projection_matrix;

		Internal(Application& application)
			: m_application(application)
			, m_light_system(application)
		{}

		bool initialize()
		{
			if (!generate_objects())
			{
				return false;
			}

			if (!create_shaders())
			{
				return false;
			}

			if (!m_light_system.initialize())
			{
				return false;
			}

			return true;
		}

		Vector2 get_z_near_far() const
		{
			// TODO: make this adjustable?
			constexpr float c_near_z = 0.1f;
			constexpr float c_far_z = 1000.0f;

			return Vector2(c_near_z, c_far_z);
		}

		bool create_shaders()
		{
			GraphicsAPI& graphics_api = m_application.get_graphics_api();
			ID3D11Device* d3d_device = graphics_api.get_device();

			// FIXME: revise paths, implement file lookup system so we don't hardcode source tree paths?
			constexpr auto c_shader_path = L"source/ForwardPlusDemo/Render/Shaders/Main.hlsl";

			// Vertex shader
			D3DBlob error_blob;
			{
				constexpr const char* c_vshader_entrypoint = "vertex_shader";

				UINT compile_flags = D3DCOMPILE_PACK_MATRIX_ROW_MAJOR;
#ifndef NDEBUG
				compile_flags |= D3DCOMPILE_DEBUG;
#endif				
				D3DBlob vshader_blob;
				if (FAILED(D3DCompileFromFile(c_shader_path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, c_vshader_entrypoint, "vs_4_0", compile_flags, 0, vshader_blob.ReleaseAndGetAddressOf(), error_blob.ReleaseAndGetAddressOf())))
				{
					if (error_blob)
					{
						OutputDebugStringA(reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
					}
					return false;
				}

				if (d3d_device->CreateVertexShader(vshader_blob->GetBufferPointer(), vshader_blob->GetBufferSize(), nullptr, m_shader.vertex_shader.ReleaseAndGetAddressOf()) != S_OK)
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
						D3D11_INPUT_ELEMENT_DESC& normal_desc = input_element_descs[1];
						ZeroMemory(&normal_desc, sizeof(D3D11_INPUT_ELEMENT_DESC));

						normal_desc.SemanticName = "NORMAL";
						normal_desc.SemanticIndex = 0;
						normal_desc.Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
						normal_desc.InputSlot = 0;
						normal_desc.AlignedByteOffset = D3D11_APPEND_ALIGNED_ELEMENT;
						normal_desc.InputSlotClass = D3D11_INPUT_PER_VERTEX_DATA;
					}

					if (d3d_device->CreateInputLayout(input_element_descs, 2, vshader_blob->GetBufferPointer(), vshader_blob->GetBufferSize(), m_shader.input_layout.ReleaseAndGetAddressOf()) != S_OK)
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
				if (FAILED(D3DCompileFromFile(c_shader_path, nullptr, D3D_COMPILE_STANDARD_FILE_INCLUDE, c_pshader_entrypoint, "ps_4_0", compile_flags, 0, pshader_blob.ReleaseAndGetAddressOf(), error_blob.ReleaseAndGetAddressOf())))
				{
					if (error_blob)
					{
						OutputDebugStringA(reinterpret_cast<const char*>(error_blob->GetBufferPointer()));
					}
					return false;
				}

				if (d3d_device->CreatePixelShader(pshader_blob->GetBufferPointer(), pshader_blob->GetBufferSize(), nullptr, m_shader.pixel_shader.ReleaseAndGetAddressOf()) != S_OK)
				{
					// TODO: error
					return false;
				}
			}

			return true;
		}

		bool generate_objects()
		{
			std::vector<Vertex> vertices;

			generate_cubes(vertices);
			generate_pyramids(vertices);
			generate_plane(vertices);

			return create_buffers(vertices);
		}

		void generate_cubes(std::vector<Vertex>& vertices)
		{
			init_cube_info(vertices);

			const DirectX::BoundingBox default_box(Vector3(0, 0, 0), Vector3(0.5f, 0.5f, 0.5f));

			ObjectInstanceInfo cube_info;
			cube_info.type = ObjectType::CUBE;

			cube_info.per_draw_data.model = DirectX::XMMatrixTranslation(1.0f, 0.5f, 0.0f);			
			cube_info.per_draw_data.inv_model = DirectX::XMMatrixInverse(nullptr, cube_info.per_draw_data.model);

			cube_info.per_draw_data.material.diffuse = Vector4(1.0f, 0.0f, 1.0f, 1.0f);
			cube_info.per_draw_data.material.ambient = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			// Set bounding volume
			default_box.Transform(cube_info.bounding_volume, cube_info.per_draw_data.model);

			m_object_instances.push_back(cube_info);
		}

		void init_cube_info(std::vector<Vertex>& vertices)
		{
			ObjectInfo& cube_info = m_object_info[static_cast<size_t>(ObjectType::CUBE)];
						
			cube_info.vertex_offset = static_cast<uint32_t>(vertices.size());

			auto generate_cube_face = [&vertices](const std::array<size_t, 4>& corners, const Vector3& normal)
			{
				std::array<Vertex, 4> template_vertices;

				{
					auto corners_it = corners.begin();
					for (Vertex& current_vertex : template_vertices)
					{
						const Vector3& current_corner_pos = c_cube_positions[*corners_it];
						current_vertex.position = Vector4(current_corner_pos.x, current_corner_pos.y, current_corner_pos.z, 1.0f);
						current_vertex.normal = Vector4(normal.x, normal.y, normal.z, 0.0f);

						++corners_it;
					}
				}

				vertices.push_back(template_vertices[0]);
				vertices.push_back(template_vertices[1]);
				vertices.push_back(template_vertices[2]);

				vertices.push_back(template_vertices[0]);
				vertices.push_back(template_vertices[2]);
				vertices.push_back(template_vertices[3]);
			};

			// Bottom
			std::array<size_t, 4> corner_indices = { 0, 1, 2, 3 };
			generate_cube_face(corner_indices, c_cube_normals[0]);

			// Right
			corner_indices = { 7, 4, 0, 3 };
			generate_cube_face(corner_indices, c_cube_normals[1]);

			// Back
			corner_indices = { 4, 5, 1, 0 };
			generate_cube_face(corner_indices, c_cube_normals[2]);

			// Left
			corner_indices = { 5, 6, 2, 1 };
			generate_cube_face(corner_indices, c_cube_normals[3]);

			// Front
			corner_indices = { 6, 7, 3, 2 };
			generate_cube_face(corner_indices, c_cube_normals[4]);

			// Top
			corner_indices = { 5, 4, 7, 6 };
			generate_cube_face(corner_indices, c_cube_normals[5]);

			cube_info.vertex_count = static_cast<uint32_t>(vertices.size()) - cube_info.vertex_offset;
		}

		void generate_pyramids(std::vector<Vertex>& vertices)
		{
			init_pyramid_info(vertices);

			const DirectX::BoundingBox default_box(Vector3(0, 0, 0), Vector3(0.5f, 0.5f, 0.5f));

			ObjectInstanceInfo pyramid_info;
			pyramid_info.type = ObjectType::PYRAMID;

			pyramid_info.per_draw_data.model = DirectX::XMMatrixTranslation(-1.0f, 0.5f, 0.0f);
			pyramid_info.per_draw_data.inv_model = DirectX::XMMatrixInverse(nullptr, pyramid_info.per_draw_data.model);

			pyramid_info.per_draw_data.material.diffuse = Vector4(0.0f, 1.0f, 1.0f, 1.0f);
			pyramid_info.per_draw_data.material.ambient = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			// Set bounding volume
			default_box.Transform(pyramid_info.bounding_volume, pyramid_info.per_draw_data.model);
			
			m_object_instances.push_back(pyramid_info);
		}

		void init_pyramid_info(std::vector<Vertex>& vertices)
		{
			ObjectInfo& pyramid_info = m_object_info[static_cast<size_t>(ObjectType::PYRAMID)];
			
			pyramid_info.vertex_offset = static_cast<uint32_t>(vertices.size());

			auto generate_pyramid_face = [&vertices](const std::array<size_t, 3>& corners, const Vector3& normal)
			{
				std::array<Vertex, 3> template_vertices;

				{
					auto corners_it = corners.begin();
					for (Vertex& current_vertex : template_vertices)
					{
						const Vector3& current_pos = c_pyramid_positions[*corners_it];
						current_vertex.position = Vector4(current_pos.x, current_pos.y, current_pos.z, 1.0f);
						current_vertex.normal = Vector4(normal.x, normal.y, normal.z, 0.0f);
						
						++corners_it;
					}
				}

				vertices.push_back(template_vertices[0]);
				vertices.push_back(template_vertices[1]);
				vertices.push_back(template_vertices[2]);
			};

			// Create base
			{
				std::array<Vertex, 4> template_vertices;

				{
					const Vector3& pyramid_base_normal = c_pyramid_normals[0];
					size_t pyramid_vertex_index = 0;
					for (Vertex& current_vertex : template_vertices)
					{
						const Vector3& current_pos = c_pyramid_positions[pyramid_vertex_index];
						current_vertex.position = Vector4(current_pos.x, current_pos.y, current_pos.z, 1.0f);
						current_vertex.normal = Vector4(pyramid_base_normal.x, pyramid_base_normal.y, pyramid_base_normal.z, 0.0f);
						++pyramid_vertex_index;
					}
				}

				vertices.push_back(template_vertices[0]);
				vertices.push_back(template_vertices[1]);
				vertices.push_back(template_vertices[2]);

				vertices.push_back(template_vertices[0]);
				vertices.push_back(template_vertices[2]);
				vertices.push_back(template_vertices[3]);
			}

			// Right
			std::array<size_t, 3> side_indices = { 0, 3, 4 };
			generate_pyramid_face(side_indices, c_pyramid_normals[1]);

			// Back
			side_indices = { 1, 0, 4 };
			generate_pyramid_face(side_indices, c_pyramid_normals[2]);

			// Left
			side_indices = { 2, 1, 4 };
			generate_pyramid_face(side_indices, c_pyramid_normals[3]);

			// Front
			side_indices = { 3, 2, 4 };
			generate_pyramid_face(side_indices, c_pyramid_normals[4]);

			pyramid_info.vertex_count = static_cast<uint32_t>(vertices.size()) - pyramid_info.vertex_offset;
		}

		void generate_plane(std::vector<Vertex>& vertices)
		{
			// Allow for tiny extent on Y axis so the collision detection doesn't freak out
			const DirectX::BoundingBox default_box(Vector3(0, 0, 0), Vector3(0.5f, 0.001f, 0.5f));

			{
				ObjectInfo& plane_info = m_object_info[static_cast<size_t>(ObjectType::PLANE)];

				plane_info.vertex_offset = static_cast<uint32_t>(vertices.size());

				constexpr size_t c_plane_resolution = 32;
				constexpr float c_plane_step = 1.0f / c_plane_resolution;

				float z_offset = 0.5f;
				for (size_t current_z = 0; current_z < c_plane_resolution; ++current_z)
				{
					float x_offset = -0.5f;
					for (size_t current_x = 0; current_x < c_plane_resolution; ++current_x)
					{
						const Vector4 top_left(x_offset, 0.0f, z_offset, 1.0f);
						const Vector4 top_right(x_offset + c_plane_step, 0.0f, z_offset, 1.0f);
						const Vector4 bottom_left(x_offset, 0.0f, z_offset - c_plane_step, 1.0f);
						const Vector4 bottom_right(x_offset + c_plane_step, 0.0f, z_offset - c_plane_step, 1.0f);

						const Vector4 normal(0.0f, 1.0f, 0.0f, 0.0f);

						vertices.push_back(Vertex{ top_left, normal });
						vertices.push_back(Vertex{ top_right, normal });
						vertices.push_back(Vertex{ bottom_left, normal });

						vertices.push_back(Vertex{ top_right, normal });
						vertices.push_back(Vertex{ bottom_right, normal });
						vertices.push_back(Vertex{ bottom_left, normal });

						x_offset += c_plane_step;
					}
					z_offset -= c_plane_step;
				}

				plane_info.vertex_count = static_cast<uint32_t>(vertices.size()) - plane_info.vertex_offset;
			}

			ObjectInstanceInfo plane_instance_info;
			plane_instance_info.type = ObjectType::PLANE;

			plane_instance_info.per_draw_data.model = DirectX::XMMatrixScaling(100.0f, 1.0f, 100.0f);
			plane_instance_info.per_draw_data.inv_model = DirectX::XMMatrixInverse(nullptr, plane_instance_info.per_draw_data.model);

			plane_instance_info.per_draw_data.material.diffuse = Vector4(1.0f, 1.0f, 0.0f, 1.0f);
			plane_instance_info.per_draw_data.material.ambient = Vector4(1.0f, 1.0f, 1.0f, 1.0f);

			// Set bounding volume
			default_box.Transform(plane_instance_info.bounding_volume, plane_instance_info.per_draw_data.model);

			m_object_instances.push_back(plane_instance_info);
		}

		bool create_buffers(const std::vector<Vertex>& vertices)
		{
			GraphicsAPI& graphics_api = m_application.get_graphics_api();
			ID3D11Device* d3d_device = graphics_api.get_device();

			// Create vertex buffer
			{
				D3D11_BUFFER_DESC buffer_description;
				ZeroMemory(&buffer_description, sizeof(D3D11_BUFFER_DESC));

				buffer_description.ByteWidth = static_cast<uint32_t>(vertices.size() * sizeof(Vertex));
				buffer_description.Usage = D3D11_USAGE_DEFAULT;
				buffer_description.BindFlags = D3D11_BIND_VERTEX_BUFFER;
				buffer_description.MiscFlags = 0;

				D3D11_SUBRESOURCE_DATA subresource_data;
				ZeroMemory(&subresource_data, sizeof(D3D11_SUBRESOURCE_DATA));
				subresource_data.pSysMem = vertices.data();

				HRESULT result = d3d_device->CreateBuffer(&buffer_description, &subresource_data, m_vertex_buffer.ReleaseAndGetAddressOf());
				if (FAILED(result))
				{
					// TODO: error handling?
					return false;
				}
			}

			// Create camera cbuffer
			{
				// Prepare projection matrix
				{
					HWND window_handle = m_application.get_window_handle();

					RECT window_rect;
					if (!GetWindowRect(window_handle, &window_rect))
					{
						return false;
					}

					const Vector2 z_near_far = get_z_near_far();
					constexpr float c_fov_y = DirectX::XMConvertToRadians(70.0f);
					m_projection_matrix = get_perspective_matrix(c_fov_y, static_cast<float>(window_rect.right - window_rect.left), static_cast<float>(window_rect.bottom - window_rect.top), z_near_far.x, z_near_far.y);
				}

				Camera init_camera;

				D3D11_BUFFER_DESC buffer_description;
				ZeroMemory(&buffer_description, sizeof(D3D11_BUFFER_DESC));

				buffer_description.ByteWidth = sizeof(Camera);
				buffer_description.Usage = D3D11_USAGE_DYNAMIC;
				buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				buffer_description.MiscFlags = 0;
				buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				D3D11_SUBRESOURCE_DATA subresource_data;
				ZeroMemory(&subresource_data, sizeof(D3D11_SUBRESOURCE_DATA));
				subresource_data.pSysMem = &init_camera;

				HRESULT result = d3d_device->CreateBuffer(&buffer_description, &subresource_data, m_camera_buffer.ReleaseAndGetAddressOf());
				if (FAILED(result))
				{
					// TODO: error handling?
					return false;
				}
			}

			// Create per-draw cbuffer
			{
				PerDrawData init_data;

				D3D11_BUFFER_DESC buffer_description;
				ZeroMemory(&buffer_description, sizeof(D3D11_BUFFER_DESC));

				buffer_description.ByteWidth = sizeof(PerDrawData);
				buffer_description.Usage = D3D11_USAGE_DYNAMIC;
				buffer_description.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
				buffer_description.MiscFlags = 0;
				buffer_description.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;

				D3D11_SUBRESOURCE_DATA subresource_data;
				ZeroMemory(&subresource_data, sizeof(D3D11_SUBRESOURCE_DATA));
				subresource_data.pSysMem = &init_data;

				HRESULT result = d3d_device->CreateBuffer(&buffer_description, &subresource_data, m_per_draw_cbuffer.ReleaseAndGetAddressOf());
				if (FAILED(result))
				{
					// TODO: error handling?
					return false;
				}
			}

			return true;
		}

		void update(float dt)
		{
			handle_input();

			// Start a new render frame
			GraphicsAPI& graphics_api = m_application.get_graphics_api();
			graphics_api.begin_frame();

			m_light_system.update();

			ID3D11DeviceContext* device_context = graphics_api.get_device_context();

			// Set primitive topology
			device_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

			// Set shaders and constant buffers
			{
				ID3D11Buffer* constant_buffers[] = {
					m_camera_buffer.Get(),
					m_per_draw_cbuffer.Get()
				};

				device_context->VSSetShader(m_shader.vertex_shader.Get(), nullptr, 0);
				device_context->IASetInputLayout(m_shader.input_layout.Get());
				device_context->VSSetConstantBuffers(1, ARRAYSIZE(constant_buffers), constant_buffers);

				device_context->PSSetShader(m_shader.pixel_shader.Get(), nullptr, 0);
				device_context->PSSetConstantBuffers(1, ARRAYSIZE(constant_buffers), constant_buffers);
			}

			// Set vertex buffer
			{
				constexpr UINT c_vertex_stride = sizeof(Vertex);
				constexpr UINT c_null_offset = 0;
				device_context->IASetVertexBuffers(0, 1, m_vertex_buffer.GetAddressOf(), &c_vertex_stride, &c_null_offset);
			}

			// Update the camera
			{
				m_camera.update(dt);

				Camera shader_camera;
				shader_camera.world_position = m_camera.position;

				shader_camera.view = m_camera.view;
				shader_camera.view_projection = shader_camera.view * m_projection_matrix;

				D3D11_MAPPED_SUBRESOURCE mapped_subresource;
				ZeroMemory(&mapped_subresource, sizeof(D3D11_MAPPED_SUBRESOURCE));

				const HRESULT result = device_context->Map(m_camera_buffer.Get(), 0, D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
				if (SUCCEEDED(result))
				{
					memcpy(mapped_subresource.pData, &shader_camera, sizeof(Camera));
				}

				device_context->Unmap(m_camera_buffer.Get(), 0);
			}

			// Create camera frustum (from projection matrix)
			DirectX::BoundingFrustum bounding_frustum(m_projection_matrix);
			{
				const XMVector camera_rotation = DirectX::XMQuaternionRotationRollPitchYaw(m_camera.rotation.x, m_camera.rotation.y, 0.0f);
				bounding_frustum.Transform(bounding_frustum, 1.0f, camera_rotation, m_camera.position);
			}

			// FIXME: optimize via instancing?
			for (const ObjectInstanceInfo& current_object : m_object_instances)
			{
				if (bounding_frustum.Intersects(current_object.bounding_volume) == false)
				{
					// Object not visible, skip
					continue;
				}

				// Update per-draw const buffer
				{
					D3D11_MAPPED_SUBRESOURCE mapped_subresource;
					ZeroMemory(&mapped_subresource, sizeof(D3D11_MAPPED_SUBRESOURCE));

					const HRESULT result = device_context->Map(m_per_draw_cbuffer.Get(), 0, D3D11_MAP::D3D11_MAP_WRITE_DISCARD, 0, &mapped_subresource);
					if (SUCCEEDED(result))
					{
						memcpy(mapped_subresource.pData, &current_object.per_draw_data, sizeof(PerDrawData));
					}

					device_context->Unmap(m_per_draw_cbuffer.Get(), 0);
				}

				// Add draw call at the relevant offset
				const ObjectInfo& object_info = m_object_info[static_cast<size_t>(current_object.type)];
				device_context->Draw(object_info.vertex_count, object_info.vertex_offset);
			}
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

			if(GetKeyState('A') & c_keydown_flag)
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

		void control_input(int key_code)
		{
			switch (key_code)
			{
			case 'V':
				// Toggle the debug rendering in the light system
				m_light_system.toggle_debug_rendering();
				break;
			}
		}
	};

	RenderSystem::~RenderSystem() = default;

	CameraInfo RenderSystem::get_camera_info() const
	{
		return m_internal->m_camera.get_info();
	}

	Vector2 RenderSystem::get_z_near_far() const
	{
		return m_internal->get_z_near_far();
	}

	XMMatrix RenderSystem::get_camera_projection() const
	{
		return m_internal->m_projection_matrix;
	}

	RenderSystem::RenderSystem(Application& application)
		: m_internal(std::make_unique<Internal>(application))
	{

	}

	bool RenderSystem::initialize()
	{
		return m_internal->initialize();
	}

	void RenderSystem::update(float dt)
	{
		m_internal->update(dt);
	}

	void RenderSystem::control_input(int key_code)
	{
		m_internal->control_input(key_code);
	}
}