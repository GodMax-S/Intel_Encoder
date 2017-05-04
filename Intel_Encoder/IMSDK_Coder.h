#pragma once
#include <string>
#include <vector>
#include <map>

#include <dxgi.h>
#include <d3d11.h>
#include <atlbase.h>

#include "mfxvideo++.h"

//#include "FrameAllocator.h"
#include "EncodePipeline.h"
#include "TaskPool.h"

/********************************************************************
    Encodes frames into H.264 bitstream.

    Can work with either video or system memory.
    For video memory only handles D3D11 devices.

    EncodeFrame takes a shared pointer, it will release the pointer 
    when it's no longer needed.
*********************************************************************/


struct DeviceAndContext;
struct CustomMemId;
struct AllocResponseCounted;

class IMSDK_Coder
{
public:
    IMSDK_Coder();
    ~IMSDK_Coder();
    IMSDK_Coder(const IMSDK_Coder&) = delete;
    IMSDK_Coder operator=(const IMSDK_Coder&) = delete;
    IMSDK_Coder(IMSDK_Coder&&) = default;

    static void Test(const std::string& input_file_name, const std::vector<std::string>& output_file_name);
    void InitializeCoder(bool use_video_memory, int pipeline_num, mfxIMPL impl, mfxVersion ver, std::vector<mfxVideoParam>& vpp_params, std::vector<mfxVideoParam>& encode_params);
    void ClearCoder();

    std::vector<std::vector<char>> EncodeFrame(const char* frame_data, int frame_data_pitch);
    std::vector<std::vector<std::vector<char>>> EncodeFinal();

private:
    static const int SyncWaitTime = 60000;
    static const int ParallelNum = 2;
    static DeviceAndContext m_devcon;
    static std::map<mfxHDL, AllocResponseCounted> AllocationResponses;

    mfxIMPL m_impl;
    mfxVersion m_ver;
    std::vector<EncodePipeline> m_pipelines;
    mfxFrameAllocator m_frame_allocator;
    mfxFrameAllocResponse m_video_alloc_response;
    std::vector<std::unique_ptr<mfxFrameSurface1>> m_surfaces;
    std::vector<mfxU8> m_surf_buffers;
    std::vector<TaskPool> m_task_pools;
    bool m_use_video_memory;

    //static void HandleMFXErrors(const mfxStatus, const std::string& = "Unspecified operation");
    //static int  FindFreeSurfaceIndex(const std::vector<std::unique_ptr<mfxFrameSurface1>>& surfaces);
    void        LoadFrameToSurface(mfxFrameSurface1&, const char* data, int data_pitch);
    static bool LoadFrameToSurfaceFromFile(mfxFrameSurface1&, std::fstream&);

    static void                     InitializeAllocator(MFXVideoSession& session, mfxFrameAllocator& frame_allocator);
    static int                      GetIntelDeviceNumber(const mfxIMPL impl);
    static CComPtr<IDXGIAdapter>    GetDXGIAdapter(const int adapter_num);
    static void                     CreateD3D11Device(IDXGIAdapter* adapter);

    static mfxStatus VideoAlloc(mfxHDL, mfxFrameAllocRequest*, mfxFrameAllocResponse*);
    static mfxStatus VideoFree(mfxHDL, mfxFrameAllocResponse*);
    static mfxStatus VideoFreeInternal(mfxFrameAllocResponse*);
    static mfxStatus VideoLock(mfxHDL, mfxMemId, mfxFrameData*);
    static mfxStatus VideoUnlock(mfxHDL, mfxMemId, mfxFrameData*);
    static mfxStatus VideoGetHandle(mfxHDL, mfxMemId, mfxHDL*);
};

struct DeviceAndContext
{
    CComPtr<ID3D11Device> device;
    CComPtr<ID3D11DeviceContext> context;
};

struct CustomMemId 
{
    mfxMemId    memid;
    mfxMemId    stage_memid;
};

struct AllocResponseCounted
{
    mfxFrameAllocResponse response;
    int count;
};
