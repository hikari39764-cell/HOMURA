#include <Windows.h>
#include <d3d12sdklayers.h>
#include <wincodec.h>
#include <wrl.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <functional>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _DEBUG
#include <crtdbg.h>
#endif

#include "Graphics/D3D12Context.h"
#include "Model/ModelLoader.h"
#include "Model/Primitive.h"
#include "Input/Input.h"
#include "Renderer/FrameRenderer.h"
#include "Renderer/Object3dRenderer.h"
#include "Renderer/ParticleRenderer.h"
#include "WinApp/WinApp.h"

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

#pragma comment(lib, "windowscodecs.lib")

using namespace Homura;
using Microsoft::WRL::ComPtr;

namespace {

constexpr UINT kWidth = 512;
constexpr UINT kHeight = 320;
int failures = 0;

void Check(bool passed, const char* name) {
	std::printf("%s %s\n", passed ? "PASS" : "FAIL", name);
	std::fflush(stdout);
	if (!passed) {
		++failures;
	}
}

void Require(bool passed, const char* name) {
	Check(passed, name);
	if (!passed) {
		throw std::runtime_error(name);
	}
}

void HResult(HRESULT result, const char* operation) {
	if (FAILED(result)) {
		std::printf("HRESULT %s = 0x%08lx\n", operation, static_cast<unsigned long>(result));
		throw std::runtime_error(operation);
	}
}

struct Pixel {
	uint8_t r, g, b, a;
};

using Image = std::vector<Pixel>;

// ReadbackしたRGBAを上から順にBMPへ保存する
void SaveBmp(const std::filesystem::path& path, const Image& image) {
	BITMAPFILEHEADER file{};
	BITMAPINFOHEADER info{};
	file.bfType = 0x4d42;
	file.bfOffBits = sizeof(file) + sizeof(info);
	file.bfSize = file.bfOffBits + kWidth * kHeight * sizeof(Pixel);
	info.biSize = sizeof(info);
	info.biWidth = kWidth;
	info.biHeight = -static_cast<LONG>(kHeight);
	info.biPlanes = 1;
	info.biBitCount = 32;
	info.biCompression = BI_RGB;
	std::ofstream stream(path, std::ios::binary);
	stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
	stream.write(reinterpret_cast<const char*>(&info), sizeof(info));
	for (const Pixel& pixel : image) {
		const Pixel bgra{ pixel.b, pixel.g, pixel.r, pixel.a };
		stream.write(reinterpret_cast<const char*>(&bgra), sizeof(bgra));
	}
	if (!stream) {
		throw std::runtime_error("save BMP");
	}
}

void SaveTexture(const std::filesystem::path& path, bool alphaSplit) {
	constexpr UINT size = 16;
	std::array<Pixel, size * size> pixels{};
	for (UINT y = 0; y < size; ++y) {
		for (UINT x = 0; x < size; ++x) {
			pixels[y * size + x] = { 255, 255, 255, static_cast<uint8_t>(alphaSplit && x < size / 2 ? 64 : 255) };
		}
	}
	ComPtr<IWICImagingFactory> factory;
	ComPtr<IWICStream> stream;
	ComPtr<IWICBitmapEncoder> encoder;
	ComPtr<IWICBitmapFrameEncode> frame;
	HResult(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER, IID_PPV_ARGS(&factory)), "WIC factory");
	HResult(factory->CreateStream(&stream), "WIC stream");
	HResult(stream->InitializeFromFilename(path.c_str(), GENERIC_WRITE), "PNG output");
	HResult(factory->CreateEncoder(GUID_ContainerFormatPng, nullptr, &encoder), "PNG encoder");
	HResult(encoder->Initialize(stream.Get(), WICBitmapEncoderNoCache), "PNG initialize");
	HResult(encoder->CreateNewFrame(&frame, nullptr), "PNG frame");
	HResult(frame->Initialize(nullptr), "PNG frame initialize");
	HResult(frame->SetSize(size, size), "PNG size");
	WICPixelFormatGUID format = GUID_WICPixelFormat32bppBGRA;
	HResult(frame->SetPixelFormat(&format), "PNG format");
	Require(IsEqualGUID(format, GUID_WICPixelFormat32bppBGRA), "WIC preserves test texture alpha");
	HResult(frame->WritePixels(size, size * sizeof(Pixel), sizeof(pixels), reinterpret_cast<BYTE*>(pixels.data())), "PNG pixels");
	HResult(frame->Commit(), "PNG frame commit");
	HResult(encoder->Commit(), "PNG commit");
}

