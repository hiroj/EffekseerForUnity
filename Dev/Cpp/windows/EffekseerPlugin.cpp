﻿#pragma warning(disable : 4005)

#include <assert.h>
#include <shlwapi.h>
#include <windows.h>

#include "Effekseer.h"

#include "EffekseerRendererDX11.h"
#include "EffekseerRendererDX9.h"
#include "EffekseerRendererGL.h"

#include "../common/EffekseerPluginCommon.h"
#include "../unity/IUnityGraphics.h"
#include "../unity/IUnityGraphicsD3D11.h"
#include "../unity/IUnityGraphicsD3D9.h"

#include "../opengl/EffekseerPluginGL.h"

#include "../common/EffekseerPluginModel.h"
#include "../common/EffekseerPluginTexture.h"
#include "../graphicsAPI/EffekseerPluginGraphics.h"
#include "../renderer/EffekseerRendererImplemented.h"

#ifdef _WIN32
#pragma comment(lib, "shlwapi.lib")
#endif

namespace EffekseerPlugin
{
int32_t g_maxInstances = 0;
int32_t g_maxSquares = 0;
RendererType g_rendererType = RendererType::Native;

bool g_reversedDepth = false;
bool g_isRightHandedCoordinate = false;

IUnityInterfaces* g_UnityInterfaces = NULL;
IUnityGraphics* g_UnityGraphics = NULL;
UnityGfxRenderer g_UnityRendererType = kUnityGfxRendererNull;
Graphics* g_graphics = nullptr;

Effekseer::Manager* g_EffekseerManager = NULL;
EffekseerRenderer::Renderer* g_EffekseerRenderer = NULL;

bool g_isInitialized = false;

void InitRenderer()
{
	if (g_EffekseerManager == nullptr)
		return;

	if (g_rendererType == RendererType::Native)
	{
		g_graphics = Graphics::Create(g_UnityRendererType, false, true);
		g_graphics->Initialize(g_UnityInterfaces);

		g_EffekseerRenderer = g_graphics->CreateRenderer(g_maxSquares, g_reversedDepth);

		g_EffekseerRenderer->SetTextureUVStyle(EffekseerRenderer::UVStyle::VerticalFlipped);
		g_EffekseerRenderer->SetBackgroundTextureUVStyle(EffekseerRenderer::UVStyle::VerticalFlipped);
	}
	else if (g_rendererType == RendererType::Unity)
	{
		g_graphics = Graphics::Create(g_UnityRendererType, true, true);
		g_graphics->Initialize(g_UnityInterfaces);

		g_EffekseerRenderer = g_graphics->CreateRenderer(g_maxSquares, g_reversedDepth);

		auto renderer = EffekseerRendererUnity::RendererImplemented::Create();
		if (renderer->Initialize(g_maxSquares))
		{
			g_EffekseerRenderer = renderer;
		}
		else
		{
			ES_SAFE_RELEASE(renderer);
			ES_SAFE_DELETE(g_graphics);
		}
	}

	if (g_EffekseerRenderer == nullptr)
	{
		return;
	}

	g_EffekseerManager->SetSpriteRenderer(g_EffekseerRenderer->CreateSpriteRenderer());
	g_EffekseerManager->SetRibbonRenderer(g_EffekseerRenderer->CreateRibbonRenderer());
	g_EffekseerManager->SetRingRenderer(g_EffekseerRenderer->CreateRingRenderer());
	g_EffekseerManager->SetTrackRenderer(g_EffekseerRenderer->CreateTrackRenderer());
	g_EffekseerManager->SetModelRenderer(g_EffekseerRenderer->CreateModelRenderer());
}

void TermRenderer()
{
#ifdef _WIN32
	for (int i = 0; i < MAX_RENDER_PATH; i++)
	{
		if (g_UnityRendererType == kUnityGfxRendererD3D11)
		{
			if (renderSettings[i].backgroundTexture)
			{
				((ID3D11ShaderResourceView*)renderSettings[i].backgroundTexture)->Release();
			}
		}
		renderSettings[i].backgroundTexture = nullptr;
	}
#endif

	if (g_EffekseerRenderer != NULL)
	{
		g_EffekseerRenderer->Destroy();
		g_EffekseerRenderer = NULL;
	}

	if (g_graphics != nullptr)
	{
		g_graphics->Shutdown(g_UnityInterfaces);
	}

	ES_SAFE_DELETE(g_graphics);
}

void SetBackGroundTexture(void* backgroundTexture)
{
	assert(g_graphics != nullptr);
	g_graphics->SetBackGroundTextureToRenderer(g_EffekseerRenderer, backgroundTexture);
}

UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API OnGraphicsDeviceEvent(UnityGfxDeviceEventType eventType)
{
	switch (eventType)
	{
	case kUnityGfxDeviceEventInitialize:
		g_UnityRendererType = g_UnityGraphics->GetRenderer();
		break;
	case kUnityGfxDeviceEventShutdown:
		TermRenderer();
		g_UnityRendererType = kUnityGfxRendererNull;
		break;
	case kUnityGfxDeviceEventBeforeReset:
		if (g_graphics != nullptr)
			g_graphics->BeforeReset(g_UnityInterfaces);
		break;
	case kUnityGfxDeviceEventAfterReset:
		if (g_graphics != nullptr)
			g_graphics->AfterReset(g_UnityInterfaces);
		break;
	}
}
} // namespace EffekseerPlugin

