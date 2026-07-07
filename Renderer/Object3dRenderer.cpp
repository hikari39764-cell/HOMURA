#include "Object3dRenderer.h"

#include <cassert>
#include <cstring>
#include <format>
#include <dxcapi.h>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#endif

#pragma comment(lib, "dxcompiler.lib")

#include "Input/Input.h"
#include "DebugTools/Logger/Logger.h"
#include "Model/ModelLoader.h"
#include "WinApp/WinConfig.h"

bool Object3dRenderer::Initialize(
	const Microsoft::WRL::ComPtr<ID3D12Device>& device,
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU,
	Microsoft::WRL::ComPtr<ID3D12Resource>* textureIntermediateResource,
	float aspectRatio
) {
	assert(device.Get() != nullptr);
	assert(commandList.Get() != nullptr);
	assert(textureIntermediateResource != nullptr);

	if (device.Get() == nullptr || commandList.Get() == nullptr || textureIntermediateResource == nullptr) {
		return false;
	}

	device_ = device;

	// OBJを読み込んで頂点とMaterial情報を作る
	if (!LoadModel()) {
		return false;
	}

	// Textureを読み込んでShaderから使えるようにする
	if (!CreateTexture(commandList, textureSrvHandleCPU, textureSrvHandleGPU, textureIntermediateResource)) {
		return false;
	}

	// 三角形を描画するためのPipelineStateを作成する
	if (!CreateGraphicsPipelineState()) {
		return false;
	}

	// Model用の頂点Resourceを作成する
	if (!CreateVertexResource()) {
		return false;
	}

	// マテリアル用のResourceを作成する
	if (!CreateMaterialResource()) {
		return false;
	}

	// 座標変換用のResourceを作成する
	if (!CreateTransformationMatrixResource()) {
		return false;
	}

	// 平行光源用のResourceを作成する
	if (!CreateDirectionalLightResource()) {
		return false;
	}

	// デバッグカメラの射影行列を画面比率に合わせて初期化する
	debugCamera_.Initialize(aspectRatio);

	return true;
}

void Object3dRenderer::Finalize() {
	textureManagerModel_.Finalize();

	// Mapしている先頭アドレスはResource本体をComPtrに任せ、こちらでは参照だけ外しておく
	directionalLightData_ = nullptr;
	transformationMatrixData_ = nullptr;
	materialData_ = nullptr;

	directionalLightResource_.Reset();
	transformationMatrixResource_.Reset();
	materialResource_.Reset();
	vertexResource_.Reset();
	graphicsPipelineState_.Reset();
	rootSignature_.Reset();
	device_.Reset();
}

void Object3dRenderer::Update(const Input& input) {
	if (input.IsTriggerKey(DIK_F1)) {
		useDebugCamera_ = !useDebugCamera_;
	}

	if (useDebugCamera_) {
		debugCamera_.Update(input);
	}
}

void Object3dRenderer::DrawDebugGui() {
#ifdef USE_IMGUI
	ImGui::Begin("Settings");

	if (materialData_ != nullptr) {
		ImGui::ColorEdit4("color", &materialData_->color.x);

		bool enableLighting = materialData_->enableLighting != 0;
		if (ImGui::Checkbox("enableLighting", &enableLighting)) {
			materialData_->enableLighting = enableLighting ? 1 : 0;
		}
	}

	ImGui::Checkbox("UseDebugCamera(F1)", &useDebugCamera_);

	if (useDebugCamera_) {
		ImGui::Text("RightDrag:Look  WASD/QE:Move  ZC:Roll  Shift:Fast  R:Reset");
	} else {
		ImGui::DragFloat3("CameraTranslate", &cameraTransform_.translate.x, 0.01f);
		ImGui::DragFloat("CameraRotateX", &cameraTransform_.rotate.x, 0.01f);
		ImGui::DragFloat("CameraRotateY", &cameraTransform_.rotate.y, 0.01f);
		ImGui::DragFloat("CameraRotateZ", &cameraTransform_.rotate.z, 0.01f);
	}

	if (directionalLightData_ != nullptr) {
		ImGui::ColorEdit4("LightColor", &directionalLightData_->color.x);
		ImGui::DragFloat3("LightDirection", &directionalLightData_->direction.x, 0.01f);
		ImGui::DragFloat("Intensity", &directionalLightData_->intensity, 0.01f);

		// ライト方向は単位ベクトルである必要があるので正規化する
		directionalLightData_->direction = Normalize(directionalLightData_->direction);
	}

	ImGui::End();
#endif
}