class ValidationCamera : public ICamera {
public:
	explicit ValidationCamera(float width = 4.0f) {
		view_ = MakeTranslateMatrix({ 0.0f, 0.0f, 3.0f });
		const float height = width * static_cast<float>(kHeight) / kWidth;
		projection_ = MakeOrthographicMatrix(-width * 0.5f, height * 0.5f, width * 0.5f, -height * 0.5f, 0.1f, 20.0f);
	}
	const Matrix4x4& GetViewMatrix() const override { return view_; }
	const Matrix4x4& GetProjectionMatrix() const override { return projection_; }

private:
	Matrix4x4 view_;
	Matrix4x4 projection_;
};

class OffscreenTarget {
public:
	explicit OffscreenTarget(D3D12Context& context) : context_(context) {
		auto& device = context_.GetDevice();
		D3D12_HEAP_PROPERTIES heap{};
		heap.Type = D3D12_HEAP_TYPE_DEFAULT;
		D3D12_RESOURCE_DESC desc{};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_TEXTURE2D;
		desc.Width = kWidth;
		desc.Height = kHeight;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
		D3D12_CLEAR_VALUE colorClear{};
		colorClear.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		colorClear.Color[0] = 0.1f;
		colorClear.Color[1] = 0.2f;
		colorClear.Color[2] = 0.3f;
		colorClear.Color[3] = 1.0f;
		HResult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_RENDER_TARGET, &colorClear, IID_PPV_ARGS(&color_)), "offscreen color");
		device->GetCopyableFootprints(&desc, 0, 1, 0, &footprint_, nullptr, nullptr, &readbackSize_);

		desc.Format = DXGI_FORMAT_D24_UNORM_S8_UINT;
		desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_DEPTH_STENCIL;
		D3D12_CLEAR_VALUE depthClear{};
		depthClear.Format = desc.Format;
		depthClear.DepthStencil.Depth = 1.0f;
		HResult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_DEPTH_WRITE, &depthClear, IID_PPV_ARGS(&depth_)), "offscreen depth");
		D3D12_DESCRIPTOR_HEAP_DESC heapDesc{};
		heapDesc.NumDescriptors = 1;
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV;
		HResult(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&rtvHeap_)), "offscreen RTV heap");
		heapDesc.Type = D3D12_DESCRIPTOR_HEAP_TYPE_DSV;
		HResult(device->CreateDescriptorHeap(&heapDesc, IID_PPV_ARGS(&dsvHeap_)), "offscreen DSV heap");
		D3D12_RENDER_TARGET_VIEW_DESC rtv{};
		rtv.Format = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;
		rtv.ViewDimension = D3D12_RTV_DIMENSION_TEXTURE2D;
		device->CreateRenderTargetView(color_.Get(), &rtv, rtvHeap_->GetCPUDescriptorHandleForHeapStart());
		device->CreateDepthStencilView(depth_.Get(), nullptr, dsvHeap_->GetCPUDescriptorHandleForHeapStart());

		heap.Type = D3D12_HEAP_TYPE_READBACK;
		desc = {};
		desc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
		desc.Width = readbackSize_;
		desc.Height = 1;
		desc.DepthOrArraySize = 1;
		desc.MipLevels = 1;
		desc.SampleDesc.Count = 1;
		desc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;
		HResult(device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &desc,
			D3D12_RESOURCE_STATE_COPY_DEST, nullptr, IID_PPV_ARGS(&readback_)), "readback buffer");
	}

	Image Render(const std::function<void(ID3D12GraphicsCommandList*)>& draw,
		const std::array<float, 4>& clear = { 0.1f, 0.2f, 0.3f, 1.0f }) {
		Require(context_.ResetCommandList(), "reset validation command list");
		auto* commandList = context_.GetCommandList().Get();
		const auto rtv = rtvHeap_->GetCPUDescriptorHandleForHeapStart();
		const auto dsv = dsvHeap_->GetCPUDescriptorHandleForHeapStart();
		commandList->OMSetRenderTargets(1, &rtv, FALSE, &dsv);
		commandList->ClearRenderTargetView(rtv, clear.data(), 0, nullptr);
		commandList->ClearDepthStencilView(dsv, D3D12_CLEAR_FLAG_DEPTH, 1.0f, 0, 0, nullptr);
		D3D12_VIEWPORT viewport{ 0.0f, 0.0f, static_cast<float>(kWidth), static_cast<float>(kHeight), 0.0f, 1.0f };
		D3D12_RECT scissor{ 0, 0, kWidth, kHeight };
		commandList->RSSetViewports(1, &viewport);
		commandList->RSSetScissorRects(1, &scissor);
		context_.SetShaderVisibleDescriptorHeap();
		draw(commandList);

		D3D12_RESOURCE_BARRIER barrier{};
		barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
		barrier.Transition.pResource = color_.Get();
		barrier.Transition.Subresource = D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES;
		barrier.Transition.StateBefore = D3D12_RESOURCE_STATE_RENDER_TARGET;
		barrier.Transition.StateAfter = D3D12_RESOURCE_STATE_COPY_SOURCE;
		commandList->ResourceBarrier(1, &barrier);
		D3D12_TEXTURE_COPY_LOCATION source{};
		source.pResource = color_.Get();
		source.Type = D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
		D3D12_TEXTURE_COPY_LOCATION destination{};
		destination.pResource = readback_.Get();
		destination.Type = D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;
		destination.PlacedFootprint = footprint_;
		commandList->CopyTextureRegion(&destination, 0, 0, 0, &source, nullptr);
		std::swap(barrier.Transition.StateBefore, barrier.Transition.StateAfter);
		commandList->ResourceBarrier(1, &barrier);
		Require(context_.ExecuteCommandListAndWait(), "execute draw and GPU readback");
		void* mapped = nullptr;
		D3D12_RANGE range{ 0, static_cast<SIZE_T>(readbackSize_) };
		HResult(readback_->Map(0, &range, &mapped), "map readback");
		Image image(kWidth * kHeight);
		for (UINT y = 0; y < kHeight; ++y) {
			const auto* row = static_cast<const uint8_t*>(mapped) + footprint_.Offset + y * footprint_.Footprint.RowPitch;
			std::memcpy(image.data() + y * kWidth, row, kWidth * sizeof(Pixel));
		}
		D3D12_RANGE noWrite{ 0, 0 };
		readback_->Unmap(0, &noWrite);
		return image;
	}

