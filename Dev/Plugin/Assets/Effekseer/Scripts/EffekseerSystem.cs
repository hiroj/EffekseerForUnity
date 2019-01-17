using UnityEngine;
using UnityEngine.Rendering;
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;

#if UNITY_EDITOR
using UnityEditor;
#endif

namespace Effekseer
{
	using Internal;

	[Serializable]
	public class EffekseerSystem
	{
		/// <summary xml:lang="en">
		/// Plays the effect.
		/// </summary>
		/// <param name="effectAsset" xml:lang="en">Effect asset</param>
		/// <param name="location" xml:lang="en">Location in world space</param>
		/// <returns>Played effect instance</returns>
		/// <summary xml:lang="ja">
		/// エフェクトの再生
		/// </summary>
		/// <param name="effectAsset" xml:lang="ja">エフェクトアセット</param>
		/// <param name="location" xml:lang="ja">再生開始する位置</param>
		/// <returns>再生したエフェクトインスタンス</returns>
		public static EffekseerHandle PlayEffect(EffekseerEffectAsset effectAsset, Vector3 location)
		{
			if (Instance == null) {
				Debug.LogError("[Effekseer] System is not initialized.");
				return new EffekseerHandle(-1);
			}
			if (effectAsset == null) {
				Debug.LogError("[Effekseer] Specified effect is null.");
				return new EffekseerHandle(-1);
			}

			IntPtr nativeEffect;
			if (Instance.nativeEffects.TryGetValue(effectAsset.GetInstanceID(), out nativeEffect)) {
				int handle = Plugin.EffekseerPlayEffect(nativeEffect, location.x, location.y, location.z);
				return new EffekseerHandle(handle);
			}
			return new EffekseerHandle(-1);
		}

		/// <summary xml:lang="en">
		/// Stops all effects
		/// </summary>
		/// <summary xml:lang="ja">
		/// 全エフェクトの再生停止
		/// </summary>
		public static void StopAllEffects()
		{
			Plugin.EffekseerStopAllEffects();
		}

		/// <summary xml:lang="en">
		/// Pause or resume all effects
		/// </summary>
		/// <summary xml:lang="ja">
		/// 全エフェクトの一時停止、もしくは再開
		/// </summary>
		public static void SetPausedToAllEffects(bool paused)
		{
			Plugin.EffekseerSetPausedToAllEffects(paused);
		}
		
		#region Internal Implimentation
		
		
		// Singleton instance
		public static EffekseerSystem Instance { get; private set; }
		public static bool IsValid { get { return Instance != null && Instance.enabled; } }

		/// <summary>
		/// Don't touch it!!
		/// </summary>
		public bool enabled;

		public IEffekseerRenderer renderer { get; private set; }

		// Loaded native effects
		[SerializeField] List<EffekseerEffectAsset> loadedEffects = new List<EffekseerEffectAsset>();
		private Dictionary<int, IntPtr> nativeEffects = new Dictionary<int, IntPtr>();

#if UNITY_EDITOR
		// For hot reloading
		[SerializeField] private List<int> nativeEffectsKeys = new List<int>();
		[SerializeField] private List<string> nativeEffectsValues = new List<string>();
#endif

		// A AssetBundle that current loading
		private EffekseerEffectAsset effectAssetInLoading;

		private static Dictionary<IntPtr, Texture> cachedTextures = new Dictionary<IntPtr, Texture>();

		private static Dictionary<IntPtr, UnityRendererModel> cachedModels = new Dictionary<IntPtr, UnityRendererModel>();

		private void ReloadEffects()
		{
			foreach (var effectAsset in loadedEffects) {
				effectAssetInLoading = effectAsset;
				int id = effectAsset.GetInstanceID();
				IntPtr nativeEffect;
				if (nativeEffects.TryGetValue(id, out nativeEffect)) {
					Plugin.EffekseerReloadResources(nativeEffect);
				}
				effectAssetInLoading = null;
			}
		}

		/// <summary>
		/// Don't touch it!!
		/// </summary>
		public void LoadEffect(EffekseerEffectAsset effectAsset) {
			effectAssetInLoading = effectAsset;
			int id = effectAsset.GetInstanceID();
			IntPtr nativeEffect;
			if (!nativeEffects.TryGetValue(id, out nativeEffect)) {
				byte[] bytes = effectAsset.efkBytes;
				nativeEffect = Plugin.EffekseerLoadEffectOnMemory(bytes, bytes.Length);
				nativeEffects.Add(id, nativeEffect);
				loadedEffects.Add(effectAsset);
			}
			effectAssetInLoading = null;
		}

