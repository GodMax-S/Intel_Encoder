// Intel_Encoder.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <iostream>
#include <chrono>
#include <fstream>
#include <string>

#include "IMSDK_Coder.h"
#include "Util.h"

const int OriginalWidth = 854;
const int OriginalHeight = 480;
const int VideoWidth[] = { 854, 854};
const int VideoHeight[] = { 480, 480};
const int AsyncDepth = 4;

struct InputParameters
{
    std::string file_name;
    int width, height;
    InputParameters() : file_name(), width(0), height(0) {}
};

struct PipelineParameters
{
    std::string file_name;
    int frameN, frameD;
    int height, width;
    int bitrate;
    PipelineParameters() : file_name(), frameN(0), frameD(0), height(0), width(0), bitrate(0) {}
};

struct CodingParameters
{
    enum class MemoryType { None, Video, System };
    enum class ImplementationType { None, Any, Software, Hardware };

    InputParameters input_param;
    MemoryType memory_type;
    ImplementationType impl_type;
    std::vector<PipelineParameters> pipeline_params;
    CodingParameters() : input_param(), memory_type(MemoryType::None), impl_type(ImplementationType::None), pipeline_params() {}
};

std::vector<mfxVideoParam> GetVPPParmas(const CodingParameters& param)
{
    auto& pipelines = param.pipeline_params;
    std::vector<mfxVideoParam> vpp_param(pipelines.size());
    for (int i = 0; i < vpp_param.size(); i++)
    {
        // Input parameters
        vpp_param[i].vpp.In.FourCC = MFX_FOURCC_NV12;
        vpp_param[i].vpp.In.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
        vpp_param[i].vpp.In.CropX = 0;
        vpp_param[i].vpp.In.CropY = 0;
        vpp_param[i].vpp.In.CropW = param.input_param.width;
        vpp_param[i].vpp.In.CropH = param.input_param.height;
        vpp_param[i].vpp.In.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
        vpp_param[i].vpp.In.FrameRateExtN = pipelines[i].frameN;
        vpp_param[i].vpp.In.FrameRateExtD = pipelines[i].frameD;
        vpp_param[i].vpp.In.Width = Util::RoundUp(vpp_param[i].vpp.In.CropW, 16);
        vpp_param[i].vpp.In.Height = Util::RoundUp(vpp_param[i].vpp.In.CropH, 16);

        // Output parameters
        vpp_param[i].vpp.Out.FourCC = MFX_FOURCC_NV12;
        vpp_param[i].vpp.Out.ChromaFormat = MFX_CHROMAFORMAT_YUV420;
        vpp_param[i].vpp.Out.CropX = 0;
        vpp_param[i].vpp.Out.CropY = 0;
        vpp_param[i].vpp.Out.CropW = pipelines[i].width;
        vpp_param[i].vpp.Out.CropH = pipelines[i].height;
        vpp_param[i].vpp.Out.PicStruct = MFX_PICSTRUCT_PROGRESSIVE;
        vpp_param[i].vpp.Out.FrameRateExtN = pipelines[i].frameN;
        vpp_param[i].vpp.Out.FrameRateExtD = pipelines[i].frameD;
        vpp_param[i].vpp.Out.Width = Util::RoundUp(vpp_param[i].vpp.Out.CropW, 16);
        vpp_param[i].vpp.Out.Height = Util::RoundUp(vpp_param[i].vpp.Out.CropH, 16);

        if (param.memory_type == CodingParameters::MemoryType::Video)
            vpp_param[i].IOPattern = MFX_IOPATTERN_IN_VIDEO_MEMORY | MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
        else
            vpp_param[i].IOPattern = MFX_IOPATTERN_IN_SYSTEM_MEMORY | MFX_IOPATTERN_OUT_OPAQUE_MEMORY;
        vpp_param[i].AsyncDepth = AsyncDepth;
    }
    return vpp_param;
}

std::vector<mfxVideoParam> GetEncodeParams(const CodingParameters& param, const std::vector<mfxVideoParam>& vpp_param)
{
    auto& pipelines = param.pipeline_params;
    std::vector<mfxVideoParam> enc_param(pipelines.size());
    for (int i = 0; i < enc_param.size(); i++)
    {
        enc_param[i].mfx.CodecId = MFX_CODEC_AVC;
        enc_param[i].mfx.TargetUsage = MFX_TARGETUSAGE_BALANCED; // quality-to-speed balance
        enc_param[i].mfx.TargetKbps = pipelines[i].bitrate; // bitrate
        enc_param[i].mfx.RateControlMethod = MFX_RATECONTROL_VBR;
        enc_param[i].mfx.FrameInfo.FrameRateExtN = vpp_param[i].vpp.In.FrameRateExtN;
        enc_param[i].mfx.FrameInfo.FrameRateExtD = vpp_param[i].vpp.In.FrameRateExtD;
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
    }
    return enc_param;
}