private:
	D3D12Context& context_;
	ComPtr<ID3D12Resource> color_, depth_, readback_;
	ComPtr<ID3D12DescriptorHeap> rtvHeap_, dsvHeap_;
	D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint_{};
	UINT64 readbackSize_ = 0;
};

int ToSrgb(float value) {
	value = std::clamp(value, 0.0f, 1.0f);
	const float encoded = value <= 0.0031308f ? value * 12.92f : 1.055f * std::pow(value, 1.0f / 2.4f) - 0.055f;
	return static_cast<int>(std::lround(encoded * 255.0f));
}

bool NearColor(const Pixel& pixel, const Vector4& expected, int tolerance = 3) {
	return std::abs(static_cast<int>(pixel.r) - ToSrgb(expected.x)) <= tolerance &&
		std::abs(static_cast<int>(pixel.g) - ToSrgb(expected.y)) <= tolerance &&
		std::abs(static_cast<int>(pixel.b) - ToSrgb(expected.z)) <= tolerance &&
		std::abs(static_cast<int>(pixel.a) - static_cast<int>(std::lround(expected.w * 255.0f))) <= tolerance;
}

void InitializeObject(D3D12Context& context, Object3dRenderer& object, const ModelData& model, UINT srvIndex) {
	Require(context.ResetCommandList(), "reset texture upload command list");
	ComPtr<ID3D12Resource> upload;
	Require(object.Initialize(context.GetDevice(), context.GetCommandList(),
		context.GetSRVCPUDescriptorHandle(srvIndex), context.GetSRVGPUDescriptorHandle(srvIndex), &upload, model), "initialize object pipeline and texture");
	Require(context.ExecuteCommandListAndWait(), "finish texture upload");
	object.SetLightingEnabled(false);
}