using namespace EffekseerPlugin;

extern "C"
{
	// Unity plugin load event
	UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginLoad(IUnityInterfaces* unityInterfaces)
	{
		g_UnityInterfaces = unityInterfaces;
		g_UnityGraphics = g_UnityInterfaces->Get<IUnityGraphics>();
		g_UnityRendererType = g_UnityGraphics->GetRenderer();

		g_UnityGraphics->RegisterDeviceEventCallback(OnGraphicsDeviceEvent);

		// Run OnGraphicsDeviceEvent(initialize) manually on plugin load
		// to not miss the event in case the graphics device is already initialized
		OnGraphicsDeviceEvent(kUnityGfxDeviceEventInitialize);
	}

	// Unity plugin unload event
	UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API UnityPluginUnload()
	{
		g_UnityGraphics->UnregisterDeviceEventCallback(OnGraphicsDeviceEvent);
	}

	UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API EffekseerRender(int renderId)
	{
		if (g_isInitialized == false)
		{
			if (g_EffekseerRenderer != nullptr)
			{
				// 遅延終了処理
				TermRenderer();
			}
			return;
		}
		else
		{
			if (g_EffekseerRenderer == nullptr)
			{
				// 遅延初期化処理
				InitRenderer();
			}
		}

		if (g_EffekseerManager == nullptr)
			return;
		if (g_EffekseerRenderer == nullptr)
			return;

		RenderSettings& settings = renderSettings[renderId];
		Effekseer::Matrix44 projectionMatrix, cameraMatrix;

		if (settings.stereoEnabled)
		{
			if (settings.stereoRenderCount == 0)
			{
				projectionMatrix = settings.leftProjectionMatrix;
				cameraMatrix = settings.leftCameraMatrix;
			}
			else if (settings.stereoRenderCount == 1)
			{
				projectionMatrix = settings.rightProjectionMatrix;
				cameraMatrix = settings.rightCameraMatrix;
			}
			settings.stereoRenderCount++;
		}
		else
		{
			projectionMatrix = settings.projectionMatrix;
			cameraMatrix = settings.cameraMatrix;
		}

		if (settings.renderIntoTexture)
		{
			// テクスチャに対してレンダリングするときは上下反転させる
			projectionMatrix.Values[1][1] = -projectionMatrix.Values[1][1];
		}

		// 行列をセット
		g_EffekseerRenderer->SetProjectionMatrix(projectionMatrix);
		g_EffekseerRenderer->SetCameraMatrix(cameraMatrix);

		// convert a right hand into a left hand
		::Effekseer::Vector3D cameraPosition;
		::Effekseer::Vector3D cameraFrontDirection;
		CalculateCameraDirectionAndPosition(cameraMatrix, cameraFrontDirection, cameraPosition);

		// if (!g_isRightHandedCoordinate)
		{
			cameraFrontDirection = -cameraFrontDirection;
			// cameraPosition.Z = -cameraPosition.Z;
		}

		g_EffekseerRenderer->SetCameraParameter(cameraFrontDirection, cameraPosition);

		// 背景テクスチャをセット
		SetBackGroundTexture(settings.backgroundTexture);

		// 描画実行(全体)
		g_EffekseerRenderer->BeginRendering();
		g_EffekseerManager->Draw();
		g_EffekseerRenderer->EndRendering();

		// 背景テクスチャを解除
		SetBackGroundTexture(nullptr);
	}

	UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API EffekseerRenderFront(int renderId)
	{
		if (g_EffekseerManager == nullptr)
			return;
		if (g_EffekseerRenderer == nullptr)
			return;

		// Need not to assgin matrixes. Because these were assigned in EffekseerRenderBack

		g_EffekseerRenderer->BeginRendering();
		g_EffekseerManager->DrawFront();
		g_EffekseerRenderer->EndRendering();

		// 背景テクスチャを解除
		SetBackGroundTexture(nullptr);
	}

	UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API EffekseerRenderBack(int renderId)
	{
		if (g_isInitialized == false)
		{
			if (g_EffekseerRenderer != nullptr)
			{
				// 遅延終了処理
				TermRenderer();
			}
			return;
		}
		else
		{
			if (g_EffekseerRenderer == nullptr)
			{
				// 遅延初期化処理
				InitRenderer();
			}
		}

		if (g_EffekseerManager == nullptr)
			return;
		if (g_EffekseerRenderer == nullptr)
			return;

		RenderSettings& settings = renderSettings[renderId];
		Effekseer::Matrix44 projectionMatrix, cameraMatrix;

		if (settings.stereoEnabled)
		{
			if (settings.stereoRenderCount == 0)
			{
				projectionMatrix = settings.leftProjectionMatrix;
				cameraMatrix = settings.leftCameraMatrix;
			}
			else if (settings.stereoRenderCount == 1)
			{
				projectionMatrix = settings.rightProjectionMatrix;
				cameraMatrix = settings.rightCameraMatrix;
			}
			settings.stereoRenderCount++;
		}
		else
		{
			projectionMatrix = settings.projectionMatrix;
			cameraMatrix = settings.cameraMatrix;
		}

		if (settings.renderIntoTexture)
		{
			// テクスチャに対してレンダリングするときは上下反転させる
			projectionMatrix.Values[1][1] = -projectionMatrix.Values[1][1];
		}

		// 行列をセット
		g_EffekseerRenderer->SetProjectionMatrix(projectionMatrix);
		g_EffekseerRenderer->SetCameraMatrix(cameraMatrix);

		// convert a right hand into a left hand
		::Effekseer::Vector3D cameraPosition;
		::Effekseer::Vector3D cameraFrontDirection;
		CalculateCameraDirectionAndPosition(cameraMatrix, cameraFrontDirection, cameraPosition);

		// if (!g_isRightHandedCoordinate)
		{
			cameraFrontDirection = -cameraFrontDirection;
		}

		g_EffekseerRenderer->SetCameraParameter(cameraFrontDirection, cameraPosition);

		// 背景テクスチャをセット
		SetBackGroundTexture(settings.backgroundTexture);

		// 描画実行(全体)
		g_EffekseerRenderer->BeginRendering();
		g_EffekseerManager->DrawBack();
		g_EffekseerRenderer->EndRendering();
	}

	UNITY_INTERFACE_EXPORT UnityRenderingEvent UNITY_INTERFACE_API EffekseerGetRenderFunc(int renderId) { return EffekseerRender; }

	UNITY_INTERFACE_EXPORT UnityRenderingEvent UNITY_INTERFACE_API EffekseerGetRenderFrontFunc(int renderId)
	{
		return EffekseerRenderFront;
	}

	UNITY_INTERFACE_EXPORT UnityRenderingEvent UNITY_INTERFACE_API EffekseerGetRenderBackFunc(int renderId) { return EffekseerRenderBack; }

	UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API
	EffekseerInit(int maxInstances, int maxSquares, int reversedDepth, int isRightHandedCoordinate, int rendererType)
	{
		g_isInitialized = true;

		g_maxInstances = maxInstances;
		g_maxSquares = maxSquares;
		g_reversedDepth = reversedDepth != 0;
		g_isRightHandedCoordinate = isRightHandedCoordinate != 0;
		g_rendererType = (RendererType)rendererType;

		g_EffekseerManager = Effekseer::Manager::Create(maxInstances);

		if (g_isRightHandedCoordinate)
		{
			g_EffekseerManager->SetCoordinateSystem(Effekseer::CoordinateSystem::RH);
		}
		else
		{
			g_EffekseerManager->SetCoordinateSystem(Effekseer::CoordinateSystem::LH);
		}

		InitRenderer();
	}

	UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API EffekseerTerm()
	{
		if (g_EffekseerManager != NULL)
		{
			g_EffekseerManager->Destroy();
			g_EffekseerManager = NULL;
		}

		TermRenderer();

		g_isInitialized = false;
	}

	// 歪み用テクスチャ設定
	UNITY_INTERFACE_EXPORT void UNITY_INTERFACE_API EffekseerSetBackGroundTexture(int renderId, void* texture)
	{
		if (g_graphics != nullptr)
		{
			g_graphics->EffekseerSetBackGroundTexture(renderId, texture);
		}
	}

	Effekseer::TextureLoader* TextureLoader::Create(TextureLoaderLoad load, TextureLoaderUnload unload)
	{
		assert(g_graphics != nullptr);
		return g_graphics->Create(load, unload);
	}

	Effekseer::ModelLoader* ModelLoader::Create(ModelLoaderLoad load, ModelLoaderUnload unload)
	{
		assert(g_graphics != nullptr);
		return g_graphics->Create(load, unload);
	}
}

#ifdef _WIN32

BOOL WINAPI DllMain(HINSTANCE hinstDLL, DWORD fdwReason, LPVOID lpvReserved)
{
	bool res = true;
	switch (fdwReason)
	{
	case DLL_PROCESS_ATTACH:
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		break;
	case DLL_PROCESS_DETACH:
		CoUninitialize();
		break;
	case DLL_THREAD_ATTACH:
		CoInitializeEx(NULL, COINIT_MULTITHREADED);
		break;
	case DLL_THREAD_DETACH:
		CoUninitialize();
		break;
	default:
		break;
	}
	return res;
}

#endif
