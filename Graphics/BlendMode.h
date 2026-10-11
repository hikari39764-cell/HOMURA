#pragma once

#include <d3d12.h>

namespace Homura {

enum class BlendMode {
	None,
	Normal,
	Add,
	Subtract,
	Multiply,
	Screen,
	Count
};

inline constexpr const char* kBlendModeNames[] = {
	"None", "Normal", "Add", "Subtract", "Multiply", "Screen"
};

inline D3D12_BLEND_DESC MakeBlendDesc(BlendMode blendMode) {
	D3D12_BLEND_DESC blendDesc{};
	D3D12_RENDER_TARGET_BLEND_DESC& target = blendDesc.RenderTarget[0];
	target.RenderTargetWriteMask = D3D12_COLOR_WRITE_ENABLE_ALL;
	target.BlendEnable = blendMode != BlendMode::None;
	target.SrcBlend = D3D12_BLEND_ONE;
	target.DestBlend = D3D12_BLEND_ZERO;
	target.BlendOp = D3D12_BLEND_OP_ADD;

	// Alphaは合成前の値をそのまま書き込む
	target.SrcBlendAlpha = D3D12_BLEND_ONE;
	target.DestBlendAlpha = D3D12_BLEND_ZERO;
	target.BlendOpAlpha = D3D12_BLEND_OP_ADD;

	// RGBの合成方法をBlendModeごとに切り替える
	switch (blendMode) {
	case BlendMode::Normal:
		target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		target.DestBlend = D3D12_BLEND_INV_SRC_ALPHA;
		break;
	case BlendMode::Add:
		target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		target.DestBlend = D3D12_BLEND_ONE;
		break;
	case BlendMode::Subtract:
		target.SrcBlend = D3D12_BLEND_SRC_ALPHA;
		target.DestBlend = D3D12_BLEND_ONE;
		target.BlendOp = D3D12_BLEND_OP_REV_SUBTRACT;
		break;
	case BlendMode::Multiply:
		target.SrcBlend = D3D12_BLEND_ZERO;
		target.DestBlend = D3D12_BLEND_SRC_COLOR;
		break;
	case BlendMode::Screen:
		target.SrcBlend = D3D12_BLEND_INV_DEST_COLOR;
		target.DestBlend = D3D12_BLEND_ONE;
		break;
	case BlendMode::None:
	case BlendMode::Count:
		break;
	}

	return blendDesc;
}

} // namespace Homura