void CheckBlendAndCutout(D3D12Context& context, OffscreenTarget& target, const std::filesystem::path& directory,
	const ModelData& plane, Object3dRenderer& background) {
	ValidationCamera camera;
	background.UpdateTransformationMatrix(camera);
	const Vector4 source{ 0.6f, 0.3f, 0.2f, 0.4f };
	const std::array<Vector4, 6> expected{{
		{ 0.6f, 0.3f, 0.2f, 0.4f },
		{ 0.3f, 0.24f, 0.26f, 0.4f },
		{ 0.34f, 0.32f, 0.38f, 0.4f },
		{ 0.0f, 0.08f, 0.22f, 0.4f },
		{ 0.06f, 0.06f, 0.06f, 0.4f },
		{ 0.64f, 0.44f, 0.44f, 0.4f }
	}};
	background.SetMaterialColor(source);
	for (size_t i = 0; i < expected.size(); ++i) {
		background.SetBlendMode(static_cast<BlendMode>(i));
		Image image = target.Render([&](auto* commands) { background.Draw(commands); });
		const Pixel& center = image[(kHeight / 2) * kWidth + kWidth / 2];
		std::printf("BLEND %s RGBA=(%u,%u,%u,%u)\n", kBlendModeNames[i], center.r, center.g, center.b, center.a);
		Check(NearColor(center, expected[i]), (std::string("GPU blend equation: ") + kBlendModeNames[i]).c_str());
		SaveBmp(directory / (std::string("blend-") + kBlendModeNames[i] + ".bmp"), image);
	}

	ModelData cutout = plane;
	cutout.material.textureFilePath = (directory / "alpha-split.png").generic_string();
	for (auto& vertex : cutout.vertices) {
		vertex.texcoord = { (vertex.position.x + 1.0f) * 0.5f, (1.0f - vertex.position.y) * 0.5f };
	}
	Object3dRenderer foreground;
	InitializeObject(context, foreground, cutout, 11);
	foreground.GetTransform().translate.z = -0.5f;
	foreground.SetMaterialColor({ 1.0f, 0.0f, 0.0f, 1.0f });
	foreground.UpdateTransformationMatrix(camera);
	background.SetBlendMode(BlendMode::None);
	background.SetMaterialColor({ 0.0f, 0.0f, 1.0f, 1.0f });
	for (float cutoff : { 0.0f, 0.5f, 1.0f }) {
		foreground.SetAlphaCutoff(cutoff);
		const Image image = target.Render([&](auto* commands) { foreground.Draw(commands); background.Draw(commands); });
		const Pixel& left = image[(kHeight / 2) * kWidth + kWidth * 3 / 8];
		const Pixel& right = image[(kHeight / 2) * kWidth + kWidth * 5 / 8];
		const bool leftCorrect = cutoff == 0.0f ? NearColor(left, { 1.0f, 0.0f, 0.0f, 64.0f / 255.0f }) : NearColor(left, { 0.0f, 0.0f, 1.0f, 1.0f });
		const bool rightCorrect = cutoff < 1.0f ? NearColor(right, { 1.0f, 0.0f, 0.0f, 1.0f }) : NearColor(right, { 0.0f, 0.0f, 1.0f, 1.0f });
		std::printf("CUTOUT cutoff=%.1f left=(%u,%u,%u,%u) right=(%u,%u,%u,%u)\n", cutoff, left.r, left.g, left.b, left.a, right.r, right.g, right.b, right.a);
		Check(leftCorrect && rightCorrect, "alpha cutoff discards color and depth before drawing background");
		if (cutoff == 0.5f) {
			SaveBmp(directory / "alpha-cutout.bmp", image);
		}
	}
	foreground.SetAlphaCutoff(0.0f);
	foreground.SetMaterialColor({ 1.0f, 0.0f, 0.0f, 0.0f });
	const Image zeroAlpha = target.Render([&](auto* commands) { foreground.Draw(commands); background.Draw(commands); });
	Check(NearColor(zeroAlpha[(kHeight / 2) * kWidth + kWidth * 5 / 8], { 0.0f, 0.0f, 1.0f, 1.0f }), "zero final material alpha also discards depth");
	foreground.Finalize();
}

Vector4 TransformPosition(const Vector4& position, const Matrix4x4& matrix) {
	return {
		position.x * matrix.m[0][0] + position.y * matrix.m[1][0] + position.z * matrix.m[2][0] + position.w * matrix.m[3][0],
		position.x * matrix.m[0][1] + position.y * matrix.m[1][1] + position.z * matrix.m[2][1] + position.w * matrix.m[3][1],
		position.x * matrix.m[0][2] + position.y * matrix.m[1][2] + position.z * matrix.m[2][2] + position.w * matrix.m[3][2],
		1.0f
	};
}