void Object3dRenderer::Draw(ID3D12GraphicsCommandList* commandList) {
	assert(commandList != nullptr);

	if (commandList == nullptr) {
		return;
	}

	// Shaderと描画設定をまとめたPipelineStateを設定する
	commandList->SetGraphicsRootSignature(rootSignature_.Get());
	commandList->SetPipelineState(graphicsPipelineState_.Get());

	// Modelの頂点データをInputAssemblerへ渡す
	commandList->IASetVertexBuffers(0, 1, &vertexBufferView_);

	// 三角形リストとしてModelを描画する
	commandList->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);

	// PixelShaderで使うマテリアルCBufferの場所を設定する
	commandList->SetGraphicsRootConstantBufferView(0, materialResource_->GetGPUVirtualAddress());

	// VertexShaderで使う座標変換CBufferの場所を設定する
	commandList->SetGraphicsRootConstantBufferView(1, transformationMatrixResource_->GetGPUVirtualAddress());

	// PixelShaderで使うModel用TextureのSRVを設定する
	commandList->SetGraphicsRootDescriptorTable(2, textureManagerModel_.GetTextureSrvHandleGPU());

	// PixelShaderで使う平行光源Bufferの場所を設定する
	commandList->SetGraphicsRootConstantBufferView(3, directionalLightResource_->GetGPUVirtualAddress());

	// OBJのFaceから展開した頂点数で描画命令を積む
	commandList->DrawInstanced(static_cast<UINT>(modelData_.vertices.size()), 1, 0, 0);
}

bool Object3dRenderer::LoadModel() {
	// ResourcesフォルダのOBJを読み込んでModelDataにする
	modelData_ = LoadObjFile("Resources", "plane.obj");
	assert(!modelData_.vertices.empty());
	assert(!modelData_.material.textureFilePath.empty());

	if (modelData_.vertices.empty() || modelData_.material.textureFilePath.empty()) {
		return false;
	}

	Log(std::format(
		"Complete load model, vertices:{}, texture:{}!!!\n",
		modelData_.vertices.size(),
		modelData_.material.textureFilePath
	));

	return true;
}


bool Object3dRenderer::CreateTexture(
	const Microsoft::WRL::ComPtr<ID3D12GraphicsCommandList>& commandList,
	D3D12_CPU_DESCRIPTOR_HANDLE textureSrvHandleCPU,
	D3D12_GPU_DESCRIPTOR_HANDLE textureSrvHandleGPU,
	Microsoft::WRL::ComPtr<ID3D12Resource>* intermediateResource
) {
	// OBJのMaterialで指定されたTextureを読み込む
	return textureManagerModel_.Initialize(
		device_,
		commandList,
		modelData_.material.textureFilePath,
		textureSrvHandleCPU,
		textureSrvHandleGPU,
		intermediateResource
	);
}

