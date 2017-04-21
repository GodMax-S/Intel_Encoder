#include "stdafx.h"
#include <iostream>
#include <string>
#include <fstream>
#include <array>
#include <exception>
#include <cassert>
#include <memory>

#include <Windows.h>
#include <atlbase.h>

#include "IMSDK_Coder.h"
#include "Util.h"
#include "TaskPool.h"

DeviceAndContext IMSDK_Coder::m_devcon;
std::map<mfxHDL, AllocResponseCounted> IMSDK_Coder::AllocationResponses;

IMSDK_Coder::IMSDK_Coder()
{
}


IMSDK_Coder::~IMSDK_Coder()
{
}

void IMSDK_Coder::SetImplementation(const mfxIMPL impl, const mfxVersion ver)
{
    m_impl = impl;
    m_ver = ver;
}

void IMSDK_Coder::Test(const std::string& input_file_name, const std::vector<std::string>& output_file_name)
{
    std::array<int, ParallelNum> size_modifier = { 2, 4 };
    const int VideoWidth = 854;
    const int VideoHeight = 480;
    const int AsyncDepth = 4;

    std::fstream in_stream(input_file_name, std::ios::in | std::ios::binary);
    std::vector<std::fstream> out_stream(ParallelNum);
    for (int i = 0; i < ParallelNum; i++)
    {
        out_stream[i].open(output_file_name[i], std::ios::trunc | std::ios::out | std::ios::binary);
    }

    mfxStatus sts = MFX_ERR_NONE;

    mfxFrameAllocator frame_allocator;
    std::vector<MFXVideoSession> session(ParallelNum);
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
    mfxVersion ver = { {0, 1} };
    
    // Initialize and join sessions
    for (int i = 0; i < ParallelNum; i++)
    {
        sts = session[i].Init(impl, &ver);
        HandleMFXErrors(sts, "Session initialization"); 
    }

    InitializeAllocator(session[0], frame_allocator);

    for (int i = 1; i < ParallelNum; i++)
    {
        sts = session[0].JoinSession(session[i]);
        HandleMFXErrors(sts, "Joining Sessions");
    }

    for (int i = 0; i < ParallelNum; i++)
    {
        sts = session[i].SetHandle(MFX_HANDLE_D3D11_DEVICE, m_devcon.device);
        HandleMFXErrors(sts, "Setting D3D11 device handle");
        sts = session[i].SetFrameAllocator(&frame_allocator);
        HandleMFXErrors(sts, "Setting frame allocator");
    }

    std::vector<MFXVideoVPP> vpp;
    for (int i = 0; i < ParallelNum; i++)
    {
        vpp.emplace_back(session[i]);
    }
    
    std::vector<mfxVideoParam> vpp_param(ParallelNum, { 0 });
    for (int i = 0; i < ParallelNum; i++)
    {
        // Input parameters
        vpp_param[i].vpp.In.FourCC = MFX_FOURCC_NV12;
        vpp_param[i].vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
        vpp_param[i].vpp.In.CropX = 0;
        vpp_param[i].vpp.In.CropY = 0;
        vpp_param[i].vpp.In.CropW = VideoWidth;
        vpp_param[i].vpp.In.CropH = VideoHeight;
        vpp_param[i].vpp.In.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
        vpp_param[i].vpp.In.FrameRateExtN = 25;
        vpp_param[i].vpp.In.FrameRateExtD = 1;
        vpp_param[i].vpp.In.Width = Util::RoundUp(VideoWidth, 16);
        vpp_param[i].vpp.In.Height = Util::RoundUp(VideoHeight, 16);

        // Output parameters
        vpp_param[i].vpp.Out.FourCC = MFX_FOURCC_NV12;
        vpp_param[i].vpp.Out.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
        vpp_param[i].vpp.Out.CropX = 0;
        vpp_param[i].vpp.Out.CropY = 0;
        vpp_param[i].vpp.Out.CropW = VideoWidth / size_modifier[i];
        vpp_param[i].vpp.Out.CropH = VideoHeight / size_modifier[i];
        vpp_param[i].vpp.Out.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
        vpp_param[i].vpp.Out.FrameRateExtN = 25;
        vpp_param[i].vpp.Out.FrameRateExtD = 1;
        vpp_param[i].vpp.Out.Width = Util::RoundUp(vpp_param[i].vpp.Out.CropW, 16);
        vpp_param[i].vpp.Out.Height = Util::RoundUp(vpp_param[i].vpp.Out.CropH, 16);

        vpp_param[i].IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
        vpp_param[i].AsyncDepth = AsyncDepth;

        sts = vpp[i].Query(&vpp_param[i], &vpp_param[i]);
        HandleMFXErrors(sts, "VPP parameters verification");
        if (sts > MFX_ERR_NONE)
        {
            std::cerr << "Warning " << sts << " during VPP parameters verification\n";
        }
    }

    std::vector<MFXVideoENCODE> encode;
    for (int i = 0; i < ParallelNum; i++)
    {
        encode.emplace_back(session[i]);
    }
    std::vector<mfxVideoParam> enc_param(ParallelNum, { 0 });

    for (int i = 0; i < ParallelNum; i++)
    {
        enc_param[i].mfx.CodecId = MFX_CODEC_AVC;
        enc_param[i].mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED; // quality-to-speed balance
        enc_param[i].mfx.TargetKbps = 500; // bitrate
        enc_param[i].mfx.RateControlMethod = MFX_RATECONTROL_VBR;
        enc_param[i].mfx.FrameInfo.FrameRateExtN = 25;
        enc_param[i].mfx.FrameInfo.FrameRateExtD = 1;
        enc_param[i].mfx.FrameInfo.FourCC = MFX_FOURCC_NV12;
        enc_param[i].mfx.FrameInfo.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
        enc_param[i].mfx.FrameInfo.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
        enc_param[i].mfx.FrameInfo.CropX = 0;
        enc_param[i].mfx.FrameInfo.CropY = 0;
        enc_param[i].mfx.FrameInfo.CropW = vpp_param[i].vpp.Out.CropW;
        enc_param[i].mfx.FrameInfo.CropH = vpp_param[i].vpp.Out.CropH;
        enc_param[i].mfx.FrameInfo.Width = Util::RoundUp(enc_param[i].mfx.FrameInfo.CropW, 16);
        enc_param[i].mfx.FrameInfo.Height = Util::RoundUp(enc_param[i].mfx.FrameInfo.CropH, 16);
        enc_param[i].IOPattern = MFX_IOPATTERN_IN_OPAQUE_MEMORY;
        enc_param[i].AsyncDepth = AsyncDepth;

        sts = encode[i].Query(&enc_param[i], &enc_param[i]);
        HandleMFXErrors(sts, "Encode parameters verification");
        if (sts > MFX_ERR_NONE)
        {
            std::cerr << "Warning " << sts << " during encode parameters verification\n";
        }
    }

    // Query and allocate surfaces
    std::vector<mfxFrameAllocRequest> vpp_alloc_request(ParallelNum * 2, { 0 });
    std::vector<mfxFrameAllocRequest> enc_alloc_request(ParallelNum, { 0 });
    
    for (int i = 0; i < ParallelNum; i++)
    {
        sts = vpp[i].QueryIOSurf(&vpp_param[i], &vpp_alloc_request[i*2]);
        HandleMFXErrors(sts, "VPP surface number query");

        sts = encode[i].QueryIOSurf(&enc_param[i], &enc_alloc_request[i]);
        HandleMFXErrors(sts, "Encude surface number query");
    }

    mfxFrameAllocRequest vpp_alloc_main_request = vpp_alloc_request[0];
    vpp_alloc_main_request.NumFrameSuggested = 0;
    std::vector<int> vpp_out_enc_surf_num(ParallelNum);
    for (int i = 0; i < ParallelNum; i++)
    {
        vpp_alloc_main_request.NumFrameSuggested += vpp_alloc_request[i*2].NumFrameSuggested + AsyncDepth;
        vpp_out_enc_surf_num[i] = vpp_alloc_request[i*2 + 1].NumFrameSuggested + enc_alloc_request[i].NumFrameSuggested + AsyncDepth;
    }

    mfxU8 bits_per_pixel = 12; // NV12 format is a 12 bits per pixel format

    //auto AllocateSurfaces = [](mfxFrameSurface1**& surfaces, const int surf_num, 
    //                           const mfxU16 width, const mfxU16 height, const int bits_per_pixel, 
    //                           const mfxFrameInfo& frame_info)
    //{
    //    mfxU16 surf_width = static_cast<mfxU16>(Util::RoundUp(width, 32));
    //    mfxU16 surf_height = static_cast<mfxU16>(Util::RoundUp(height, 32));
    //    mfxU32 surf_size = surf_width * surf_height * bits_per_pixel / 8;
    //    mfxU8* surf_buffers = new mfxU8[surf_size * surf_num];

    //    surfaces = new mfxFrameSurface1*[surf_num];
    //    for (int i = 0; i < surf_num; i++)
    //    {
    //        surfaces[i] = new mfxFrameSurface1{ 0 };
    //        surfaces[i]->Info = frame_info;
    //        surfaces[i]->Data.Y = &surf_buffers[surf_size * i];
    //        surfaces[i]->Data.U = surfaces[i]->Data.Y + surf_width * surf_height;
    //        surfaces[i]->Data.V = surfaces[i]->Data.U + 1;
    //        surfaces[i]->Data.Pitch = surf_width;
    //    }
    //};

    
    
    // Allocate vpp input surfaces (video memory)
    mfxFrameAllocResponse alloc_response;
    frame_allocator.Alloc(frame_allocator.pthis, &vpp_alloc_main_request, &alloc_response);
    mfxU16 vpp_in_surf_num = alloc_response.NumFrameActual;
    mfxFrameSurface1** vpp_in_surfaces = new mfxFrameSurface1*[vpp_in_surf_num];
    for (int i = 0; i < vpp_in_surf_num; i++)
    {
        vpp_in_surfaces[i] = new mfxFrameSurface1{ 0 };
        vpp_in_surfaces[i]->Info = vpp_param[0].vpp.In;
        vpp_in_surfaces[i]->Data.MemId = alloc_response.mids[i];
    }

    //AllocateSurfaces(vpp_in_surfaces, vpp_in_surf_num, 
    //                 vpp_alloc_request[0].Info.Width, vpp_alloc_request[0].Info.Height, 12,
    //                 vpp_param[0].vpp.In);

    // Allocate vpp output/encode surfaces
    std::vector<mfxFrameSurface1**> vpp_out_enc_surfaces(ParallelNum, nullptr);

    std::vector<mfxExtOpaqueSurfaceAlloc> enc_opaque_alloc_ext(ParallelNum, { 0 });
    std::vector<mfxExtOpaqueSurfaceAlloc> vpp_out_opaque_alloc_ext(ParallelNum, { 0 });
    std::vector<mfxExtBuffer*> enc_ext_buffer(ParallelNum, nullptr);
    std::vector<mfxExtBuffer*> vpp_out_ext_buffer(ParallelNum, nullptr);
    for (int k = 0; k < ParallelNum; k++)
    {
        vpp_out_enc_surfaces[k] = new mfxFrameSurface1*[vpp_out_enc_surf_num[k]];
        for (int i = 0; i < vpp_out_enc_surf_num[k]; i++)
        {
            vpp_out_enc_surfaces[k][i] = new mfxFrameSurface1{ 0 };
            vpp_out_enc_surfaces[k][i]->Info = vpp_param[k].vpp.Out;
        }

        
        enc_opaque_alloc_ext[k].Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
        enc_opaque_alloc_ext[k].Header.BufferSz = sizeof(mfxExtOpaqueSurfaceAlloc);
        enc_ext_buffer[k] = reinterpret_cast<mfxExtBuffer*>(&enc_opaque_alloc_ext[k]);

        
        vpp_out_opaque_alloc_ext[k].Header.BufferId = MFX_EXTBUFF_OPAQUE_SURFACE_ALLOCATION;
        vpp_out_opaque_alloc_ext[k].Header.BufferSz = sizeof(mfxExtOpaqueSurfaceAlloc);
        vpp_out_ext_buffer[k] = reinterpret_cast<mfxExtBuffer*>(&vpp_out_opaque_alloc_ext[k]);

        enc_opaque_alloc_ext[k].In.Surfaces = vpp_out_enc_surfaces[k];
        enc_opaque_alloc_ext[k].In.NumSurface = vpp_out_enc_surf_num[k];
        enc_opaque_alloc_ext[k].In.Type = enc_alloc_request[k].Type;

        vpp_out_opaque_alloc_ext[k].Out = enc_opaque_alloc_ext[k].In; // Vpp output surfaces are also encoder input surfaces
        vpp_out_opaque_alloc_ext[k].Out.Type = vpp_alloc_request[1].Type;

        enc_param[k].ExtParam = &enc_ext_buffer[k];
        enc_param[k].NumExtParam = 1;
        vpp_param[k].ExtParam = &vpp_out_ext_buffer[k];
        vpp_param[k].NumExtParam = 1;
    }

    auto FindFreeSurfaceIndex = [](mfxFrameSurface1** surfaces, int size) -> int
    {
        for (int i = 0; i < size; i++)
        {
            if (!surfaces[i]->Data.Locked)
                return i;
        }
        return -1;
    };

    std::vector<TaskPool> task_pool(ParallelNum);

    for (int i = 0; i < ParallelNum; i++)
    {
        sts = vpp[i].Init(&vpp_param[i]);
        HandleMFXErrors(sts, "VPP initiation");

        sts = encode[i].Init(&enc_param[i]);
        HandleMFXErrors(sts, "Encoder initiation");

        mfxVideoParam actual_param{ 0 };
        sts = encode[i].GetVideoParam(&actual_param);
        HandleMFXErrors(sts, "Getting encoder parameters");

        task_pool[i].CreateTasks(enc_param[i].AsyncDepth, actual_param.mfx.BufferSizeInKB * 1000);
    }

    std::vector<int> frame_num(ParallelNum, 0);
    while (true)
    {
        // Get free task index
        std::vector<int> free_task_ind(ParallelNum);
        bool can_proceed = true;
        std::cout << "Frame number: ";
        for (int i = 0; i < ParallelNum; i++)
        {
            free_task_ind[i] = task_pool[i].GetFreeIndex();
            if (free_task_ind[i] == -1) // if no free task available - synchronise the first task
            {
                can_proceed = false;
                auto& first_task = task_pool[i].GetFirstTask();
                sts = session[i].SyncOperation(first_task.sync_point, SyncWaitTime);
                HandleMFXErrors(sts, "Sync Operation");

                out_stream[i].write(reinterpret_cast<char*>(first_task.bitstream.Data + first_task.bitstream.DataOffset), first_task.bitstream.DataLength);
                first_task.bitstream.DataLength = 0;

                first_task.sync_point = 0;
                task_pool[i].IncrementFirst();

                frame_num[i]++;
                std::cout << frame_num[i] << "|";
            }
        }
        std::cout << "\r";
        std::cout.flush();
        if (!can_proceed)
            continue;
        // Find free surface for vpp input
        int in_surf_ind = FindFreeSurfaceIndex(vpp_in_surfaces, vpp_in_surf_num);
        if (in_surf_ind == -1)
            std::cerr << "Can't find a free surface for vpp input\n";

        sts = frame_allocator.Lock(frame_allocator.pthis, vpp_in_surfaces[in_surf_ind]->Data.MemId, &(vpp_in_surfaces[in_surf_ind]->Data));
        HandleMFXErrors(sts, "Locking frame");

        bool input_available = LoadFrameToSurface(*(vpp_in_surfaces[in_surf_ind]), in_stream);
        if (!input_available)
            break; // No input available means we reached the end of file. Go to the next stage.

        sts = frame_allocator.Unlock(frame_allocator.pthis, vpp_in_surfaces[in_surf_ind]->Data.MemId, &(vpp_in_surfaces[in_surf_ind]->Data));
        HandleMFXErrors(sts, "Unlocking frame");

        std::vector<int> out_surf_ind(ParallelNum, 0);
        for (int i = 0; i < ParallelNum; i++)
        {
            while (true)
            {
                mfxSyncPoint vpp_sync_point;
                out_surf_ind[i] = FindFreeSurfaceIndex(vpp_out_enc_surfaces[i], vpp_out_enc_surf_num[i]);
                if (out_surf_ind[i] == -1)
                    std::cerr << "Can't find a free surface for vpp outup/encode\n";
                sts = vpp[i].RunFrameVPPAsync(vpp_in_surfaces[in_surf_ind], vpp_out_enc_surfaces[i][out_surf_ind[i]], nullptr, &vpp_sync_point);
                if (sts == MFX_WRN_DEVICE_BUSY)
                {
                    Sleep(1);
                    continue;
                }
                break;
            }

            if (sts == MFX_ERR_MORE_DATA)
            {
                continue;
            }
            if (sts == MFX_ERR_MORE_SURFACE)
            {
                // This shouldn't happen now
                // TODO Change later
                std::cerr << "Need more surface for vpp\n";
            }

            HandleMFXErrors(sts);

            while (true)
            {
                sts = encode[i].EncodeFrameAsync(nullptr, vpp_out_enc_surfaces[i][out_surf_ind[i]], &task_pool[i].GetTask(free_task_ind[i]).bitstream,
                                              &task_pool[i].GetTask(free_task_ind[i]).sync_point);
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
        }
    }

    //
    // Stage 2: Retrieve the buffered encoded frames
    //
    std::vector<bool> buffer_empty(ParallelNum, false);
    int buffer_empty_num = 0; // number of encoders that emptied their buffers fully
    while (true)
    {
        if (buffer_empty_num >= ParallelNum)
            break;
        // Get free task index
        std::vector<int> free_task_ind(ParallelNum);
        bool can_proceed = true;
        std::cout << "Frame number: ";
        for (int i = 0; i < ParallelNum; i++)
        {
            free_task_ind[i] = task_pool[i].GetFreeIndex();
            if (free_task_ind[i] == -1) // if no free task available - synchronise the first task
            {
                can_proceed = false;
                auto& first_task = task_pool[i].GetFirstTask();
                sts = session[i].SyncOperation(first_task.sync_point, SyncWaitTime);
                HandleMFXErrors(sts, "Sync Operation");

                out_stream[i].write(reinterpret_cast<char*>(first_task.bitstream.Data + first_task.bitstream.DataOffset), first_task.bitstream.DataLength);
                first_task.bitstream.DataLength = 0;

                first_task.sync_point = 0;
                task_pool[i].IncrementFirst();

                frame_num[i]++;
                std::cout << frame_num[i] << "|";
            }
        }
        std::cout << "\r";
        std::cout.flush();
        if (!can_proceed)
            continue;

        std::vector<int> out_surf_ind(ParallelNum, 0);
        for (int i = 0; i < ParallelNum; i++)
        {
            if (buffer_empty[i])
                continue;   // Skip encoders that finished emptying their buffers
            while (true)
            {
                mfxSyncPoint vpp_sync_point;
                out_surf_ind[i] = FindFreeSurfaceIndex(vpp_out_enc_surfaces[i], vpp_out_enc_surf_num[i]);
                if (out_surf_ind[i] == -1)
                    std::cerr << "Can't find a free surface for vpp outup/encode\n";
                sts = vpp[i].RunFrameVPPAsync(nullptr, vpp_out_enc_surfaces[i][out_surf_ind[i]], nullptr, &vpp_sync_point);
                if (sts == MFX_WRN_DEVICE_BUSY)
                {
                    Sleep(1);
                    continue;
                }
                break;
            }

            if (sts == MFX_ERR_MORE_DATA)
            {
                buffer_empty[i] = true;
                buffer_empty_num++;
                continue;
            }
            if (sts == MFX_ERR_MORE_SURFACE)
            {
                // This shouldn't happen now
                // TODO Change later
                std::cerr << "Need more surface for vpp\n";
            }

            HandleMFXErrors(sts);

            while (true)
            {
                sts = encode[i].EncodeFrameAsync(nullptr, vpp_out_enc_surfaces[i][out_surf_ind[i]], &task_pool[i].GetTask(free_task_ind[i]).bitstream,
                                                 &task_pool[i].GetTask(free_task_ind[i]).sync_point);
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
            if (sts == MFX_ERR_MORE_DATA)
            {
                std::cerr << "More data error received while retrieving buffered encode frames\n";
            }
        }
    }

    //
    // Stage 3: Sync all remaining tasks in task pool
    //
    for (int i = 0; i < ParallelNum; i++)
        buffer_empty[i] = false;
    buffer_empty_num = 0;

    while (true)
    {
        if (buffer_empty_num == ParallelNum)
            break;
        std::cout << "Frame number: ";
        for (int i = 0; i < ParallelNum; i++)
        {
            if (buffer_empty[i])
                continue;
            auto& first_task = task_pool[i].GetFirstTask();
            if (first_task.sync_point == 0)
            {
                buffer_empty[i] = true;
                buffer_empty_num++;
                continue;
            }

            sts = session[i].SyncOperation(first_task.sync_point, SyncWaitTime);
            HandleMFXErrors(sts, "Sync Operation");

            out_stream[i].write(reinterpret_cast<char*>(first_task.bitstream.Data + first_task.bitstream.DataOffset), first_task.bitstream.DataLength);
            first_task.bitstream.DataLength = 0;

            first_task.sync_point = 0;
            task_pool[i].IncrementFirst();

            frame_num[i]++;
            std::cout << frame_num[i] << "|";
        }
        std::cout << "\r";
        std::cout.flush();
    }

    // =============================================================
    // Clean up

    encode[0].Close();
    vpp[0].Close();

    for (int i = 1; i < ParallelNum; i++)
    {
        session[i].DisjoinSession();
        encode[i].Close();
        vpp[i].Close();
    }

    frame_allocator.Free(frame_allocator.pthis, &alloc_response);
}

void IMSDK_Coder::HandleMFXErrors(const mfxStatus status, const std::string& where_str /* = "Unspecified operation" */)
{
    if (status < MFX_ERR_NONE)
    {
        std::cerr << "Error " << status << " during " << where_str << "\n";
        getchar();
    }
}

bool IMSDK_Coder::LoadFrameToSurface(mfxFrameSurface1& surface, std::fstream& in_stream)
{
    // TODO Add real error handling
    auto& surf_info = surface.Info;
    auto& surf_data = surface.Data;
    int width, height, pitch;
    mfxU8* ptr;

    if (surf_info.CropW > 0 && surf_info.CropH > 0)
    {
        width = surf_info.CropW;
        height = surf_info.CropH;
    }
    else
    {
        width = surf_info.Width;
        height = surf_info.Height;
    }
    pitch = surf_data.Pitch;
    ptr = (surf_data.Y + surf_info.CropY * pitch + surf_info.CropX);

    for (int i = 0; i < height; i++)
    {
        in_stream.read(reinterpret_cast<char*>(ptr + i * pitch), width);
        if (in_stream.gcount() != width)
        {
            return false;
        }
    }

    mfxU8 buf[2048];
    width /= 2;
    height /= 2;
    ptr = surf_data.U + surf_info.CropX + (surf_info.CropY / 2) * pitch;
    if (width > 2048)
    {
        std::cerr << "Width larger than 2048 is not supported\n";
        return false;
    }

    auto ReadPlaneData = [](int width, int height, mfxU8* buf, mfxU8* ptr, int pitch, int offset, std::fstream& in_stream) -> bool
    {
        for (int i = 0; i < height; i++)
        {
            in_stream.read(reinterpret_cast<char*>(buf), width);
            if (in_stream.gcount() != width)
            {
                return false;
            }
            for (int j = 0; j < width; j++)
            {
                ptr[i * pitch + j * 2 + offset] = buf[j];
            }
        }
        return true;
    };

    if (!ReadPlaneData(width, height, buf, ptr, pitch, 0, in_stream))
        return false;
    if (!ReadPlaneData(width, height, buf, ptr, pitch, 1, in_stream))
        return false;
    return true;
}

void IMSDK_Coder::InitializeAllocator(MFXVideoSession& session, mfxFrameAllocator& frame_allocator)
{
    mfxStatus sts;

    mfxIMPL impl;
    sts = session.QueryIMPL(&impl);
    HandleMFXErrors(sts, "Quering implementation");

    int device_num = GetIntelDeviceNumber(impl);
    CComPtr<IDXGIAdapter> adapter = GetDXGIAdapter(device_num);
    CreateD3D11Device(adapter); // assignes the created device and context to m_devcon
    
    frame_allocator.pthis = session;
    frame_allocator.Alloc = VideoAlloc;
    frame_allocator.Free = VideoFree;
    frame_allocator.Lock = VideoLock;
    frame_allocator.Unlock = VideoUnlock;
    frame_allocator.GetHDL = VideoGetHandle;
}

int IMSDK_Coder::GetIntelDeviceNumber(const mfxIMPL impl)
{
    struct ImplToID
    {
        mfxIMPL impl;
        int ID;
    };
    static const std::array<ImplToID, 4> impl_to_id = { {
        { MFX_IMPL_HARDWARE, 0 },
        { MFX_IMPL_HARDWARE2, 1 },
        { MFX_IMPL_HARDWARE3, 2 },
        { MFX_IMPL_HARDWARE4, 3 }
    } };
    
    mfxIMPL base_impl = MFX_IMPL_BASETYPE(impl);

    for (int i = 0; i < impl_to_id.size(); i++)
    {
        if (base_impl == impl_to_id[i].impl)
        {
            return impl_to_id[i].ID;
        }
    }
    throw std::runtime_error("Session implementation is not hardware based");
}

CComPtr<IDXGIAdapter> IMSDK_Coder::GetDXGIAdapter(const int adapter_num)
{
    CComPtr<IDXGIFactory> dxgi_factory;
    HRESULT hres;
    hres = CreateDXGIFactory(__uuidof(IDXGIFactory), reinterpret_cast<void**>(&dxgi_factory));
    if (FAILED(hres))
        throw std::runtime_error("Failed to create a DXGI Factory");
    CComPtr<IDXGIAdapter> adapter;
    hres = dxgi_factory->EnumAdapters(adapter_num, &adapter);
    if (FAILED(hres))
        throw std::runtime_error("Failed to enumerate to the required adapter");
    return adapter;
}

void IMSDK_Coder::CreateD3D11Device(IDXGIAdapter* adapter)
{
    HRESULT hres = 0;
    std::array<D3D_FEATURE_LEVEL, 4> feature_levels = {
        D3D_FEATURE_LEVEL_11_1,
        D3D_FEATURE_LEVEL_11_0,
        D3D_FEATURE_LEVEL_10_1,
        D3D_FEATURE_LEVEL_10_0
    };
    hres = D3D11CreateDevice(
        adapter,
        D3D_DRIVER_TYPE_UNKNOWN,
        NULL,
        NULL,
        feature_levels.data(),
        static_cast<UINT>(feature_levels.size()),
        D3D11_SDK_VERSION,
        &m_devcon.device,
        nullptr,
        &m_devcon.context
    );
    if (FAILED(hres))
        throw std::runtime_error("Failed to create D3D11 device");

    CComQIPtr<ID3D10Multithread> multithread(m_devcon.context);
    if (multithread != nullptr)
        multithread->SetMultithreadProtected(true);
    else
        throw std::runtime_error("Failed to access multithread interface of the device context");
}

// Allocator functions

mfxStatus IMSDK_Coder::VideoAlloc(mfxHDL handle, mfxFrameAllocRequest* request, mfxFrameAllocResponse* response)
{
    assert(!(request->Type & MFX_MEMTYPE_SYSTEM_MEMORY));

    auto fin_resp = AllocationResponses.find(handle);
    if (fin_resp != AllocationResponses.end())
    {
        *response = fin_resp->second.response;
        fin_resp->second.count++;
        return MFX_ERR_NONE;
    }

    auto frame_num = request->NumFrameSuggested;
    std::vector<std::unique_ptr<CustomMemId>> mids (frame_num);
    

    for (int i = 0; i < frame_num; i++)
        mids[i].reset(new CustomMemId);

    D3D11_TEXTURE2D_DESC texture_desc = { 0 };
    texture_desc.Width = request->Info.Width;
    texture_desc.Height = request->Info.Height;
    texture_desc.MipLevels = 1;
    texture_desc.ArraySize = 1;
    texture_desc.Format = DXGI_FORMAT_NV12;
    texture_desc.SampleDesc.Count = 1;
    texture_desc.Usage = D3D11_USAGE_DEFAULT;
    texture_desc.BindFlags = D3D11_BIND_DECODER;
    texture_desc.MiscFlags = 0;

    HRESULT hres;
    ID3D11Texture2D* ptexture;
    for (int i = 0; i < frame_num; i++)
    {
        hres = m_devcon.device->CreateTexture2D(&texture_desc, NULL, &ptexture);
        if (FAILED(hres))
            throw std::runtime_error("Failed to create texture");

        mids[i]->memid = ptexture;
    }
    
    texture_desc.Usage = D3D11_USAGE_STAGING;
    texture_desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    texture_desc.BindFlags = 0;

    for (int i = 0; i < frame_num; i++)
    {
        hres = m_devcon.device->CreateTexture2D(&texture_desc, NULL, &ptexture);
        if (FAILED(hres))
            throw std::runtime_error("Failed to create staging texture");

        mids[i]->stage_memid = ptexture;
    }

    CustomMemId** unsafe_memid = new CustomMemId*[frame_num];
    for (int i = 0; i < frame_num; i++)
    {
        unsafe_memid[i] = mids[i].release();
    }
    response->mids = reinterpret_cast<mfxMemId*>(unsafe_memid);
    response->NumFrameActual = frame_num;

    AllocationResponses[handle].response = *response;
    AllocationResponses[handle].count = 1;

    return MFX_ERR_NONE;
}

mfxStatus IMSDK_Coder::VideoFreeInternal(mfxFrameAllocResponse* response)
{

    if (response->mids != nullptr)
    {
        for (int i = 0; i < response->NumFrameActual; i++)
        {
            if (response->mids[i])
            {
                CustomMemId*     mid = reinterpret_cast<CustomMemId*>(response->mids[i]);
                ID3D11Texture2D* psurface = reinterpret_cast<ID3D11Texture2D*>(mid->memid);
                ID3D11Texture2D* pstage = reinterpret_cast<ID3D11Texture2D*>(mid->stage_memid);

                if (psurface)
                    psurface->Release();
                if (pstage)
                    pstage->Release();

                delete mid;
            }
        }
        delete[] response->mids;
        response->mids = nullptr;
    }
    return MFX_ERR_NONE;
}

mfxStatus IMSDK_Coder::VideoFree(mfxHDL handle, mfxFrameAllocResponse* response)
{
    assert(response != nullptr);

    auto fin_resp = AllocationResponses.find(handle);
    if (fin_resp != AllocationResponses.end())
    {
        if (fin_resp->second.count-- == 0)
        {
            VideoFreeInternal(response);
            AllocationResponses.erase(fin_resp);
        }
    }
    else
    {
        throw std::runtime_error("Request to free nonexistent memory");
    }
    return MFX_ERR_NONE;
}

mfxStatus IMSDK_Coder::VideoLock(mfxHDL, mfxMemId mid, mfxFrameData* frame)
{
    HRESULT hres = S_OK;

    D3D11_TEXTURE2D_DESC desc = { 0 };
    D3D11_MAPPED_SUBRESOURCE locked_rect = { 0 };

    CustomMemId*     cmid     = reinterpret_cast<CustomMemId*>(mid);
    ID3D11Texture2D* surface = reinterpret_cast<ID3D11Texture2D*>(cmid->memid);
    ID3D11Texture2D* stage   = reinterpret_cast<ID3D11Texture2D*>(cmid->stage_memid);

    D3D11_MAP map_type  = D3D11_MAP_WRITE;
    UINT      map_flags = D3D11_MAP_FLAG_DO_NOT_WAIT;

    surface->GetDesc(&desc);
    while (true)
    {
        hres = m_devcon.context->Map(stage, 0, map_type, map_flags, &locked_rect);
        if (!FAILED(hres))
            break;
        if (FAILED(hres) && hres != DXGI_ERROR_WAS_STILL_DRAWING)
            throw std::runtime_error("Couldn't map the video memory");
    } while (hres == DXGI_ERROR_WAS_STILL_DRAWING);
    
    frame->Pitch = static_cast<mfxU16>(locked_rect.RowPitch);
    frame->Y = reinterpret_cast<mfxU8*>(locked_rect.pData);
    frame->U = frame->Y + desc.Height * locked_rect.RowPitch;
    frame->V = frame->U + 1;

    return MFX_ERR_NONE;
}

mfxStatus IMSDK_Coder::VideoUnlock(mfxHDL, mfxMemId mid, mfxFrameData* frame)
{
    CustomMemId*     cmid = reinterpret_cast<CustomMemId*>(mid);
    ID3D11Texture2D* surface = reinterpret_cast<ID3D11Texture2D*>(cmid->memid);
    ID3D11Texture2D* stage = reinterpret_cast<ID3D11Texture2D*>(cmid->stage_memid);

    m_devcon.context->Unmap(surface, 0);
    m_devcon.context->CopySubresourceRegion(surface, 0, 0, 0, 0, stage, 0, NULL);

    if (frame)
    {
        frame->Pitch = 0;
        frame->U = frame->V = frame->Y = 0;
    }

    return MFX_ERR_NONE;
}

mfxStatus IMSDK_Coder::VideoGetHandle(mfxHDL, mfxMemId mid, mfxHDL* handle)
{
    if (handle == nullptr)
        return MFX_ERR_INVALID_HANDLE;

    mfxHDLPair* handle_pair = reinterpret_cast<mfxHDLPair*>(handle);
    CustomMemId* cmid = reinterpret_cast<CustomMemId*>(mid);

    handle_pair->first = cmid->memid;
    handle_pair->second = 0;

    return MFX_ERR_NONE;
}



