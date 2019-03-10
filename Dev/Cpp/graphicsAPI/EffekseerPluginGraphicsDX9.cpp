﻿
#include "EffekseerPluginGraphicsDX9.h"
#include "../unity/IUnityGraphicsD3D9.h"
#include <algorithm>
#include <assert.h>

namespace EffekseerPlugin
{

class TextureLoaderDX9 : public TextureLoader
{
	struct TextureResource
	{
		int referenceCount = 1;
		Effekseer::TextureData texture = {};
	};
	std::map<std::u16string, TextureResource> resources;
	std::map<void*, void*> textureData2NativePtr;

public:
	TextureLoaderDX9(TextureLoaderLoad load, TextureLoaderUnload unload) : TextureLoader(load, unload) {}
	virtual ~TextureLoaderDX9() {}
	virtual Effekseer::TextureData* Load(const EFK_CHAR* path, Effekseer::TextureType textureType)
	{
		// リソーステーブルを検索して存在したらそれを使う
		auto it = resources.find((const char16_t*)path);
		if (it != resources.end())
		{
			it->second.referenceCount++;
			return &it->second.texture;
		}

		// Unityでテクスチャをロード
		int32_t width, height, format;
		void* texturePtr = load((const char16_t*)path, &width, &height, &format);
		if (texturePtr == nullptr)
		{
			return nullptr;
		}

		// リソーステーブルに追加
		auto added = resources.insert(std::make_pair((const char16_t*)path, TextureResource()));
		TextureResource& res = added.first->second;

		res.texture.Width = width;
		res.texture.Height = height;
		res.texture.TextureFormat = (Effekseer::TextureFormatType)format;

		IDirect3DTexture9* textureDX9 = (IDirect3DTexture9*)texturePtr;
		res.texture.UserPtr = textureDX9;

		textureData2NativePtr[&res.texture] = texturePtr;

		return &res.texture;
	}
	virtual void Unload(Effekseer::TextureData* source)
	{
		if (source == nullptr)
		{
			return;
		}

		// アンロードするテクスチャを検索
		auto it = std::find_if(resources.begin(), resources.end(), [source](const std::pair<std::u16string, TextureResource>& pair) {
			return &pair.second.texture == source;
		});
		if (it == resources.end())
		{
			return;
		}

		// 参照カウンタが0になったら実際にアンロード
		it->second.referenceCount--;
		if (it->second.referenceCount <= 0)
		{
			// Unload from unity
			unload(it->first.c_str(), textureData2NativePtr[source]);
			textureData2NativePtr.erase(source);
			resources.erase(it);
		}
	}
};

GraphicsDX9::GraphicsDX9() {}

GraphicsDX9::~GraphicsDX9() { assert(d3d9Device == nullptr); }

bool GraphicsDX9::Initialize(IUnityInterfaces* unityInterfaces)
{
	d3d9Device = unityInterfaces->Get<IUnityGraphicsD3D9>()->GetDevice();
	return true;
}

void GraphicsDX9::Shutdown(IUnityInterfaces* unityInterface) {}

EffekseerRenderer::Renderer* GraphicsDX9::CreateRenderer(int squareMaxCount, bool reversedDepth)
{
	auto renderer = EffekseerRendererDX9::Renderer::Create(d3d9Device, squareMaxCount);
	return renderer;
}

void GraphicsDX9::SetBackGroundTextureToRenderer(EffekseerRenderer::Renderer* renderer, void* backgroundTexture)
{
	((EffekseerRendererDX9::Renderer*)renderer)->SetBackground((IDirect3DTexture9*)backgroundTexture);
}

void GraphicsDX9::EffekseerSetBackGroundTexture(int renderId, void* texture) { renderSettings[renderId].backgroundTexture = texture; }

Effekseer::TextureLoader* GraphicsDX9::Create(TextureLoaderLoad load, TextureLoaderUnload unload)
{
	return new TextureLoaderDX9(load, unload);
}

Effekseer::ModelLoader* GraphicsDX9::Create(ModelLoaderLoad load, ModelLoaderUnload unload)
{
	auto loader = (ModelLoader*)ModelLoader::Create(load, unload);
	auto internalLoader = EffekseerRendererDX9::CreateModelLoader(d3d9Device, loader->GetFileInterface());
	loader->SetInternalLoader(internalLoader);
	return loader;
}

} // namespace EffekseerPlugin