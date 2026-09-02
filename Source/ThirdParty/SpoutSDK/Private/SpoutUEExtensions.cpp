// Copyright (c) 2026 SpoutUE contributors. MIT licensed.

#include "SpoutUEExtensions.h"

bool FSpoutSenderDX::SendTextureAs(ID3D11Texture2D* Texture, DXGI_FORMAT PublishFormat)
{
	if (!Texture)
	{
		return false;
	}

	if (!OpenDirectX11())
	{
		return false;
	}

	D3D11_TEXTURE2D_DESC Desc = {};
	Texture->GetDesc(&Desc);
	if (Desc.Width == 0 || Desc.Height == 0)
	{
		return false;
	}

	// Fall back to whatever the texture says when the caller has no opinion.
	const DXGI_FORMAT SenderFormat = (PublishFormat == DXGI_FORMAT_UNKNOWN) ? Desc.Format : PublishFormat;

	if (!CheckSender(Desc.Width, Desc.Height, static_cast<DWORD>(SenderFormat)))
	{
		return false;
	}

	if (frame.CheckTextureAccess(m_pSharedTexture))
	{
		m_pImmediateContext->CopyResource(m_pSharedTexture, Texture);

		// The shared texture was written on this device, so the copy has to be
		// submitted before another process is told there is a frame waiting.
		m_pImmediateContext->Flush();

		frame.SetNewFrame();
		frame.AllowTextureAccess(m_pSharedTexture);
		return true;
	}

	// The mutex was held by someone else: this frame is dropped rather than torn.
	return false;
}