void CheckModelNodes(D3D12Context& context, OffscreenTarget& target, const std::filesystem::path& directory, const ModelData& obj) {
	bool objIdentity = true;
	for (int row = 0; row < 4; ++row) {
		for (int column = 0; column < 4; ++column) {
			objIdentity = objIdentity && std::abs(obj.rootNode.localMatrix.m[row][column] - (row == column ? 1.0f : 0.0f)) < 0.0001f;
		}
	}
	Check(obj.vertices.size() == 6 && objIdentity, "OBJ triangle expansion and identity root node");
	ModelData gltf = LoadModelFile("Resources/CG3", "plane.gltf");
	Require(gltf.vertices.size() == 6, "glTF fixture loads six expanded vertices");
	float minX = 100.0f, maxX = -100.0f;
	bool rootNotBaked = true;
	for (const auto& vertex : gltf.vertices) {
		minX = (std::min)(minX, vertex.position.x);
		maxX = (std::max)(maxX, vertex.position.x);
		rootNotBaked = rootNotBaked && std::abs(vertex.position.y) < 0.0001f;
	}
	Check(std::abs(minX + 1.25f) < 0.0001f && std::abs(maxX - 0.75f) < 0.0001f && rootNotBaked,
		"glTF child translation is baked once while root rotation remains separate");
	Check(std::abs(gltf.rootNode.localMatrix.m[1][2] - 1.0f) < 0.0001f &&
		std::abs(gltf.rootNode.localMatrix.m[2][1] + 1.0f) < 0.0001f, "glTF root rotation is converted to row-vector layout");

	ModelData expected = gltf;
	const Matrix4x4 root = gltf.rootNode.localMatrix;
	for (auto& vertex : expected.vertices) {
		vertex.position = TransformPosition(vertex.position, root);
		const Vector4 normal = TransformPosition({ vertex.normal.x, vertex.normal.y, vertex.normal.z, 0.0f }, root);
		vertex.normal = Normalize({ normal.x, normal.y, normal.z });
	}
	expected.rootNode.localMatrix = MakeIdentity4x4();
	Object3dRenderer actualObject, expectedObject;
	InitializeObject(context, actualObject, gltf, 12);
	InitializeObject(context, expectedObject, expected, 13);
	ValidationCamera camera;
	actualObject.UpdateTransformationMatrix(camera);
	expectedObject.UpdateTransformationMatrix(camera);
	const Image actualImage = target.Render([&](auto* commands) { actualObject.Draw(commands); });
	const Image expectedImage = target.Render([&](auto* commands) { expectedObject.Draw(commands); });
	const Image clear = target.Render([](auto*) {});
	size_t difference = 0, coverage = 0;
	for (size_t i = 0; i < actualImage.size(); ++i) {
		const auto& a = actualImage[i];
		const auto& b = expectedImage[i];
		if (std::abs(a.r - b.r) > 2 || std::abs(a.g - b.g) > 2 || std::abs(a.b - b.b) > 2) {
			++difference;
		}
		if (std::abs(a.r - clear[i].r) > 4 || std::abs(a.g - clear[i].g) > 4 || std::abs(a.b - clear[i].b) > 4) {
			++coverage;
		}
	}
	std::printf("ROOT pixelsDifferent=%zu covered=%zu\n", difference, coverage);
	Check(coverage > 1000 && difference < actualImage.size() / 100, "GPU applies glTF root matrix exactly once");
	SaveBmp(directory / "gltf-root-once.bmp", actualImage);
	SaveBmp(directory / "gltf-root-reference.bmp", expectedImage);
	actualObject.Finalize();
	expectedObject.Finalize();
}