		internal void ReleaseEffect(EffekseerEffectAsset effectAsset) {
			int id = effectAsset.GetInstanceID();
			IntPtr nativeEffect;
			if (nativeEffects.TryGetValue(id, out nativeEffect)) {
				Plugin.EffekseerReleaseEffect(nativeEffect);
				nativeEffects.Remove(id);
				loadedEffects.Remove(effectAsset);
			}
		}

		/// <summary>
		/// Don't touch it!!
		/// </summary>
		public void InitPlugin() {
			//Debug.Log("EffekseerSystem.InitPlugin");
			if (Instance != null) {
				Debug.LogError("[Effekseer] EffekseerSystem instance is already found.");
			}
			Instance = this;
			
			var settings = EffekseerSettings.Instance;

			// サポート外グラフィックスAPIのチェック
			switch (SystemInfo.graphicsDeviceType) {
			case GraphicsDeviceType.Metal:
	#if UNITY_5_4_OR_NEWER
			case GraphicsDeviceType.Direct3D12:
	#elif UNITY_5_5_OR_NEWER
			case GraphicsDeviceType.Vulkan:
	#endif
				Debug.LogError("[Effekseer] Graphics API \"" + SystemInfo.graphicsDeviceType + "\" is not supported.");
				return;
			}

			// Zのnearとfarの反転対応
			bool reversedDepth = false;
	#if UNITY_5_5_OR_NEWER
			switch (SystemInfo.graphicsDeviceType) {
			case GraphicsDeviceType.Direct3D11:
			case GraphicsDeviceType.Direct3D12:
			case GraphicsDeviceType.Metal:
			case GraphicsDeviceType.PlayStation4:
	#if UNITY_2017_4_OR_NEWER
			case GraphicsDeviceType.Switch:
	#endif
				reversedDepth = true;
				break;
			}
	#endif

			// Initialize effekseer library
			Plugin.EffekseerInit(settings.effectInstances, settings.maxSquares, reversedDepth ? 1 : 0, settings.isRightEffekseerHandledCoordinateSystem ? 1 : 0, (int)settings.RendererType);
		}

		/// <summary>
		/// Don't touch it!!
		/// </summary>
		public void TermPlugin() {
			//Debug.Log("EffekseerSystem.TermPlugin");
			foreach (var effectAsset in EffekseerEffectAsset.enabledAssets) {
				ReleaseEffect(effectAsset);
			}
			nativeEffects.Clear();
			
	#if UNITY_EDITOR
			nativeEffectsKeys.Clear();
			nativeEffectsValues.Clear();
	#endif

			// Finalize Effekseer library
			Plugin.EffekseerTerm();
			// For a platform that is releasing in render thread
			GL.IssuePluginEvent(Plugin.EffekseerGetRenderFunc(), 0);
			
			Instance = null;
		}

		/// <summary>
		/// Don't touch it!!
		/// </summary>
		public void OnEnable() {
			if (Instance == null) {
				Instance = this;
			}

			var settings = EffekseerSettings.Instance;
			if (settings.RendererType == EffekseerRendererType.Native)
			{
				renderer = new EffekseerRendererNative();
			}
			else
			{
				renderer = new EffekseerRendererUnity();
			}

			renderer.SetVisible(true);

			// Enable all loading functions
			Plugin.EffekseerSetTextureLoaderEvent(
				TextureLoaderLoad, 
				TextureLoaderUnload);
			Plugin.EffekseerSetModelLoaderEvent(
				ModelLoaderLoad, 
				ModelLoaderUnload);
			Plugin.EffekseerSetSoundLoaderEvent(
				SoundLoaderLoad, 
				SoundLoaderUnload);
			
	#if UNITY_EDITOR
			for (int i = 0; i < nativeEffectsKeys.Count; i++) {
				IntPtr nativeEffect = new IntPtr((long)ulong.Parse(nativeEffectsValues[i]));
				nativeEffects.Add(nativeEffectsKeys[i], nativeEffect);
			}
			nativeEffectsKeys.Clear();
			nativeEffectsValues.Clear();
	#endif

			ReloadEffects();

			enabled = true;
		}

