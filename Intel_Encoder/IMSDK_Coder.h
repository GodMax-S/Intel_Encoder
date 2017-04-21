#pragma once
#include <string>
#include <vector>
#include <map>

#include <dxgi.h>
#include <d3d11.h>
#include <atlbase.h>

#include "mfxvideo++.h"

struct DeviceAndContext;
struct CustomMemId;
struct AllocResponseCounted;

class IMSDK_Coder
{
public:
    IMSDK_Coder();
    ~IMSDK_Coder();
    void SetImplementation(const mfxIMPL, const mfxVersion);
    static void Test(const std::string& input_file_name, const std::vector<std::string>& output_file_name);

private:
    static const int SyncWaitTime = 60000;
    static const int ParallelNum = 2;
    static DeviceAndContext m_devcon;
    static std::map<mfxHDL, AllocResponseCounted> AllocationResponses;

    mfxIMPL m_impl;
    mfxVersion m_ver;
    MFXVideoSession m_session;

    static void HandleMFXErrors(const mfxStatus, const std::string& = "Unspecified operation");
    static bool LoadFrameToSurface(mfxFrameSurface1&, std::fstream&);

    static void InitializeAllocator(MFXVideoSession& session, mfxFrameAllocator& frame_allocator);
    static int GetIntelDeviceNumber(const mfxIMPL impl);
    static CComPtr<IDXGIAdapter> GetDXGIAdapter(const int adapter_num);
    static void CreateD3D11Device(IDXGIAdapter* adapter);

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