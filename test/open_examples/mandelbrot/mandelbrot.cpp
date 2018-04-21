/*
 * Copyright (c) 2017, Intel Corporation
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included
 * in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
 * OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

// The only CM runtime header file that you need is cm_rt.h.
// It includes all of the CM runtime.
#include "cm_rt.h"

// Includes bitmap_helpers.h for bitmap file open/save/compare operations.
#include "common/bitmap_helpers.h"

// Include cm_rt_helpers.h to convert the integer return code returned from
// the CM runtime to a meaningful string message.
#include "common/cm_rt_helpers.h"

// Includes isa_helpers.h to load the ISA file generated by the CM compiler.
#include "common/isa_helpers.h"

#define OUTPUT_BUFFER (0)

#define    WIDTH   800
#define    HEIGHT  600 

#define    CRUNCH  512
#define    SCALE   0.004 
#define    XOFF    -2.09798 
#define    YOFF    -1.19798 

int outbuff[WIDTH*HEIGHT];

// kernel prototype
void mandelbrot(SurfaceIndex output_index, int crunch, float xOff, float yOff, float scale);

static char* k_name = "mandelbrot";

int runkernel(char* app_name, unsigned char *dst,
              int crunch, float xOff, float yOff, float scale)
{
    SurfaceIndex * index = NULL;

    CmDevice* pCmDev = NULL;
    UINT version = 0;
    cm_result_check( ::CreateCmDevice( pCmDev, version ) );

    std::string isa_code = cm::util::isa::loadFile("mandelbrot_genx.isa");
    CmProgram* program = NULL;
    if( isa_code.size() == 0 ) {
        printf("Error: Open mandelbrot.isa fail!\n");
        exit(1);
    }
    cm_result_check(pCmDev->LoadProgram(const_cast<char*>(isa_code.data()),
                                isa_code.size(), program));

    SurfaceIndex* pOutputIndex=NULL;
    CmSurface2D* surf1=NULL;
    pCmDev->CreateSurface2D( WIDTH, HEIGHT, CM_SURFACE_FORMAT_A8R8G8B8, surf1);
    surf1->GetIndex(pOutputIndex);

    CmKernel* kernel0 = NULL;

    pCmDev->CreateKernel(program, k_name, kernel0);

    kernel0->SetKernelArg( 0, sizeof( SurfaceIndex ), pOutputIndex );
    kernel0->SetKernelArg( 1, sizeof( int ), &crunch );
    kernel0->SetKernelArg( 2, sizeof( float ), &xOff );
    kernel0->SetKernelArg( 3, sizeof( float ), &yOff );
    kernel0->SetKernelArg( 4, sizeof( float ), &scale );


    CmQueue* pCmQueue = NULL;
    pCmDev->CreateQueue( pCmQueue );
    CmTask *pKernelArray = NULL;
    cm_result_check( pCmDev->CreateTask(pKernelArray) );
    cm_result_check( pKernelArray->AddKernel(kernel0) );
   
    CmThreadGroupSpace *pts = NULL;
    cm_result_check( pCmDev->CreateThreadGroupSpace(1, 1, WIDTH/8, HEIGHT/2, pts) );
    //pts->SelectMediaWalkingPattern(CM_WALK_WAVEFRONT);
    //pts->SelectThreadDependencyPattern(CM_NONE_DEPENDENCY);

    CmEvent* sync_event = NULL;
    unsigned long time_out = (-1);
    // warm up
    cm_result_check(pCmQueue->EnqueueWithGroup(pKernelArray, sync_event, pts));
    cm_result_check(sync_event->WaitForTaskFinished(time_out));
    // Start timer.
    double start = getTimeStamp();

    // Launches the task on the GPU.
    UINT64 kernel_time_in_ns = 0;
    unsigned num_iters = 1000;

    for (int i = 0; i < num_iters; ++i) {
        UINT64 time_in_ns = 0;
        cm_result_check(pCmQueue->EnqueueWithGroup(pKernelArray, sync_event, pts));
        cm_result_check(sync_event->WaitForTaskFinished(time_out));
        cm_result_check(sync_event->GetExecutionTime(time_in_ns));
        kernel_time_in_ns += time_in_ns;
    }

    // End timer.
    double end = getTimeStamp();

    float total_time = (end - start) * 1000.0f / num_iters;
    float kernel_time = kernel_time_in_ns / 1000000.0f / num_iters;

    pCmDev->DestroyTask(pKernelArray);
    pCmDev->DestroyThreadGroupSpace(pts);

    surf1->ReadSurface((unsigned char*) dst, sync_event);

    printf("Mandelbrot %d x %d max-iter %d exec time %fms kernal_time %fms\n",
           WIDTH, HEIGHT, crunch, total_time, kernel_time); 

    FILE* dumpfile = fopen("mandelbrot.ppm", "w");
    if (!dumpfile) {
        printf("Error: cannot dump file!\n");
        exit(1);
    }
    fprintf(dumpfile, "P6\n");
    fprintf(dumpfile, "%u %u\n", WIDTH, HEIGHT);
    fprintf(dumpfile, "%u\n", 255);
    fclose(dumpfile);
    dumpfile = fopen("mandelbrot.ppm", "ab");
    for (int32_t i = 0; i < WIDTH * HEIGHT; ++i)
    {
        fwrite(&dst[i*4], sizeof(char), 1, dumpfile);
        fwrite(&dst[i*4+1], sizeof(char), 1, dumpfile);
        fwrite(&dst[i*4+2], sizeof(char), 1, dumpfile);
    }
    fclose(dumpfile);

    ::DestroyCmDevice( pCmDev );

    return 0;
}


int main() {
    int status = 0;

    memset(outbuff, 0, WIDTH * HEIGHT * sizeof(int));
    status |= runkernel("simd_mandelbrot", (unsigned char *)outbuff, CRUNCH, XOFF, YOFF, SCALE);

    if (status) {
        printf("FAILED\n");
        return -1;
    }
    printf("PASSED\n");
    return 0;
}