		/// <summary>
		/// Don't touch it!!
		/// </summary>
		public void OnDisable() {
			enabled = false;

	#if UNITY_EDITOR
			foreach (var pair in nativeEffects) {
				nativeEffectsKeys.Add(pair.Key);
				nativeEffectsValues.Add(pair.Value.ToString());
				Plugin.EffekseerUnloadResources(pair.Value);
			}
			nativeEffects.Clear();
	#endif
			renderer.CleanUp();
			renderer.SetVisible(false);
			renderer = null;
			
			// Disable all loading functions
			Plugin.EffekseerSetTextureLoaderEvent(null, null);
			Plugin.EffekseerSetModelLoaderEvent(null, null);
			Plugin.EffekseerSetSoundLoaderEvent(null, null);
		}
		
		internal void Update(float deltaTime) {
			float deltaFrames = Utility.TimeToFrames(deltaTime);
			int updateCount = Mathf.Max(1, Mathf.RoundToInt(deltaFrames));
			for (int i = 0; i < updateCount; i++) {
				Plugin.EffekseerUpdate(deltaFrames / updateCount);
			}
		}

		internal static Texture GetCachedTexture(IntPtr key)
		{
			if(cachedTextures.ContainsKey(key))
			{
				return cachedTextures[key];
			}
			return Texture2D.whiteTexture;
		}

		internal static UnityRendererModel GetCachedModel(IntPtr key)
		{
			if (cachedModels.ContainsKey(key))
			{
				return cachedModels[key];
			}
			return null;
		}

		[AOT.MonoPInvokeCallback(typeof(Plugin.EffekseerTextureLoaderLoad))]
		private static IntPtr TextureLoaderLoad(IntPtr path, out int width, out int height, out int format)
		{
			var pathstr = Marshal.PtrToStringUni(path);
			var asset = Instance.effectAssetInLoading;
			var res = asset.FindTexture(pathstr);
			var texture = (res != null) ? res.texture : null;

			if (texture != null)
			{
				width = texture.width;
				height = texture.height;
				switch (texture.format)
				{
					case TextureFormat.DXT1: format = 1; break;
					case TextureFormat.DXT5: format = 2; break;
					default: format = 0; break;
				}

				var ptr = texture.GetNativeTexturePtr();

				cachedTextures.Add(ptr, texture);

				return ptr;
			}
			width = 0;
			height = 0;
			format = 0;
			return IntPtr.Zero;
		}

		[AOT.MonoPInvokeCallback(typeof(Plugin.EffekseerTextureLoaderUnload))]
		private static void TextureLoaderUnload(IntPtr path, IntPtr nativePtr)
		{
			cachedTextures.Remove(nativePtr);
		}

		[AOT.MonoPInvokeCallback(typeof(Plugin.EffekseerModelLoaderLoad))]
		private static IntPtr ModelLoaderLoad(IntPtr path, IntPtr buffer, int bufferSize, ref int requiredBufferSize) {
			var pathstr = Marshal.PtrToStringUni(path);
			var asset = Instance.effectAssetInLoading;
			var res = asset.FindModel(pathstr);
			var model = (res != null) ? res.asset : null;

			if (model != null) {
				requiredBufferSize = model.bytes.Length;

				if (model.bytes.Length <= bufferSize) {
					Marshal.Copy(model.bytes, 0, buffer, model.bytes.Length);

					if(EffekseerSettings.Instance.RendererType == EffekseerRendererType.Unity)
					{
						var unityRendererModel = new UnityRendererModel();
						unityRendererModel.Initialize(model.bytes);
						cachedModels.Add(unityRendererModel.VertexBuffer.GetNativeBufferPtr(), unityRendererModel);
						return unityRendererModel.VertexBuffer.GetNativeBufferPtr();
					}

					return new IntPtr(1);
				}
			}

			return IntPtr.Zero;
		}

		[AOT.MonoPInvokeCallback(typeof(Plugin.EffekseerModelLoaderUnload))]
		private static void ModelLoaderUnload(IntPtr path, IntPtr modelPtr) {
			if (EffekseerSettings.Instance.RendererType == EffekseerRendererType.Unity)
			{
				cachedModels.Remove(modelPtr);
			}
		}

		[AOT.MonoPInvokeCallback(typeof(Plugin.EffekseerSoundLoaderLoad))]
		private static IntPtr SoundLoaderLoad(IntPtr path) {
			var pathstr = Marshal.PtrToStringUni(path);
			var asset = Instance.effectAssetInLoading;
			
			var res = asset.FindSound(pathstr);
			if (res != null) {
				return res.ToIntPtr();
			}
			return IntPtr.Zero;
		}
		[AOT.MonoPInvokeCallback(typeof(Plugin.EffekseerSoundLoaderUnload))]
		private static void SoundLoaderUnload(IntPtr path) {
		}

		#endregion
	}
}