Microsoft::WRL::ComPtr<IDxcBlob> Object3dRenderer::CompileShader(
	const std::wstring& filePath,
	const wchar_t* profile,
	const Microsoft::WRL::ComPtr<IDxcUtils>& dxcUtils,
	const Microsoft::WRL::ComPtr<IDxcCompiler3>& dxcCompiler,
	const Microsoft::WRL::ComPtr<IDxcIncludeHandler>& includeHandler
) {
	// ログ
	Log(std::format(L"Begin CompileShader, path:{}, profile:{}\n", filePath, profile));

	// hlslファイルを読み込む
	Microsoft::WRL::ComPtr<IDxcBlobEncoding> shaderSource;
	HRESULT hr = dxcUtils->LoadFile(filePath.c_str(), nullptr, shaderSource.GetAddressOf());
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return {};
	}

	// 読み込んだファイルの内容をDXCへ渡す
	DxcBuffer shaderSourceBuffer{};
	shaderSourceBuffer.Ptr = shaderSource->GetBufferPointer();
	shaderSourceBuffer.Size = shaderSource->GetBufferSize();
	shaderSourceBuffer.Encoding = DXC_CP_UTF8;

	// Compileに使う設定
	LPCWSTR arguments[] = {
		filePath.c_str(),	// Compile対象のhlslファイル名
		L"-E", L"main",		// EntryPoint。基本的にmain
		L"-T", profile,		// ShaderProfile
		L"-Zi", L"-Qembed_debug",	// Debug情報を埋め込む
		L"-Od",				// 最適化を外しておく
		L"-Zpr",			// メモリレイアウトは行優先
	};

	// 実際にShaderをCompileする
	Microsoft::WRL::ComPtr<IDxcResult> shaderResult;
	hr = dxcCompiler->Compile(
		&shaderSourceBuffer,
		arguments,
		_countof(arguments),
		includeHandler.Get(),
		IID_PPV_ARGS(&shaderResult)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return {};
	}

	// 警告又はエラーが出ていないか確認する
	Microsoft::WRL::ComPtr<IDxcBlobUtf8> shaderError;
	hr = shaderResult->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&shaderError), nullptr);
	assert(SUCCEEDED(hr));

	if (shaderError != nullptr && shaderError->GetStringLength() != 0) {
		Log(shaderError->GetStringPointer());

		// Shaderの警告又はエラーは必ず直す
		assert(false);
		return {};
	}

	// Compile自体が成功しているか確認する
	HRESULT compileStatus = S_OK;
	hr = shaderResult->GetStatus(&compileStatus);
	assert(SUCCEEDED(hr));
	assert(SUCCEEDED(compileStatus));

	if (FAILED(hr) || FAILED(compileStatus)) {
		return {};
	}

	// Compile結果から実行用のバイナリ部分を取得する
	Microsoft::WRL::ComPtr<IDxcBlob> shaderBlob;
	hr = shaderResult->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&shaderBlob), nullptr);
	assert(SUCCEEDED(hr));
	assert(shaderBlob != nullptr);

	if (FAILED(hr) || shaderBlob == nullptr) {
		return {};
	}

	// 成功したらログを出す
	Log(std::format(L"Compile Succeeded, path:{}, profile:{}\n", filePath, profile));

	// 実行用のバイナリを返す
	return shaderBlob;
}

