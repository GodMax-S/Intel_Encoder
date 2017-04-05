#pragma once
#include "mfxvideo++.h"

class IMSDK_Coder
{
public:
    IMSDK_Coder();
    ~IMSDK_Coder();
    void SetOptions(mfxIMPL, mfxVersion);

private:
    mfxIMPL m_impl;
    mfxVersion m_ver;
    MFXVideoSession m_session;

};

