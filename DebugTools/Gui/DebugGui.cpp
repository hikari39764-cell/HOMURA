#include "DebugGui.h"

#include <cassert>

#ifdef USE_IMGUI
#include "externals/imgui/imgui.h"
#include "externals/imgui/imgui_impl_dx12.h"
#include "externals/imgui/imgui_impl_win32.h"
#endif

namespace Homura {

bool DebugGui::Initialize(
	HWND hwnd,
	ID3D12Device* device,
	int bufferCount,
	DXGI_FORMAT rtvFormat,
	ID3D12DescriptorHeap* srvDescriptorHeap,
	D3D12_CPU_DESCRIPTOR_HANDLE fontSrvHandleCPU,
	D3D12_GPU_DESCRIPTOR_HANDLE fontSrvHandleGPU
) {
#ifdef USE_IMGUI
	assert(hwnd != nullptr);
	assert(device != nullptr);
	assert(srvDescriptorHeap != nullptr);

	if (hwnd == nullptr || device == nullptr || srvDescriptorHeap == nullptr) {
		return false;
	}

	// ImGuiの初期化。開発用UIなのでReleaseでは呼ばれない
	IMGUI_CHECKVERSION();
	ImGui::CreateContext();
	ImGui::StyleColorsDark();

	bool win32Initialized = ImGui_ImplWin32_Init(hwnd);
	assert(win32Initialized);

	if (!win32Initialized) {
		ImGui::DestroyContext();
		return false;
	}

	bool dx12Initialized = ImGui_ImplDX12_Init(
		device,
		bufferCount,
		rtvFormat,
		srvDescriptorHeap,
		fontSrvHandleCPU,
		fontSrvHandleGPU
	);
	assert(dx12Initialized);

	if (!dx12Initialized) {
		ImGui_ImplWin32_Shutdown();
		ImGui::DestroyContext();
		return false;
	}

	ImGuiIO& io = ImGui::GetIO();
	io.Fonts->Build();

	initialized_ = true;
#else
	(void)hwnd;
	(void)device;
	(void)bufferCount;
	(void)rtvFormat;
	(void)srvDescriptorHeap;
	(void)fontSrvHandleCPU;
	(void)fontSrvHandleGPU;
#endif

	return true;
}

void DebugGui::Finalize() {
#ifdef USE_IMGUI
	if (!initialized_) {
		return;
	}

	// ImGuiの終了処理。初期化と逆順に行う
	ImGui_ImplDX12_Shutdown();
	ImGui_ImplWin32_Shutdown();
	ImGui::DestroyContext();
#endif

	initialized_ = false;
}

void DebugGui::BeginFrame() {
#ifdef USE_IMGUI
	if (!initialized_) {
		return;
	}

	// ImGuiに新しいフレームが始まることを伝える
	ImGui_ImplDX12_NewFrame();
	ImGui_ImplWin32_NewFrame();
	ImGui::NewFrame();
#endif
}

void DebugGui::ShowDemoWindow() {
#ifdef USE_IMGUI
	if (!initialized_) {
		return;
	}

	// 開発用UI。実際に作るUIが増えたらここを置き換える
	ImGui::ShowDemoWindow();
#endif
}

void DebugGui::EndFrame() {
#ifdef USE_IMGUI
	if (!initialized_) {
		return;
	}

	// ImGuiの内部コマンドを生成する
	ImGui::Render();
#endif
}

void DebugGui::Render(ID3D12GraphicsCommandList* commandList) {
#ifdef USE_IMGUI
	if (!initialized_) {
		return;
	}

	assert(commandList != nullptr);

	if (commandList == nullptr) {
		return;
	}

	// 実際のCommandListにImGuiの描画コマンドを積む
	ImGui_ImplDX12_RenderDrawData(ImGui::GetDrawData(), commandList);
#else
	(void)commandList;
#endif
}

} // namespace Homura