bool ReadFrameFromFile(int width, int height, int pitch, char*& data, std::fstream& in_stream)
{
    std::unique_ptr<char[]> safe_data(new char[pitch*height * 12 / 8]); // 12 bits per pixel in NV12 format
    mfxU8* ptr = reinterpret_cast<mfxU8*>(safe_data.get());

    for (int i = 0; i < height; i++)
    {
        in_stream.read(reinterpret_cast<char*>(ptr + i * pitch), width);
        if (in_stream.gcount() != width)
        {
            return false;
        }
    }
    ptr += pitch*height;
    mfxU8 buf[2048];
    width /= 2;
    height /= 2;
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
    data = safe_data.release();
    return true;
}

CodingParameters ParseParameters(int argv, char** argc)
{
    CodingParameters param;
    param.pipeline_params.emplace_back();
    int curr_pipeline = 0;
    for (int i = 1; i < argv; i++)
    {
        if (!strcmp(argc[i], "-sw"))
        {
            if (param.impl_type != CodingParameters::ImplementationType::None)
                throw std::runtime_error("Invalid option \"-sw\". Implementation type is already set.");
            param.impl_type = CodingParameters::ImplementationType::Software;
        }
        else if (!strcmp(argc[i], "-hw"))
        {
            if (param.impl_type != CodingParameters::ImplementationType::None)
                throw std::runtime_error("Invalid option \"-hw\". Implementation type is already set.");
            param.impl_type = CodingParameters::ImplementationType::Hardware;
        }
        else if (!strcmp(argc[i], "-aw"))
        {
            if (param.impl_type != CodingParameters::ImplementationType::None)
                throw std::runtime_error("Invalid option \"-aw\". Implementation type is already set.");
            param.impl_type = CodingParameters::ImplementationType::Any;
        }
        else if (!strcmp(argc[i], "-sz"))
        {
            if (param.input_param.width != 0 || param.input_param.height != 0)
                throw std::runtime_error("Invalid option \"-sz\". Input size is already set.");
            int width = 0, height = 0;
            if (++i >= argv)
                throw std::runtime_error("No argument for \"-sz\" option given.");
            if (2 != sscanf_s(argc[i], "%dx%d", &width, &height) || (width <= 0) || (height <= 0))
                throw std::runtime_error("Incorrect agrument for \"-sz\" option.");
            param.input_param.width = width;
            param.input_param.height = height;
        }
        else if (!strcmp(argc[i], "-vm"))
        {
            if (param.memory_type != CodingParameters::MemoryType::None)
                throw std::runtime_error("Invalid option \"-vm\". Memory type is already set.");
            param.memory_type = CodingParameters::MemoryType::Video;
        }
        else if (!strcmp(argc[i], "-sm"))
        {
            if (param.memory_type != CodingParameters::MemoryType::None)
                throw std::runtime_error("Invalid option \"-sm\". Memory type is already set.");
            param.memory_type = CodingParameters::MemoryType::System;
        }
        else if (!strcmp(argc[i], "-pl"))
        {
            if (param.pipeline_params[curr_pipeline].frameD == 0 || param.pipeline_params[curr_pipeline].frameN == 0 ||
                param.pipeline_params[curr_pipeline].width == 0 || param.pipeline_params[curr_pipeline].height == 0 ||
                param.pipeline_params[curr_pipeline].file_name == "")
                throw std::runtime_error("Invalid option \"-pl\". Some of the pipeline parameters have not been specified.");
            param.pipeline_params.emplace_back();
            curr_pipeline++;
        }
        else if (!strcmp(argc[i], "-psz"))
        {
            if (param.pipeline_params[curr_pipeline].width != 0 || param.pipeline_params[curr_pipeline].height != 0)
                throw std::runtime_error("Invalid option \"-psz\". Output size for this pipeline is already set.");
            int width = 0, height = 0;
            if (++i >= argv)
                throw std::runtime_error("No argument for \"-psz\" option given.");
            if (2 != sscanf_s(argc[i], "%dx%d", &width, &height) || (width <= 0) || (height <= 0))
                throw std::runtime_error("Incorrect agrument for \"-psz\" option.");
            param.pipeline_params[curr_pipeline].width = width;
            param.pipeline_params[curr_pipeline].height = height;
        }
        else if (!strcmp(argc[i], "-pfr"))
        {
            if (param.pipeline_params[curr_pipeline].frameN != 0 || param.pipeline_params[curr_pipeline].frameD != 0)
                throw std::runtime_error("Invalid option \"-pfr\". Output framerate for this pipeline is already set.");
            int frameN = 0, frameD = 0;
            if (++i >= argv)
                throw std::runtime_error("No argument for \"-pfr\" option given.");
            if (2 != sscanf_s(argc[i], "%d/%d", &frameN, &frameD) || (frameN <= 0) || (frameD <= 0))
                throw std::runtime_error("Incorrect agrument for \"-pfr\" option.");
            param.pipeline_params[curr_pipeline].frameN = frameN;
            param.pipeline_params[curr_pipeline].frameD = frameD;
        }
        else if (!strcmp(argc[i], "-pb"))
        {
            if (param.pipeline_params[curr_pipeline].bitrate != 0)
                throw std::runtime_error("Invalid option \"-pb\". Output bitrate for this pipeline is already set.");
            int bitrate;
            if (++i >= argv)
                throw std::runtime_error("No argument for \"-pb\" option given.");
            if (1 != sscanf_s(argc[i], "%d", &bitrate) || (bitrate <= 0))
                throw std::runtime_error("Incorrect agrument for \"-pb\" option.");
            param.pipeline_params[curr_pipeline].bitrate = bitrate;
        }
        else if (!strcmp(argc[i], "-i"))
        {
            if (param.input_param.file_name != "")
                throw std::runtime_error("Invalid option \"-i\". Input file is already set.");
            int frameN = 0, frameD = 0;
            if (++i >= argv)
                throw std::runtime_error("No argument for \"-i\" option given.");
            param.input_param.file_name = argc[i];
        }
        else if (!strcmp(argc[i], "-o"))
        {
            if (param.pipeline_params[curr_pipeline].file_name != "")
                throw std::runtime_error("Invalid option \"-o\". Output file for this pipeline is already set.");
            int frameN = 0, frameD = 0;
            if (++i >= argv)
                throw std::runtime_error("No argument for \"-o\" option given.");
            param.pipeline_params[curr_pipeline].file_name = argc[i];
        }
        else
            throw std::runtime_error("Invalid option \"" + std::string(argc[i]) + "\".");
    }
    if (param.impl_type == CodingParameters::ImplementationType::None)
        param.impl_type = CodingParameters::ImplementationType::Any;
    if (param.memory_type == CodingParameters::MemoryType::None)
        param.memory_type = CodingParameters::MemoryType::Video;
    if (param.pipeline_params[curr_pipeline].frameD == 0 || param.pipeline_params[curr_pipeline].frameN == 0 ||
        param.pipeline_params[curr_pipeline].width == 0 || param.pipeline_params[curr_pipeline].height == 0 ||
        param.pipeline_params[curr_pipeline].file_name == "")
        throw std::runtime_error("Some of the last pipeline options have not been specified.");

    return param;
}

