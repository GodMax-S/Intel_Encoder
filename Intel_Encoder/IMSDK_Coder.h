#pragma once
#include <string>
#include <vector>

#include "mfxvideo++.h"

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
    mfxIMPL m_impl;
    mfxVersion m_ver;
    MFXVideoSession m_session;

    static void HandleMFXErrors(const mfxStatus, const std::string& = "Unspecified operation");
    static bool LoadFrameToSurface(mfxFrameSurface1&, std::fstream&);
};

