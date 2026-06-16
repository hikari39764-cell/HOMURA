#pragma once

#include <dxgidebug.h>
#include <wrl.h>

#include "Logger.h"

struct D3DResourceLeakChecker {
	~D3DResourceLeakChecker() {
#ifdef _DEBUG
		// ComPtrなどの解放が終わった後にDirectX関連の生存オブジェクトを確認する
		Microsoft::WRL::ComPtr<IDXGIDebug1> debug;
		HRESULT hr = DXGIGetDebugInterface1(0, IID_PPV_ARGS(&debug));
		if (SUCCEEDED(hr)) {
			Log("========== DXGI Live Object Report Start!!!!!!!!!!!!!!!!!!!!!!!!!! ==========\n");

			// DXGI全体の生存オブジェクトを出力する
			debug->ReportLiveObjects(DXGI_DEBUG_ALL, DXGI_DEBUG_RLO_ALL);

			// アプリケーション側で作成したDXGI関連オブジェクトを出力する
			debug->ReportLiveObjects(DXGI_DEBUG_APP, DXGI_DEBUG_RLO_ALL);

			// D3D12関連の生存オブジェクトを出力する
			debug->ReportLiveObjects(DXGI_DEBUG_D3D12, DXGI_DEBUG_RLO_ALL);

			Log("========== DXGI Live Object Report End !!!!!!!!!!!!!!!!!!!!!==========\n");
		}
#endif
	}
};