void CheckLighting(D3D12Context& context, OffscreenTarget& target, const std::filesystem::path& directory, const ModelData& plane) {
	ModelData facingPlane = plane;
	for (auto& vertex : facingPlane.vertices) {
		vertex.normal = { 0.0f, 0.0f, -1.0f };
	}
	Object3dRenderer object;
	InitializeObject(context, object, facingPlane, 14);
	ValidationCamera camera;
	object.UpdateTransformationMatrix(camera);
	object.SetLightingEnabled(true);
	object.SetMaterialColor({ 0.1f, 0.1f, 0.1f, 0.35f });
	object.SetShininess(32.0f);
	object.SetDirectionalLight({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.25f, 0.0f, 1.0f }, 0.5f);
	PointLight point;
	point.intensity = 0.0f;
	object.SetPointLight(point);
	SpotLight spot;
	spot.intensity = 0.0f;
	object.SetSpotLight(spot);
	const char* names[]{ "half-lambert", "phong", "blinn-phong" };
	std::array<Pixel, 3> centers{};
	for (int i = 0; i < 3; ++i) {
		object.SetShadingModel(static_cast<ShadingModel>(i));
		const Image image = target.Render([&](auto* commands) { object.Draw(commands); });
		centers[i] = image[kHeight / 2 * kWidth + kWidth / 2];
		std::printf("SHADING %s center=(%u,%u,%u,%u)\n", names[i], centers[i].r, centers[i].g, centers[i].b, centers[i].a);
		Check(std::abs(static_cast<int>(centers[i].a) - 89) <= 2, "light model preserves material alpha");
		SaveBmp(directory / (std::string("lighting-") + names[i] + ".bmp"), image);
	}
	Check(centers[1].r > centers[0].r + 25 && centers[2].r > centers[1].r + 10,
		"Phong and Blinn-Phong produce distinct expected specular responses");

	object.SetShadingModel(ShadingModel::HalfLambert);
	object.SetMaterialColor({ 0.25f, 0.25f, 0.25f, 0.35f });
	object.SetDirectionalLight({ 1.0f, 1.0f, 1.0f, 1.0f }, { 0.0f, 0.0f, 1.0f }, 0.0f);
	point.position = { 0.0f, 0.0f, -1.0f };
	point.intensity = 1.0f;
	point.radius = 4.0f;
	point.decay = 1.0f;
	object.SetPointLight(point);
	const Image nearPoint = target.Render([&](auto* commands) { object.Draw(commands); });
	SaveBmp(directory / "lighting-point.bmp", nearPoint);
	point.position.z = -10.0f;
	object.SetPointLight(point);
	const Image farPoint = target.Render([&](auto* commands) { object.Draw(commands); });
	const size_t center = kHeight / 2 * kWidth + kWidth / 2;
	Check(nearPoint[center].r > 100 && farPoint[center].r <= 1, "point light attenuates to zero outside its radius");
	Check(std::abs(static_cast<int>(farPoint[center].a) - 89) <= 2, "point light attenuation does not fade alpha");
	point.intensity = 0.0f;
	object.SetPointLight(point);
	spot.position = { 0.0f, 0.0f, -1.0f };
	spot.direction = { 0.0f, 0.0f, 1.0f };
	spot.intensity = 1.0f;
	spot.distance = 4.0f;
	spot.decay = 1.0f;
	spot.cosAngle = 0.8f;
	spot.cosFalloffStart = 0.95f;
	object.SetSpotLight(spot);
	const Image insideCone = target.Render([&](auto* commands) { object.Draw(commands); });
	SaveBmp(directory / "lighting-spot.bmp", insideCone);
	const Pixel& midCone = insideCone[kHeight / 2 * kWidth + kWidth * 5 / 8];
	const Pixel& outerCone = insideCone[kHeight / 2 * kWidth + kWidth * 23 / 32];
	Check(insideCone[center].r > midCone.r + 15 && midCone.r > outerCone.r + 15 && outerCone.r <= 1,
		"spot light has a bright center, smooth falloff and dark exterior");
	spot.direction = { 1.0f, 0.0f, 0.0f };
	object.SetSpotLight(spot);
	const Image outsideCone = target.Render([&](auto* commands) { object.Draw(commands); });
	Check(outsideCone[center].r <= 1, "turning spot direction away removes illumination");
	object.Finalize();
}

void CheckParticles(D3D12Context& context, OffscreenTarget& target, const std::filesystem::path& directory) {
	ParticleRenderer particles;
	Require(particles.Initialize(context.GetDevice(), context.GetSRVCPUDescriptorHandle(2),
		context.GetSRVGPUDescriptorHandle(2), context.GetSRVGPUDescriptorHandle(10)), "initialize particle StructuredBuffer and pipelines");
	ValidationCamera camera(8.0f);
	particles.SetDemoMode(ParticleRenderer::DemoMode::Static);
	Check(particles.GetParticleCount() == 10, "particle static mode creates ten planes");
	particles.Update(0.0f, camera);
	SaveBmp(directory / "particles-static-10.bmp", target.Render([&](auto* commands) { particles.Draw(commands); }));
	particles.SetDemoMode(ParticleRenderer::DemoMode::Simulation);
	particles.GetEmitter().enabled = false;
	Check(particles.GetParticleCount() == 3, "simulation starts with default emitter count three");
	particles.Emit(10000);
	Check(particles.GetParticleCount() == ParticleRenderer::kMaxParticles, "emission is bounded by StructuredBuffer capacity");
	particles.Update(0.2f, camera);
	SaveBmp(directory / "particles-capacity-1000.bmp", target.Render([&](auto* commands) { particles.Draw(commands); }));
	particles.Update(3.1f, camera);
	Check(particles.GetParticleCount() == 0, "particles expire after their 1-3 second lifetimes");
	particles.GetEmitter().enabled = true;
	particles.GetEmitter().frequencyTime = 0.0f;
	particles.Update(0.2f, camera);
	Check(particles.GetParticleCount() == 0, "emitter waits for its interval");
	particles.Update(0.4f, camera);
	Check(particles.GetParticleCount() == 3 && std::abs(particles.GetEmitter().frequencyTime - 0.1f) < 0.0001f,
		"emitter creates three particles and keeps remaining time");
	particles.Update(0.4f, camera);
	Check(particles.GetParticleCount() == 6, "emitter uses the carried time for the next emission");
	particles.GetAccelerationField().enabled = true;
	particles.SetBillboardEnabled(false);
	particles.Update(1.0f / 60.0f, camera);
	for (int blend = 0; blend < static_cast<int>(BlendMode::Count); ++blend) {
		particles.SetBlendMode(static_cast<BlendMode>(blend));
		particles.Update(0.0f, camera);
		target.Render([&](auto* commands) { particles.Draw(commands); });
	}
	Check(true, "particle field, billboard-off and all six blend pipelines execute");
	particles.Finalize();
	particles.Finalize();
	Check(particles.GetParticleCount() == 0, "particle Finalize is repeatable");
}

