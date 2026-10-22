// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#pragma once

#include "SpoutDX.h"

/**
 * A spoutDX sender that publishes an explicit DXGI format.
 *
 * spoutDX::SendTexture takes the format straight off the texture it is handed.
 * That is fine for a plain D3D11 application, but Unreal registers most of its
 * pixel formats as typeless (PF_B8G8R8A8 becomes DXGI_FORMAT_B8G8R8A8_TYPELESS)
 * so that it can create both UNORM and UNORM_SRGB views over one resource. A
 * D3D11On12 wrapper mirrors that typeless description, and a sender advertising
 * a typeless format is unusable: receivers have no way to know how to interpret
 * the bits, and OBS or Resolume will show nothing at all.
 *
 * This subclass exists purely to pass the format we intend to publish down to
 * CheckSender, leaving the vendored Spout sources untouched so they can be
 * updated from upstream without re-applying a patch. Copying a typeless texture
 * into a typed shared texture of the same format family is valid D3D11.
 */
// SPOUT_DLLEXP rather than SPOUTSDK_API: this file is compiled with the
// vendored sources, which never include the engine headers that define
// DLLEXPORT. SpoutSDK.Build.cs sets SPOUT_BUILD_DLL / SPOUT_IMPORT_DLL so the
// macro resolves the same way UBT's would.
class SPOUT_DLLEXP FSpoutSenderDX : public spoutDX
{
public:
	/**
	 * Publishes Texture under PublishFormat.
	 *
	 * Behaves exactly like spoutDX::SendTexture otherwise: it creates the sender
	 * on the first call and re-creates the shared texture whenever the size or
	 * the published format changes.
	 */
	bool SendTextureAs(ID3D11Texture2D* Texture, DXGI_FORMAT PublishFormat);
};
