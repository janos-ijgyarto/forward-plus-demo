#ifndef FORWARDPLUSDEMO_RENDER_MATH_HPP
#define FORWARDPLUSDEMO_RENDER_MATH_HPP
#include <DirectXMath.h>
using namespace DirectX; // NOTE: required to get the operator overloads
namespace ForwardPlusDemo
{
	using Vector2i = DirectX::XMINT2;
	using Vector3i = DirectX::XMINT3;

	using Vector2 = DirectX::XMFLOAT2;
	using Vector3 = DirectX::XMFLOAT3;
	using Vector4 = DirectX::XMFLOAT4;

	using Matrix3 = DirectX::XMFLOAT3X3;
	using Matrix4 = DirectX::XMFLOAT4X4;

	using XMVector = DirectX::XMVECTOR;
	using XMMatrix = DirectX::XMMATRIX;

	inline XMVector to_xmvector(const Vector3& vector)
	{
		return DirectX::XMLoadFloat3(&vector);
	}

	inline Vector3 to_vector3(const XMVector& vector)
	{
		Vector3 result;
		DirectX::XMStoreFloat3(&result, vector);

		return result;
	}

	inline XMVector to_xmvector(const Vector4& vector)
	{
		return DirectX::XMLoadFloat4(&vector);
	}

	inline Vector4 to_vector4(const XMVector& vector)
	{
		Vector4 result;
		DirectX::XMStoreFloat4(&result, vector);

		return result;
	}

	// NOTE: the below utility functions will be dead slow, avoid where possible!
	inline Matrix3 to_matrix3(const XMMatrix& matrix)
	{
		Matrix3 result_mat;
		DirectX::XMStoreFloat3x3(&result_mat, matrix);

		return result_mat;
	}

	inline Matrix4 to_matrix4(const XMMatrix& matrix)
	{
		Matrix4 result_mat;
		DirectX::XMStoreFloat4x4(&result_mat, matrix);

		return result_mat;
	}

	inline Matrix4 get_transform_matrix(const Vector3& position, const Vector3& rotation, const Vector3& scale)
	{
		const XMVector xm_scale = DirectX::XMLoadFloat3(&scale);

		const Vector3 rotation_origin(0, 0, 0);
		const XMVector xm_rotation_origin = DirectX::XMLoadFloat3(&rotation_origin);
		const XMVector xm_rotation_quat = DirectX::XMQuaternionRotationRollPitchYaw(rotation.y, rotation.z, rotation.x);

		const XMVector xm_position = DirectX::XMLoadFloat3(&position);

		const XMMatrix xm_transform_matrix = DirectX::XMMatrixAffineTransformation(xm_scale, xm_rotation_origin, xm_rotation_quat, xm_position);
		return to_matrix4(xm_transform_matrix);
	}

	inline XMMatrix get_perspective_matrix(float fov_y, float view_width, float view_height, float near_z, float far_z)
	{
		return DirectX::XMMatrixPerspectiveFovLH(fov_y, view_width / view_height, near_z, far_z);
	}
}
#endif