void CheckSphere(D3D12Context& context, OffscreenTarget& target, const std::filesystem::path& directory) {
	const ModelData model = MakeSphereModel((directory / "white.png").generic_string());
	Require(!model.vertices.empty(), "MakeSphereModel creates renderable vertices");
	Object3dRenderer sphere;
	InitializeObject(context, sphere, model, 15);
	ValidationCamera camera(5.5f);
	sphere.GetTransform().scale = { 1.0f, 1.5f, 0.65f };
	sphere.SetMaterialColor({ 0.35f, 0.45f, 0.6f, 1.0f });
	sphere.SetLightingEnabled(true);
	sphere.SetShadingModel(ShadingModel::BlinnPhong);
	sphere.SetShininess(40.0f);
	sphere.SetDirectionalLight({ 1.0f, 1.0f, 1.0f, 1.0f }, { -0.5f, -0.6f, 1.0f }, 1.0f);
	PointLight point;
	point.intensity = 0.0f;
	sphere.SetPointLight(point);
	SpotLight spot;
	spot.intensity = 0.0f;
	sphere.SetSpotLight(spot);
	sphere.UpdateTransformationMatrix(camera);
	const Image image = target.Render([&](auto* commands) { sphere.Draw(commands); });
	SaveBmp(directory / "sphere-nonuniform-specular.bmp", image);
	const Pixel& center = image[kHeight / 2 * kWidth + kWidth / 2];
	std::printf("SPHERE center=(%u,%u,%u,%u)\n", center.r, center.g, center.b, center.a);
	Check(center.b > 100 && center.a == 255, "nonuniform sphere exposes its lit outward-facing surface");
	sphere.Finalize();
}

ComPtr<ID3D12InfoQueue> GetActiveDeviceInfoQueue() {
	ComPtr<IDXGIFactory6> factory;
	ComPtr<ID3D12InfoQueue> result;
	if (FAILED(CreateDXGIFactory1(IID_PPV_ARGS(&factory)))) {
		return result;
	}
	for (UINT index = 0; ; ++index) {
		ComPtr<IDXGIAdapter4> adapter;
		if (FAILED(factory->EnumAdapterByGpuPreference(index, DXGI_GPU_PREFERENCE_HIGH_PERFORMANCE, IID_PPV_ARGS(&adapter)))) {
			break;
		}
		DXGI_ADAPTER_DESC3 desc{};
		if (FAILED(adapter->GetDesc3(&desc)) || (desc.Flags & DXGI_ADAPTER_FLAG3_SOFTWARE)) {
			continue;
		}
		ComPtr<ID3D12Device> device;
		if (SUCCEEDED(D3D12CreateDevice(adapter.Get(), D3D_FEATURE_LEVEL_12_0, IID_PPV_ARGS(&device)))) {
			device.As(&result);
		}
		break;
	}
	return result;
}

void CheckDebugMessages(ID3D12InfoQueue* queue) {
	if (queue == nullptr) {
		std::puts("INFO D3D12 debug layer unavailable in this configuration");
		return;
	}
	unsigned errors = 0, warnings = 0;
	for (UINT64 i = 0; i < queue->GetNumStoredMessagesAllowedByRetrievalFilter(); ++i) {
		SIZE_T size = 0;
		HResult(queue->GetMessage(i, nullptr, &size), "debug message length");
		std::vector<uint8_t> bytes(size);
		auto* message = reinterpret_cast<D3D12_MESSAGE*>(bytes.data());
		HResult(queue->GetMessage(i, message, &size), "debug message");
		if (message->Severity <= D3D12_MESSAGE_SEVERITY_ERROR) {
			++errors;
		}
		if (message->Severity == D3D12_MESSAGE_SEVERITY_WARNING) {
			++warnings;
		}
		if (message->Severity <= D3D12_MESSAGE_SEVERITY_WARNING) {
			std::printf("D3D12 severity=%u id=%u %s\n", message->Severity, message->ID, message->pDescription);
		}
	}
	std::printf("D3D12 errors=%u warnings=%u\n", errors, warnings);
	Check(errors == 0, "GPU validation reports zero errors/corruption");
}