int main(int argv, char** argc)
{
    //for (int i = 0; i < argv; i++)
    //{
    //    std::cout << argc[i] << "\n";
    //}

    using namespace std::chrono;
    steady_clock::time_point t1 = steady_clock::now();
    CodingParameters param;
    try
    {
        param = ParseParameters(argv, argc);
    }
    catch (std::runtime_error& e)
    {
        std::cerr << e.what() << std::endl;
        return 1;
    }

    bool use_video_memory = (param.memory_type == CodingParameters::MemoryType::Video) ? true : false;
    mfxIMPL impl = 0;
    if (param.impl_type == CodingParameters::ImplementationType::Any)
        impl = MFX_IMPL_AUTO_ANY;
    else if (param.impl_type == CodingParameters::ImplementationType::Hardware)
        impl = MFX_IMPL_HARDWARE_ANY;
    else
        impl = MFX_IMPL_SOFTWARE;
    
    std::string& input_file = param.input_param.file_name;
    std::vector<int> frame_num(param.pipeline_params.size());
    IMSDK_Coder coder;
    std::fstream input_stream(input_file, std::ios::in | std::ios::binary);
    std::vector<std::fstream> output_streams(param.pipeline_params.size());
    for (int i = 0; i < param.pipeline_params.size(); i++)
    {
        output_streams[i].open(param.pipeline_params[i].file_name, std::ios::out | std::ios::binary);
    }
    try
    {
        auto vpp_params = GetVPPParmas(param);
        auto enc_params = GetEncodeParams(param, vpp_params);
        mfxVersion ver;
        ver.Major = 1;
        ver.Minor = 0;
        coder.InitializeCoder(use_video_memory, param.pipeline_params.size(), impl, ver, vpp_params, enc_params);
        
        while (true)
        {
            std::unique_ptr<char> safe_data;
            char* raw_data = nullptr;
            if (!ReadFrameFromFile(OriginalWidth, OriginalHeight, OriginalWidth, raw_data, input_stream))
                break;
            safe_data.reset(raw_data);
            auto res = coder.EncodeFrame(safe_data.get(), OriginalWidth);
            for (int i = 0; i < res.size(); i++)
            {
                if (res[i].size() > 0)
                {
                    frame_num[i]++;
                    output_streams[i].write(res[i].data(), res[i].size());
                }
            }
            std::cout << "Frame num ";
            for (int i = 0; i < frame_num.size(); i++)
            {
                std::cout << frame_num[i] << "|";
            }
            std::cout << "\r";
        }
        auto res = coder.EncodeFinal();
        for (int i = 0; i < res.size(); i++)
        {
            for (int j = 0; j < res[i].size(); j++)
            {
                output_streams[i].write(res[i][j].data(), res[i][j].size());
            }
        }
    }
    catch (std::exception& e)
    {
        std::cerr << "Exception caught: " << e.what() << std::endl;
    }

    steady_clock::time_point t2 = steady_clock::now();
    milliseconds time_span = duration_cast<milliseconds>(t2 - t1);
    std::cout << "Time spent: " << time_span.count() << " ms" << std::endl;
    getchar();
    return 0;
}

