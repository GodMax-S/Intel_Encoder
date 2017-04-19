#include "stdafx.h"
#include <iostream>
#include <string>
#include <fstream>
#include <Windows.h>
#include <array>

#include "IMSDK_Coder.h"
#include "Util.h"
#include "TaskPool.h"



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

    std::vector<MFXVideoSession> session(ParallelNum);
    mfxIMPL impl = MFX_IMPL_AUTO_ANY;
    mfxVersion ver = { {0, 1} };
    
    for (int i = 0; i < ParallelNum; i++)
    {
        sts = session[i].Init(impl, &ver);
        HandleMFXErrors(sts, "Session initialization");
    }
    for (int i = 1; i < ParallelNum; i++)
    {
        sts = session[0].JoinSession(session[i]);
        HandleMFXErrors(sts, "Joining Sessions");
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

        vpp_param[i].IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
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
    //memset(&vpp_alloc_request, 0, sizeof(vpp_alloc_request[0]) * 2);
    
    for (int i = 0; i < ParallelNum; i++)
    {
        sts = vpp[i].QueryIOSurf(&vpp_param[i], &vpp_alloc_request[i*2]);
        HandleMFXErrors(sts, "VPP surface number query");

        sts = encode[i].QueryIOSurf(&enc_param[i], &enc_alloc_request[i]);
        HandleMFXErrors(sts, "Encude surface number query");
    }

    int vpp_in_surf_num = 0;
    std::vector<int> vpp_out_enc_surf_num(ParallelNum);
    for (int i = 0; i < ParallelNum; i++)
    {
        vpp_in_surf_num += vpp_alloc_request[i*2].NumFrameSuggested + AsyncDepth;
        vpp_out_enc_surf_num[i] = vpp_alloc_request[i*2 + 1].NumFrameSuggested + enc_alloc_request[i].NumFrameSuggested + AsyncDepth;
    }

    mfxU8 bits_per_pixel = 12; // NV12 format is a 12 bits per pixel format

    auto AllocateSurfaces = [](mfxFrameSurface1**& surfaces, const int surf_num, 
                               const mfxU16 width, const mfxU16 height, const int bits_per_pixel, 
                               const mfxFrameInfo& frame_info)
    {
        mfxU16 surf_width = static_cast<mfxU16>(Util::RoundUp(width, 32));
        mfxU16 surf_height = static_cast<mfxU16>(Util::RoundUp(height, 32));
        mfxU32 surf_size = surf_width * surf_height * bits_per_pixel / 8;
        mfxU8* surf_buffers = new mfxU8[surf_size * surf_num];

        surfaces = new mfxFrameSurface1*[surf_num];
        for (int i = 0; i < surf_num; i++)
        {
            surfaces[i] = new mfxFrameSurface1{ 0 };
            surfaces[i]->Info = frame_info;
            surfaces[i]->Data.Y = &surf_buffers[surf_size * i];
            surfaces[i]->Data.U = surfaces[i]->Data.Y + surf_width * surf_height;
            surfaces[i]->Data.V = surfaces[i]->Data.U + 1;
            surfaces[i]->Data.Pitch = surf_width;
        }
    };
    mfxFrameSurface1** vpp_in_surfaces = 0;
    std::vector<mfxFrameSurface1**> vpp_out_enc_surfaces(ParallelNum, nullptr);
    AllocateSurfaces(vpp_in_surfaces, vpp_in_surf_num, 
                     vpp_alloc_request[0].Info.Width, vpp_alloc_request[0].Info.Height, 12,
                     vpp_param[0].vpp.In);


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

    //AllocateSurfaces(vpp_out_enc_surfaces, vpp_out_enc_surf_num,
    //                 vpp_alloc_request[1].Info.Width, vpp_alloc_request[1].Info.Height, 12,
    //                 vpp_param.vpp.Out);

    auto FindFreeSurfaceIndex = [](mfxFrameSurface1** surfaces, int size) -> int
    {
        for (int i = 0; i < size; i++)
        {
            if (!surfaces[i]->Data.Locked)
                return i;
        }
        return -1;
    };

    //mfxU16 vpp_in_surf_width = static_cast<mfxU16>(Util::RoundUp(enc_alloc_request.Info.Width, 32));
    //mfxU16 vpp_in_surf_height = static_cast<mfxU16>(Util::RoundUp(enc_alloc_request.Info.Height, 32));
    //mfxU32 vpp_in_surf_size = vpp_in_surf_width * vpp_in_surf_height * bits_per_pixel / 8;
    //mfxU8* vpp_in_surf_buffers = new mfxU8[vpp_in_surf_size * vpp_in_surf_num];

    //
    //for (int i = 0; i < vpp_in_surf_num; i++)
    //{
    //    vpp_in_surfaces[i] = new mfxFrameSurface1{ 0 };
    //    vpp_in_surfaces[i]->Info = enc_param.mfx.FrameInfo;
    //    vpp_in_surfaces[i]->Data.Y = &vpp_in_surf_buffers[vpp_in_surf_size * i];
    //    vpp_in_surfaces[i]->Data.U = vpp_in_surfaces[i]->Data.Y + vpp_in_surf_width * vpp_in_surf_height;
    //    vpp_in_surfaces[i]->Data.V = vpp_in_surfaces[i]->Data.U + 1;
    //    vpp_in_surfaces[i]->Data.Pitch = vpp_in_surf_width;
    //}

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
    //mfxBitstream mfx_bs{ 0 };
    //mfx_bs.MaxLength = actual_param.mfx.BufferSizeInKB * 1000;
    //mfx_bs.Data = new mfxU8[mfx_bs.MaxLength];

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

        bool input_available = LoadFrameToSurface(*(vpp_in_surfaces[in_surf_ind]), in_stream);
        if (!input_available)
            break; // No input available means we reached the end of file. Go to the next stage.

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
    //while (true)
    //{
    //    // Get free task index
    //    int free_task_ind = task_pool.GetFreeIndex();
    //    if (free_task_ind == -1)
    //    {
    //        auto& first_task = task_pool.GetFirstTask();
    //        sts = session.SyncOperation(first_task.sync_point, SyncWaitTime);
    //        HandleMFXErrors(sts, "Sync Operation");

    //        out_stream.write(reinterpret_cast<char*>(first_task.bitstream.Data + first_task.bitstream.DataOffset), first_task.bitstream.DataLength);
    //        first_task.bitstream.DataLength = 0;

    //        first_task.sync_point = 0;
    //        task_pool.IncrementFirst();

    //        frame_num++;
    //        std::cout << "Frame number: " << frame_num << "\r";
    //        std::cout.flush();
    //    }
    //    else
    //    {
    //        int out_surf_ind = FindFreeSurfaceIndex(vpp_out_enc_surfaces, vpp_out_enc_surf_num);
    //        if (out_surf_ind == -1)
    //            std::cerr << "Can't find a free surface for vpp outup/encode\n";

    //        while (true)
    //        {
    //            mfxSyncPoint vpp_sync_point;
    //            sts = vpp.RunFrameVPPAsync(nullptr, vpp_out_enc_surfaces[out_surf_ind], nullptr, &vpp_sync_point);
    //            if (sts == MFX_WRN_DEVICE_BUSY)
    //            {
    //                Sleep(1);
    //                continue;
    //            }
    //            break;
    //        }

    //        if (sts == MFX_ERR_MORE_SURFACE)
    //        {
    //            // This shouldn't happen now
    //            // TODO Change later
    //            std::cerr << "Need more surface for vpp\n";
    //        }
    //        if (sts == MFX_ERR_MORE_DATA)
    //            break;

    //        while (true)
    //        {
    //            sts = encode.EncodeFrameAsync(nullptr, nullptr, &task_pool.GetTask(free_task_ind).bitstream,
    //                                                            &task_pool.GetTask(free_task_ind).sync_point);
    //            if (sts == MFX_WRN_DEVICE_BUSY)
    //            {
    //                Sleep(1);
    //                continue;
    //            }
    //            if (sts > MFX_ERR_NONE)
    //            {
    //                std::cerr << "Warning " << sts << " received during encoding\n";
    //                break;
    //            }
    //            if (sts == MFX_ERR_NOT_ENOUGH_BUFFER)
    //            {
    //                // TODO Allocate more bitstream buffer here and remove output
    //                std::cerr << "Error: Not enough buffer for encoding\n";
    //                break;
    //            }
    //            if (sts < MFX_ERR_NONE && sts != MFX_ERR_MORE_DATA)
    //            {
    //                std::cerr << "Error " << sts << " received during encoding\n";
    //                break;
    //            }
    //            break;
    //        }
    //        if (sts == MFX_ERR_MORE_DATA)
    //        {
    //            std::cerr << "More data error received while retrieving buffered encode frames\n";
    //            break;
    //        }
    //    }
    //}

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
    for (int i = 1; i < ParallelNum; i++)
    {
        session[i].DisjoinSession();
        encode[i].Close();
    }
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