void CheckIntegratedScenes(WinApp& window) {
	Input input;
	FrameRenderer renderer;
	Require(input.Initialize(&window), "integrated input initialization");
	Require(renderer.Initialize(&window), "integrated FrameRenderer loads every resource and shader");
#ifdef USE_IMGUI
	Require(ImGui::GetCurrentContext() != nullptr, "integrated ImGui context exists");
	// 検証中は作業フォルダへImGuiの設定を書き込まない
	ImGui::GetIO().IniFilename = nullptr;
#endif
	auto queue = GetActiveDeviceInfoQueue();
	if (queue != nullptr) {
		queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
		queue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
	}
	for (int scene = 0; scene < static_cast<int>(FrameRenderer::DemoScene::Count); ++scene) {
		const auto selected = static_cast<FrameRenderer::DemoScene>(scene);
		renderer.SetDemoScene(selected);
		Check(renderer.GetDemoScene() == selected, "integrated demo selection is applied");
		for (int frame = 0; frame < 3; ++frame) {
			Require(!window.ProcessMessage(), "integrated window remains active");
			input.Update();
			renderer.Update(input);
			Require(renderer.Draw(), "integrated scene draws with GUI");
		}
		std::printf("INTEGRATED scene=%d frames=3 complete\n", scene);
	}
	CheckDebugMessages(queue.Get());
	queue.Reset();
	renderer.Finalize();
	renderer.Finalize();
	input.Finalize();
	Check(!renderer.Draw(), "integrated repeated Finalize leaves drawing disabled");
	Check(!IsWindowVisible(window.GetHwnd()), "integrated validation keeps the same window hidden");
}

} // namespace

int wmain(int argc, wchar_t** argv) {
#ifdef _DEBUG
	// assertは標準エラーへ出し、非表示のダイアログ待ちにしない
	for (int report : { _CRT_WARN, _CRT_ERROR, _CRT_ASSERT }) {
		_CrtSetReportMode(report, _CRTDBG_MODE_FILE);
		_CrtSetReportFile(report, _CRTDBG_FILE_STDERR);
	}
#endif
	if (argc != 2) {
		std::puts("Usage: CG3Validation.exe <output-directory>");
		return 2;
	}
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	const std::filesystem::path directory = std::filesystem::absolute(argv[1]);
	std::filesystem::create_directories(directory);
	WinApp window;
	D3D12Context context;
	try {
		Require(window.Initialize(SW_HIDE), "initialize hidden validation window");
		Require(!IsWindowVisible(window.GetHwnd()), "validation window stays hidden");
		Require(context.Initialize(&window), "initialize DirectX12 context");
		ComPtr<ID3D12InfoQueue> infoQueue;
		context.GetDevice().As(&infoQueue);
		if (infoQueue != nullptr) {
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_CORRUPTION, FALSE);
			infoQueue->SetBreakOnSeverity(D3D12_MESSAGE_SEVERITY_ERROR, FALSE);
		}
		SaveTexture(directory / "white.png", false);
		SaveTexture(directory / "alpha-split.png", true);
		{
			OffscreenTarget target(context);
			ModelData plane = LoadObjFile("Resources", "plane.obj");
			Require(plane.vertices.size() == 6, "load OBJ plane for GPU pixel checks");
			plane.material.textureFilePath = (directory / "white.png").generic_string();
			Object3dRenderer object;
			InitializeObject(context, object, plane, 10);
			CheckBlendAndCutout(context, target, directory, plane, object);
			CheckParticles(context, target, directory);
			CheckLighting(context, target, directory, plane);
			CheckSphere(context, target, directory);
			CheckModelNodes(context, target, directory, plane);
			object.Finalize();
			Require(context.WaitForGpu(), "finish GPU work before releasing test resources");
		}
		CheckDebugMessages(infoQueue.Get());
		infoQueue.Reset();
		context.Finalize();
		CheckIntegratedScenes(window);
		window.Finalize();
		std::printf("RESULT failures=%d\n", failures);
		return failures == 0 ? 0 : 1;
	} catch (const std::exception& error) {
		std::printf("FATAL %s\n", error.what());
		context.Finalize();
		window.Finalize();
		return 2;
	}
}
