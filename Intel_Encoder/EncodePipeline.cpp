#include "stdafx.h"
#include "EncodePipeline.h"

#include <iostream>

#include "Util.h"
#include "Windows.h"

using namespace Util;

EncodePipeline::EncodePipeline()
{
}

EncodePipeline::~EncodePipeline()
{
    // TODO
}

EncodePipeline::EncodePipeline(EncodePipeline&& other)
{
    m_session = std::move(other.m_session);
    m_vpp = std::move(other.m_vpp);
    m_encode = std::move(other.m_encode);

    m_surfaces = std::move(other.m_surfaces);
    m_vpp_out_opaque_alloc_ext = std::move(other.m_vpp_out_opaque_alloc_ext);
    m_enc_opaque_alloc_ext = std::move(other.m_enc_opaque_alloc_ext);
    m_vpp_out_ext_buffer = std::move(other.m_vpp_out_ext_buffer);
    m_enc_ext_buffer = std::move(other.m_enc_ext_buffer);
}

MFXVideoSession& EncodePipeline::GetSession()
{
    return m_session;
}

void EncodePipeline::InitSession(mfxIMPL impl, mfxVersion ver)
{
    Clear();
    mfxStatus sts = MFX_ERR_NONE;

    // Initialize session
    sts = m_session.Init(impl, &ver);
    HandleMFXErrors(sts, "Session initialization");
}


void EncodePipeline::InitBase(mfxVideoParam& vpp_param, mfxVideoParam& enc_param)
{
    //Clear();
    mfxStatus sts = MFX_ERR_NONE;

    //// Initialize session
    //sts = m_session.Init(impl, &ver);
    //HandleMFXErrors(sts, "Session initialization");

    // Create VPP and Encode and check their parameters for validity
    m_vpp.reset(new MFXVideoVPP(m_session));
    sts = m_vpp->Query(&vpp_param, &vpp_param);
    HandleMFXErrors(sts, "VPP parameters verification");
    if (sts > MFX_ERR_NONE)
    {
        std::cerr << "Warning " << sts << " during VPP parameters verification\n";
    }

    m_encode.reset(new MFXVideoENCODE(m_session));
    sts = m_encode->Query(&enc_param, &enc_param);
    HandleMFXErrors(sts, "VPP parameters verification");
    if (sts > MFX_ERR_NONE)
    {
        std::cerr << "Warning " << sts << " during VPP parameters verification\n";
    }

    // Calculate the number of surfaces needed
    mfxFrameAllocRequest vpp_request[2];
    mfxFrameAllocRequest enc_request;

    sts = m_vpp->QueryIOSurf(&vpp_param, vpp_request);
    HandleMFXErrors(sts, "VPP surface number query");
    sts = m_encode->QueryIOSurf(&enc_param, &enc_request);
    HandleMFXErrors(sts, "Encode surface number query");

    mfxU16 total_surf_num = vpp_request[1].NumFrameSuggested + enc_request.NumFrameSuggested + vpp_param.AsyncDepth;
    m_vpp_out_opaque_alloc_ext.reset(new mfxExtOpaqueSurfaceAlloc{ 0 });
    m_enc_opaque_alloc_ext.reset(new mfxExtOpaqueSurfaceAlloc{ 0 });

    m_surfaces.resize(total_surf_num);
    for (int i = 0; i < m_surfaces.size(); i++)
    {
        m_surfaces[i] = new mfxFrameSurface1{ 0 };
        m_surfaces[i]->Info = vpp_param.vpp.Out;
    }

    // Allocate opaque surfaces
    m_enc_opaque_alloc_ext->Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    m_enc_opaque_alloc_ext->Header.BufferSz = sizeof(mfxExtOpaqueSurfaceAlloc);
    m_enc_ext_buffer = reinterpret_cast<mfxExtBuffer*>(m_enc_opaque_alloc_ext.get());

    m_vpp_out_opaque_alloc_ext->Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
    m_vpp_out_opaque_alloc_ext->Header.BufferSz = sizeof(mfxExtOpaqueSurfaceAlloc);
    m_vpp_out_ext_buffer = reinterpret_cast<mfxExtBuffer*>(m_vpp_out_opaque_alloc_ext.get());

    m_enc_opaque_alloc_ext->In.Surfaces = m_surfaces.data();
    m_enc_opaque_alloc_ext->In.NumSurface = m_surfaces.size();
    m_enc_opaque_alloc_ext->In.Type = enc_request.Type;

    m_vpp_out_opaque_alloc_ext->Out = m_enc_opaque_alloc_ext->In; // Vpp output surfaces are also encoder input surfaces
    m_vpp_out_opaque_alloc_ext->Out.Type = vpp_request[1].Type;

    enc_param.ExtParam = &m_enc_ext_buffer;
    enc_param.NumExtParam = 1;
    vpp_param.ExtParam = &m_vpp_out_ext_buffer;
    vpp_param.NumExtParam = 1;

    // Initialize VPP and Encoder
    sts = m_vpp->Init(&vpp_param);
    HandleMFXErrors(sts, "VPP initiation");
    sts = m_encode->Init(&enc_param);
    HandleMFXErrors(sts, "Encoder initiation");
}

void EncodePipeline::Clear()
{
    //TODO
}

bool EncodePipeline::ProcessFrame(mfxFrameSurface1* surface, Task& task)
{
    mfxStatus sts = MFX_ERR_NONE;
    int out_surf_ind = 0;
    while (true)
    {
        mfxSyncPoint vpp_sync_point;
        out_surf_ind = FindFreeSurfaceIndex(m_surfaces, m_surfaces.size());
        if (out_surf_ind == -1)
            throw std::runtime_error("Cant find a free surface");
        sts = m_vpp->RunFrameVPPAsync(surface, m_surfaces[out_surf_ind], nullptr, &vpp_sync_point);
        if (sts == MFX_WRN_DEVICE_BUSY)
        {
            Sleep(1);
            continue;
        }
        break;
    }

    if (sts == MFX_ERR_MORE_DATA)
    {
        return true;
    }
    if (sts == MFX_ERR_MORE_SURFACE)
    {
        // This shouldn't happen
        std::cerr << "Need more surface for vpp (this shouldn't happen)\n";
    }

    HandleMFXErrors(sts);

    while (true)
    {
        sts = m_encode->EncodeFrameAsync(nullptr, m_surfaces[out_surf_ind], &task.bitstream, &task.sync_point);
        if (sts == MFX_WRN_DEVICE_BUSY)
        {
            Sleep(1);
            continue;
        }
        if (sts > MFX_ERR_NONE)
        {
            std::cerr << "Warning " << sts << " received during encoding\n";
            break;
        }
        if (sts == MFX_ERR_NOT_ENOUGH_BUFFER)
        {
            // TODO Allocate more bitstream buffer here and remove output
            // Actually, whatever, it works like this.
            std::cerr << "Error: Not enough buffer for encoding\n";
            break;
        }
        if (sts < MFX_ERR_NONE && sts != MFX_ERR_MORE_DATA)
        {
            std::cerr << "Error " << sts << " received during encoding\n";
            break;
        }
        break;
    }
    return false;
}

MFXVideoVPP* EncodePipeline::GetVPP()
{
    return m_vpp.get();
}

MFXVideoENCODE* EncodePipeline::GetEncode()
{
    return m_encode.get();
}
