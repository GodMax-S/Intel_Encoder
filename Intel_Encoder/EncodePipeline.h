#pragma once

#include <memory>
#include <vector>

#include <mfxvideo++.h>

#include "TaskPool.h"

class EncodePipeline
{
public:
    EncodePipeline();
    ~EncodePipeline();

    EncodePipeline(const EncodePipeline&) = delete;
    EncodePipeline operator=(const EncodePipeline&) = delete;
    EncodePipeline(EncodePipeline&&);

    void InitSession(mfxIMPL impl, mfxVersion ver);
    void InitBase(mfxVideoParam& vpp_param, mfxVideoParam& enc_param);
    void Clear();

    bool ProcessFrame(mfxFrameSurface1* surface, Task& task);

    MFXVideoVPP* GetVPP();
    MFXVideoENCODE* GetEncode();
    MFXVideoSession& GetSession();

private:
    MFXVideoSession m_session;
    std::unique_ptr<MFXVideoVPP> m_vpp;
    std::unique_ptr<MFXVideoENCODE> m_encode;

    std::vector<mfxFrameSurface1*> m_surfaces;
    std::unique_ptr<mfxExtOpaqueSurfaceAlloc> m_vpp_out_opaque_alloc_ext;
    std::unique_ptr<mfxExtOpaqueSurfaceAlloc> m_enc_opaque_alloc_ext;
    mfxExtBuffer* m_vpp_out_ext_buffer;
    mfxExtBuffer* m_enc_ext_buffer;
};