bool Object3dRenderer::CreateGraphicsPipelineState() {
	// DXCを初期化する
	Microsoft::WRL::ComPtr<IDxcUtils> dxcUtils;
	Microsoft::WRL::ComPtr<IDxcCompiler3> dxcCompiler;
	Microsoft::WRL::ComPtr<IDxcIncludeHandler> includeHandler;

	HRESULT hr = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&dxcUtils));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	hr = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&dxcCompiler));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	hr = dxcUtils->CreateDefaultIncludeHandler(&includeHandler);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// Shaderをコンパイルする
	Microsoft::WRL::ComPtr<IDxcBlob> vertexShaderBlob = CompileShader(
		L"Shaders/Object3d.VS.hlsl",
		L"vs_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);
	assert(vertexShaderBlob != nullptr);

	if (vertexShaderBlob == nullptr) {
		return false;
	}

	Microsoft::WRL::ComPtr<IDxcBlob> pixelShaderBlob = CompileShader(
		L"Shaders/Object3d.PS.hlsl",
		L"ps_6_0",
		dxcUtils,
		dxcCompiler,
		includeHandler
	);
	assert(pixelShaderBlob != nullptr);

	if (pixelShaderBlob == nullptr) {
		return false;
	}

	// RootSignature作成
	D3D12_ROOT_SIGNATURE_DESC descriptionRootSignature{};
	descriptionRootSignature.Flags =
		D3D12_ROOT_SIGNATURE_FLAG_ALLOW_INPUT_ASSEMBLER_INPUT_LAYOUT;

	// Texture用のDescriptorRangeを作成する
	D3D12_DESCRIPTOR_RANGE descriptorRange[1] = {};
	descriptorRange[0].BaseShaderRegister = 0;
	descriptorRange[0].NumDescriptors = 1;
	descriptorRange[0].RangeType = D3D12_DESCRIPTOR_RANGE_TYPE_SRV;
	descriptorRange[0].OffsetInDescriptorsFromTableStart =
		D3D12_DESCRIPTOR_RANGE_OFFSET_APPEND;

	// RootParameter作成。PixelShaderのMaterial、VertexShaderのTransform、PixelShaderのTexture
	D3D12_ROOT_PARAMETER rootParameters[4] = {};

	rootParameters[0].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[0].Descriptor.ShaderRegister = 0;

	rootParameters[1].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[1].ShaderVisibility = D3D12_SHADER_VISIBILITY_VERTEX;
	rootParameters[1].Descriptor.ShaderRegister = 0;

	rootParameters[2].ParameterType = D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;
	rootParameters[2].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[2].DescriptorTable.pDescriptorRanges = descriptorRange;
	rootParameters[2].DescriptorTable.NumDescriptorRanges = _countof(descriptorRange);

	rootParameters[3].ParameterType = D3D12_ROOT_PARAMETER_TYPE_CBV;
	rootParameters[3].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;
	rootParameters[3].Descriptor.ShaderRegister = 1;

	descriptionRootSignature.pParameters = rootParameters;
	descriptionRootSignature.NumParameters = _countof(rootParameters);

	// Samplerの設定
	D3D12_STATIC_SAMPLER_DESC staticSamplers[1] = {};
	staticSamplers[0].Filter = D3D12_FILTER_MIN_MAG_MIP_LINEAR;
	staticSamplers[0].AddressU = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressV = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].AddressW = D3D12_TEXTURE_ADDRESS_MODE_WRAP;
	staticSamplers[0].ComparisonFunc = D3D12_COMPARISON_FUNC_NEVER;
	staticSamplers[0].MaxLOD = D3D12_FLOAT32_MAX;
	staticSamplers[0].ShaderRegister = 0;
	staticSamplers[0].ShaderVisibility = D3D12_SHADER_VISIBILITY_PIXEL;

	descriptionRootSignature.pStaticSamplers = staticSamplers;
	descriptionRootSignature.NumStaticSamplers = _countof(staticSamplers);

	// シリアライズしてバイナリにする
	Microsoft::WRL::ComPtr<ID3DBlob> signatureBlob;
	Microsoft::WRL::ComPtr<ID3DBlob> errorBlob;

	hr = D3D12SerializeRootSignature(
		&descriptionRootSignature,
		D3D_ROOT_SIGNATURE_VERSION_1,
		signatureBlob.GetAddressOf(),
		errorBlob.GetAddressOf()
	);

	if (FAILED(hr)) {
		if (errorBlob.Get() != nullptr) {
			Log(reinterpret_cast<char*>(errorBlob->GetBufferPointer()));
		}

		assert(false);
		return false;
	}

	// バイナリを元に生成
	hr = device_->CreateRootSignature(
		0,
		signatureBlob->GetBufferPointer(),
		signatureBlob->GetBufferSize(),
		IID_PPV_ARGS(&rootSignature_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// InputLayoutの設定
	D3D12_INPUT_ELEMENT_DESC inputElementDescs[3] = {};

	inputElementDescs[0].SemanticName = "POSITION";
	inputElementDescs[0].SemanticIndex = 0;
	inputElementDescs[0].Format = DXGI_FORMAT_R32G32B32A32_FLOAT;
	inputElementDescs[0].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[1].SemanticName = "TEXCOORD";
	inputElementDescs[1].SemanticIndex = 0;
	inputElementDescs[1].Format = DXGI_FORMAT_R32G32_FLOAT;
	inputElementDescs[1].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	inputElementDescs[2].SemanticName = "NORMAL";
	inputElementDescs[2].SemanticIndex = 0;
	inputElementDescs[2].Format = DXGI_FORMAT_R32G32B32_FLOAT;
	inputElementDescs[2].AlignedByteOffset = D3D12_APPEND_ALIGNED_ELEMENT;

	D3D12_INPUT_LAYOUT_DESC inputLayoutDesc{};
	inputLayoutDesc.pInputElementDescs = inputElementDescs;
	inputLayoutDesc.NumElements = _countof(inputElementDescs);

	// BlendStateの設定
	D3D12_BLEND_DESC blendDesc{};

	// すべての色要素を書き込む
	blendDesc.RenderTarget[0].RenderTargetWriteMask =
		D3D12_COLOR_WRITE_ENABLE_ALL;

	// RasterizerStateの設定
	D3D12_RASTERIZER_DESC rasterizerDesc{};

	// 裏面を表示しない
	rasterizerDesc.CullMode = D3D12_CULL_MODE_BACK;

	// OBJを左手座標系に変換して逆順で積んだ面を表として扱う
	rasterizerDesc.FrontCounterClockwise = true;

	// 三角形の中を塗りつぶす
	rasterizerDesc.FillMode = D3D12_FILL_MODE_SOLID;

	// DepthStencilStateの設定
	D3D12_DEPTH_STENCIL_DESC depthStencilDesc{};

	// Depthの機能を有効にする
	depthStencilDesc.DepthEnable = true;

	// 書き込みします
	depthStencilDesc.DepthWriteMask = D3D12_DEPTH_WRITE_MASK_ALL;

	// 比較関数はLessEqual。つまり、近ければ描画される
	depthStencilDesc.DepthFunc = D3D12_COMPARISON_FUNC_LESS_EQUAL;

	// PSOを生成する
	D3D12_GRAPHICS_PIPELINE_STATE_DESC graphicsPipelineStateDesc{};
	graphicsPipelineStateDesc.pRootSignature = rootSignature_.Get();
	graphicsPipelineStateDesc.InputLayout = inputLayoutDesc;
	graphicsPipelineStateDesc.VS = {
		vertexShaderBlob->GetBufferPointer(),
		vertexShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.PS = {
		pixelShaderBlob->GetBufferPointer(),
		pixelShaderBlob->GetBufferSize()
	};
	graphicsPipelineStateDesc.BlendState = blendDesc;
	graphicsPipelineStateDesc.RasterizerState = rasterizerDesc;

	// DepthStencilをPSOへ設定する
	graphicsPipelineStateDesc.DepthStencilState = depthStencilDesc;
	graphicsPipelineStateDesc.DSVFormat = DXGI_FORMAT_D24_UNORM_S8_UINT;

	// 書き込むRTVの情報
	graphicsPipelineStateDesc.NumRenderTargets = 1;
	graphicsPipelineStateDesc.RTVFormats[0] = DXGI_FORMAT_R8G8B8A8_UNORM_SRGB;

	// 利用するトポロジのタイプ。今回は三角形
	graphicsPipelineStateDesc.PrimitiveTopologyType =
		D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;

	// どのように画面に色を打ち込むかの設定
	graphicsPipelineStateDesc.SampleDesc.Count = 1;
	graphicsPipelineStateDesc.SampleMask = D3D12_DEFAULT_SAMPLE_MASK;

	// 実際に生成
	hr = device_->CreateGraphicsPipelineState(
		&graphicsPipelineStateDesc,
		IID_PPV_ARGS(&graphicsPipelineState_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	Log("Complete create GraphicsPipelineState!!!\n");

	return true;
}


Microsoft::WRL::ComPtr<ID3D12Resource> Object3dRenderer::CreateBufferResource(size_t sizeInBytes) {
	// Buffer用のResourceを作成するためのHeap設定
	D3D12_HEAP_PROPERTIES uploadHeapProperties{};
	uploadHeapProperties.Type = D3D12_HEAP_TYPE_UPLOAD;

	// Buffer用のResource設定
	D3D12_RESOURCE_DESC bufferResourceDesc{};
	bufferResourceDesc.Dimension = D3D12_RESOURCE_DIMENSION_BUFFER;
	bufferResourceDesc.Width = sizeInBytes;
	bufferResourceDesc.Height = 1;
	bufferResourceDesc.DepthOrArraySize = 1;
	bufferResourceDesc.MipLevels = 1;
	bufferResourceDesc.SampleDesc.Count = 1;
	bufferResourceDesc.Layout = D3D12_TEXTURE_LAYOUT_ROW_MAJOR;

	// 実際にBufferResourceを作成する
	Microsoft::WRL::ComPtr<ID3D12Resource> bufferResource;
	HRESULT hr = device_->CreateCommittedResource(
		&uploadHeapProperties,
		D3D12_HEAP_FLAG_NONE,
		&bufferResourceDesc,
		D3D12_RESOURCE_STATE_GENERIC_READ,
		nullptr,
		IID_PPV_ARGS(&bufferResource)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return {};
	}

	return bufferResource;
}

bool Object3dRenderer::CreateVertexResource() {
	assert(!modelData_.vertices.empty());

	if (modelData_.vertices.empty()) {
		return false;
	}

	// Model用の頂点Resourceを作成する
	const size_t vertexBufferSize = sizeof(VertexData) * modelData_.vertices.size();
	vertexResource_ = CreateBufferResource(vertexBufferSize);
	assert(vertexResource_ != nullptr);

	if (vertexResource_ == nullptr) {
		return false;
	}

	// Model用の頂点ResourceにOBJから読んだデータを書き込む
	VertexData* vertexData = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = vertexResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&vertexData)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	std::memcpy(vertexData, modelData_.vertices.data(), vertexBufferSize);

	// 頂点バッファビューを作成する
	vertexBufferView_.BufferLocation = vertexResource_->GetGPUVirtualAddress();
	vertexBufferView_.SizeInBytes = static_cast<UINT>(vertexBufferSize);
	vertexBufferView_.StrideInBytes = sizeof(VertexData);

	Log("Complete create Model VertexResource!!!\n");

	return true;
}

bool Object3dRenderer::CreateMaterialResource() {
	// マテリアル用のResourceを作成する
	materialResource_ = CreateBufferResource(sizeof(Material));
	assert(materialResource_ != nullptr);

	if (materialResource_ == nullptr) {
		return false;
	}

	// マテリアルResourceにデータを書き込む
	materialData_ = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = materialResource_->Map(0, nullptr, reinterpret_cast<void**>(&materialData_));
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// Textureの色をそのまま見やすくするため白色を設定する
	materialData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };

	// ModelはLightingする
	materialData_->enableLighting = true;
	materialData_->padding[0] = 0.0f;
	materialData_->padding[1] = 0.0f;
	materialData_->padding[2] = 0.0f;

	// 最初は単位行列を設定する
	materialData_->uvTransform = MakeIdentity4x4();

	Log("Complete create MaterialResource!!!\n");

	return true;
}

bool Object3dRenderer::CreateDirectionalLightResource() {
	// 平行光源用のResourceを作成する
	directionalLightResource_ = CreateBufferResource(sizeof(DirectionalLight));
	assert(directionalLightResource_ != nullptr);

	if (directionalLightResource_ == nullptr) {
		return false;
	}

	// 平行光源Resourceにデータを書き込む
	directionalLightData_ = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = directionalLightResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&directionalLightData_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// とりあえず白いライトで上から下へ照らす
	directionalLightData_->color = { 1.0f, 1.0f, 1.0f, 1.0f };
	directionalLightData_->direction = Normalize({ 0.0f, -1.0f, 0.0f });
	directionalLightData_->intensity = 1.0f;

	Log("Complete create DirectionalLightResource!!!\n");

	return true;
}

bool Object3dRenderer::CreateTransformationMatrixResource() {
	// 座標変換用のResourceを作成する
	transformationMatrixResource_ = CreateBufferResource(sizeof(TransformationMatrix));
	assert(transformationMatrixResource_ != nullptr);

	if (transformationMatrixResource_ == nullptr) {
		return false;
	}

	// 座標変換Resourceにデータを書き込む
	transformationMatrixData_ = nullptr;

	// 書き込み用のアドレスを取得する
	HRESULT hr = transformationMatrixResource_->Map(
		0,
		nullptr,
		reinterpret_cast<void**>(&transformationMatrixData_)
	);
	assert(SUCCEEDED(hr));

	if (FAILED(hr)) {
		return false;
	}

	// 最初は単位行列を設定する
	transformationMatrixData_->WVP = MakeIdentity4x4();
	transformationMatrixData_->World = MakeIdentity4x4();

	Log("Complete create TransformationMatrixResource!!!\n");

	return true;
}

void Object3dRenderer::UpdateTransformationMatrix() {
	if (transformationMatrixData_ == nullptr) {
		return;
	}

	// オブジェクトのWorldMatrixを作成する
	Matrix4x4 worldMatrix = MakeAffineMatrix(
		transform_.scale,
		transform_.rotate,
		transform_.translate
	);

	// カメラのViewMatrixを作成する
	Matrix4x4 viewMatrix =
		useDebugCamera_ ? debugCamera_.GetViewMatrix() : CreateDefaultViewMatrix();

	// 透視投影行列を作成する
	Matrix4x4 projectionMatrix =
		useDebugCamera_ ? debugCamera_.GetProjectionMatrix() : CreateProjectionMatrix();

	// World、View、Projectionをまとめる
	Matrix4x4 worldViewProjectionMatrix = Multiply(
		worldMatrix,
		Multiply(viewMatrix, projectionMatrix)
	);

	// VertexShaderへ渡す行列を更新する
	transformationMatrixData_->WVP = worldViewProjectionMatrix;
	transformationMatrixData_->World = worldMatrix;
}

Matrix4x4 Object3dRenderer::CreateDefaultViewMatrix() const {
	// 通常カメラのViewMatrixを作成する
	Matrix4x4 cameraMatrix = MakeAffineMatrix(
		cameraTransform_.scale,
		cameraTransform_.rotate,
		cameraTransform_.translate
	);

	return Inverse(cameraMatrix);
}

Matrix4x4 Object3dRenderer::CreateProjectionMatrix() const {
	// 透視投影行列を作成する
	return MakePerspectiveFovMatrix(
		0.45f,
		float(WinConfig::kClientWidth) / float(WinConfig::kClientHeight),
		0.1f,
		100.0f
	);
}

