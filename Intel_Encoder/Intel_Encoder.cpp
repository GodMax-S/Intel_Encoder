// Intel_Encoder.cpp : Defines the entry point for the console application.
//

#include "stdafx.h"
#include <iostream>
#include <chrono>

#include "IMSDK_Coder.h"


int main()
{
    using namespace std::chrono;
    steady_clock::time_point t1 = steady_clock::now();
    
    std::string output_directory = "E:/University/Semester VIII/Diploma/";

    std::vector<std::string> output_files = { output_directory + "my_output1.h264", output_directory + "my_output2.h264" };
    try
    {
        IMSDK_Coder::Test("E:/University/Semester VIII/Diploma/test.yuv", output_files);
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

