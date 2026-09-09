#pragma once
#include "..\..\Include\xrRender\UISequenceVideoItem.h"
#include "bgfxVideoDecoder.h"

class bgfxUISequenceVideoItem : public IUISequenceVideoItem
{
public:
    bgfxUISequenceVideoItem();
    virtual ~bgfxUISequenceVideoItem();

    bool Open(const char* _videoPath);
    virtual void Copy(IUISequenceVideoItem &_in) override;
    virtual bool HasTexture() override;
    virtual void CaptureTexture() override;
    virtual void ResetTexture() override;
    virtual BOOL video_IsPlaying() override;
    virtual void video_Sync(u32 _time) override;
    virtual void video_Play(BOOL looped, u32 _time) override;
    virtual void video_Stop() override;

private:
    void RenderVideoFrame();

    BgfxVideoDecoder m_decoder;
    bool m_textureCaptured;
};
