#include <iostream>
#include <fstream>
#include <cmath>
#include <chrono>
#include <CL/cl.h>

static std::string readFile(const char* fileName){
    std::fstream f;
	f.open( fileName, std::ios_base::in );
    if(f.is_open() != true){
        std::cerr << "file aint open :<\n";
        return "oops!";
    }
	//assert( f.is_open() );

	std::string res;
	while( !f.eof() ) {
		char c;
		f.get( c );
		res += c;
	}
	
	f.close();

	return std::move(res);
}

//	splitmix64
uint64_t next_s(uint64_t &x) {
	uint64_t z = (x += 0x9e3779b97f4a7c15);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	return z ^ (z >> 31);
}

cl_context context;
cl_command_queue commandQueue;
cl_device_id device;

cl_program fillProgram;
cl_kernel fillKernel;
cl_program compProgram;
cl_kernel compKernel;

cl_mem a;
cl_mem b;
cl_mem randState;
cl_mem results;

size_t globalWorkSize = 256;
size_t localWorkSize = 32;

int getLocalWorkSize(size_t maxLocalSize, int globalSize){
    int result = -1;

    for(int i = 1; i <= sqrt(globalSize); i++){
        if (globalSize % i == 0){

            if(globalSize/i == i){
                if(i >= result && i <= maxLocalSize && i < (int)sqrt(globalSize)){
                    result = i;
                }
            }
            else{
                if(i >= result && i <= maxLocalSize && i < (int)sqrt(globalSize)){
                    result = i;
                }
                if(globalSize/i >= result && globalSize/i <= maxLocalSize){
                    result = globalSize/i;
                }
            }
        }
    }

    if(result == -1){
        std::cerr << "couldnt find a factor for " << globalSize << " something strage is afoot!\n";
    }

    return result;
}

bool setup(const char* _fillKernelFileName, const char* _compKernelFileName){
    cl_int platformResult = CL_SUCCESS;
    cl_uint numPlatforms = 0;
    cl_platform_id platforms[64];

    platformResult = clGetPlatformIDs( 64, platforms, &numPlatforms );

    if (platformResult != CL_SUCCESS) {
        std::cerr << "Couldnt get platform IDs!\n Failed with error(" << platformResult << ")\n";
        return false;
    }

    for(int i = 0; i < numPlatforms; i++){
        cl_device_id devices[64];
        unsigned int deviceCount;
        cl_int deviceResult = clGetDeviceIDs( platforms[i], CL_DEVICE_TYPE_GPU, 64, devices, &deviceCount);

        if( deviceResult == CL_SUCCESS){
            for (int j = 0; j < deviceCount; j++){
                unsigned long numComputeUnits;
                size_t computeUnitsLen;
                cl_int deviceInfoResult = clGetDeviceInfo( devices[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cl_uint), &numComputeUnits, &computeUnitsLen);
                cl_int atomicInfoRes = clGetDeviceInfo( devices[j], CL_DEVICE_ATOMIC_ORDER_SEQ_CST, sizeof(cl_int))
                if(deviceInfoResult == CL_SUCCESS){
                    // currently picks first device, rank by mem size ig?
                    //std::cout << "device found!!\n";
                    device = devices[j];
                    break;
                }
            }

            char version[128];
            clGetDeviceInfo(device, CL_DEVICE_VERSION, sizeof(version), version, NULL);
            printf("OpenCL version: %s\n", version);
            
        }
    }

    cl_int contextResult;
    context = clCreateContext( nullptr, 1, &device, nullptr, nullptr, &contextResult);
    if (contextResult != CL_SUCCESS){
        std::cerr << "Failed to make context!\n Failed with error (" << contextResult << ")\n";
        return false;
    }

    cl_int commandQueueResult;
    commandQueue = clCreateCommandQueue(context, device, 0, &commandQueueResult);
    if (commandQueueResult != CL_SUCCESS){
        std::cerr << "Failed to make command queue!\n Failed with error (" << commandQueueResult << ")\n";
        return false;
    }

    // create fill program and kernel
    std::string s = readFile(_fillKernelFileName);
    const char* programSource = s.c_str();
    size_t length = 0;
    cl_int programResult;
    fillProgram = clCreateProgramWithSource(context, 1, &programSource, &length, &programResult);
    if (programResult != CL_SUCCESS){
        std::cerr << "Failed to make program!(fill)\n Failed with error (" << programResult << ")\n";
        return false;
    }

    cl_int programBuildResult = clBuildProgram( fillProgram, 1, &device, "-cl-std=CL3.0\0", nullptr, nullptr);
    if (programBuildResult != CL_SUCCESS){
        char log[1024];
        size_t logLength;
        cl_int programBuildInfoResult = clGetProgramBuildInfo(fillProgram, device, CL_PROGRAM_BUILD_LOG, 1024, log, &logLength);
        
        std::cout << "log len - " << logLength << std::endl;
        std::cout <<  "log:\n" << log << std::endl << std::endl;
        if (programBuildInfoResult != CL_SUCCESS){
            std::cerr << "Failed to build program!(fill)\n Failed with error (" << programBuildInfoResult << ")\n";
            return false;
        }
    }

    cl_int kernelResult;              // this string must mach entry function name
	fillKernel = clCreateKernel( fillProgram, "fill", &kernelResult);
    if (programResult != CL_SUCCESS){
        std::cerr << "Failed to make kernel!(fill)\n Failed with error (" << programResult << ")\n";
        return false;
    }



    // create comparison program and kernel
    s = readFile(_compKernelFileName);
    programSource = s.c_str();
    length = 0;
    programResult = CL_SUCCESS;
    compProgram = clCreateProgramWithSource(context, 1, &programSource, &length, &programResult);
    if (programResult != CL_SUCCESS){
        std::cerr << "Failed to make program!(comparison)\n Failed with error (" << programResult << ")\n";
        return false;
    }

    programBuildResult = clBuildProgram( compProgram, 1, &device, "-cl-std=CL3.0\0", nullptr, nullptr);
    if (programBuildResult != CL_SUCCESS){
        char log[2048];
        size_t logLength;
        cl_int programBuildInfoResult = clGetProgramBuildInfo(compProgram, device, CL_PROGRAM_BUILD_LOG, 2048, log, &logLength);

        std::cout << "log len - " << logLength << std::endl;
        std::cout <<  "log:\n" << log << std::endl << "*end of log*" << std::endl;

        if (programBuildInfoResult != CL_SUCCESS){
            std::cerr << "Failed to build program!(comparison)\n Failed with error (" << programBuildInfoResult << ")\n";
            return false;
        }
    }

    //invalid value could be fuckinnnnn ummmm the value of the global tally

    kernelResult;              // this string must mach entry function name
	compKernel = clCreateKernel( compProgram, "comp", &kernelResult);
    if (kernelResult != CL_SUCCESS){
        std::cerr << "Failed to make kernel!(comparison)\n Failed with error (" << kernelResult << ")\n";
        return false;
    }


    return true;
}

void cleanup(){
    clReleaseMemObject(a);
    clReleaseMemObject(b);
    clReleaseMemObject(randState);
    clReleaseMemObject(results);
    clReleaseKernel(fillKernel);
    clReleaseProgram(fillProgram);
    clReleaseKernel(compKernel);
    clReleaseProgram(compProgram);
    clReleaseCommandQueue(commandQueue);
    clReleaseContext(context);
    //clReleaseDevice(device); // not needed??
}

/*
a, b buffers will be global
for now generate all randomiser state on cpu and pass it as another buffer
one kernel will generate x amount of numbers into a and b <- im here rn
another kernel will compare a section of b with all of a and record results into the global tally
    copy section of b into private memory actually, copare wit all of a (hopefully we get a bunch of cache hits)
    keep a private tally, then at the very end update the global tally

after this is done further changes can be done, i just wanna get this done first
*/

int main(int argc, char* argv[])
{
    uint64_t rolls;
    if( argc == 1){
        std::cout << "not enough arguments, ya need to add how many rolls dum dum\n";
        return 1;
    }
    rolls = atol(argv[1]);

    if( argc == 3){
        if( *argv[2] == 'k'){
            rolls *= 1000;
        }
        else if (*argv[2] == 'm'){
            rolls *= 1000000;
        }
        else{
            std::cerr << "bad mult dumbass\n";
            return 1;
        }
    }

    if (setup("../fillKernel.cl", "../compKernel.cl") != true){
        std::cerr << "Failed to set up open cl\n";
        cleanup();
        return 1;
    }

    int sqRolls = sqrt(rolls);
    /* std::cout << "sqrolls = " << sqRolls << std::endl;
    return 0; */
    ulong s[sqRolls*4];

    // initialise randomiser state
    auto now = std::chrono::system_clock::now();
	auto epoch = now.time_since_epoch();
	uint64_t count = epoch.count();
	for(int i = 0; i < sqRolls*4; i++){
		s[i] = next_s(count);
	}

    // setup buffers
    uint aData[sqRolls];
    uint bData[sqRolls];

    cl_int randStateResult;
    randState = clCreateBuffer(context, CL_MEM_READ_ONLY, sqRolls * 4 * sizeof(ulong), nullptr, &randStateResult);
    if (randStateResult != CL_SUCCESS){
        std::cerr << "Failed to create buffer rand state!\n Failed with error (" << randStateResult << ")\n";
        return 1;
    }
    cl_int enqueueStateResult = clEnqueueWriteBuffer(commandQueue, randState, CL_TRUE, 0, sqRolls * 4 * sizeof(ulong), s, 0, nullptr, nullptr);
    if (enqueueStateResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer write (state)!\n Failed with error (" << enqueueStateResult << ")\n";
        return 1;
    }


    cl_int aDataResult;
    a = clCreateBuffer(context, CL_MEM_READ_WRITE, sqRolls * sizeof(uint), nullptr, &aDataResult);
    if (aDataResult != CL_SUCCESS){
        std::cerr << "Failed to create buffer a!\n Failed with error (" << aDataResult << ")\n";
        return 1;
    }
    // TODO: consider switching out write buffer as theyll be empty rn and thats just fine but idk
    cl_int enqueueAResult = clEnqueueWriteBuffer(commandQueue, a, CL_TRUE, 0, sqRolls * sizeof(uint), aData, 0, nullptr, nullptr);
    if (enqueueAResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer write (a)!\n Failed with error (" << enqueueAResult << ")\n";
        return 1;
    }


    cl_int bDataResult;
    b = clCreateBuffer(context, CL_MEM_READ_WRITE, sqRolls * sizeof(uint), nullptr, &bDataResult);
    if (bDataResult != CL_SUCCESS){
        std::cerr << "Failed to create buffer b!\n Failed with error (" << bDataResult << ")\n";
        return 1;
    }
    cl_int enqueueBResult = clEnqueueWriteBuffer(commandQueue, b, CL_TRUE, 0, sqRolls * sizeof(uint), bData, 0, nullptr, nullptr);
    if (enqueueBResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer write (b)!\n Failed with error (" << enqueueBResult << ")\n";
        return 1;
    }


    cl_int tallyBufferResult;
    cl_mem tally = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 20 * sizeof(uint64_t), nullptr, &tallyBufferResult);
    if (tallyBufferResult != CL_SUCCESS){
        std::cerr << "Failed to create tally buffer!\n Failed with error (" << enqueueAResult << ")\n";
        return 1;
    }


    // query max local work group size
    size_t sumin;
    size_t maxLocalWorkSize;
    cl_int lWorkSizeResult = clGetKernelWorkGroupInfo(fillKernel, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &maxLocalWorkSize, &sumin); 
    if ( lWorkSizeResult != CL_SUCCESS){
        std::cerr << "Failed to query work group info!\n Failed with error (" << lWorkSizeResult << ")\n";
        return 1;
    }
    std::cout << "max local work size - " << maxLocalWorkSize << std::endl;

    // set work sizes
    globalWorkSize = sqRolls; // each thread will generate one number into a
    localWorkSize = getLocalWorkSize(maxLocalWorkSize, globalWorkSize);
    std::cout << "global size - " << globalWorkSize << " local size - " << localWorkSize << std::endl;



    // fill a
    // set kernel arguments
    cl_int kernalArgStateResult = clSetKernelArg(fillKernel, 0, sizeof(cl_mem), &randState);
    if (kernalArgStateResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(state)!\n Failed with error (" << kernalArgStateResult << ")\n";
        return 1;
    }
    cl_int kernalArgAResult = clSetKernelArg(fillKernel, 1, sizeof(cl_mem), &a);
    if (kernalArgAResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(a)!\n Failed with error (" << kernalArgAResult << ")\n";
        return 1;
    }

    //enqueu kernel
    cl_int enqueueKernelResult = clEnqueueNDRangeKernel(commandQueue, fillKernel, 1, 0, &globalWorkSize, &localWorkSize, 0, nullptr, nullptr);
    if (enqueueKernelResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue kernel(fill a)!\n Failed with error (" << enqueueKernelResult << ")\n";
        return 1;
    }

    clFinish(commandQueue);

    // fill b
    // set kernel arguments
    kernalArgStateResult = clSetKernelArg(fillKernel, 0, sizeof(cl_mem), &randState);
    if (kernalArgStateResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(state)!\n Failed with error (" << kernalArgStateResult << ")\n";
        return 1;
    }
    cl_int kernalArgBResult = clSetKernelArg(fillKernel, 1, sizeof(cl_mem), &b);
    if (kernalArgBResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(b)!\n Failed with error (" << kernalArgBResult << ")\n";
        return 1;
    }
    // enqueue kernel
    enqueueKernelResult = clEnqueueNDRangeKernel(commandQueue, fillKernel, 1, 0, &globalWorkSize, &localWorkSize, 0, nullptr, nullptr);
    if (enqueueKernelResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue kernel(fill b)!\n Failed with error (" << enqueueKernelResult << ")\n";
        return 1;
    }

    clFinish(commandQueue);

    kernalArgAResult = clSetKernelArg(compKernel, 0, sizeof(cl_mem), &a);
    if (kernalArgAResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(a for comp)!\n Failed with error (" << kernalArgAResult << ")\n";
        return 1;
    }
    cl_int kernelArgASizeResult = clSetKernelArg(compKernel, 1, sizeof(uint), &sqRolls);
    if (kernelArgASizeResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(a size)!\n Failed with error (" << kernelArgASizeResult << ")\n";
        return 1;
    }
    kernalArgBResult = clSetKernelArg(compKernel, 2, sizeof(cl_mem), &b);
    if (kernalArgBResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(b for comp)!\n Failed with error (" << kernalArgBResult << ")\n";
        return 1;
    }
    // local buffer
    cl_int kernalArgLTallyResult = clSetKernelArg(compKernel, 3, localWorkSize * 20 * sizeof(ulong), nullptr);
    if (kernalArgLTallyResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(local tally)!\n Failed with error (" << kernalArgLTallyResult << ")\n";
        return 1;
    }

    cl_int kernalArgGTallyResult = clSetKernelArg(compKernel, 4, sizeof(cl_mem), &tally);
    if (kernalArgGTallyResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(global tally)!\n Failed with error (" << kernalArgGTallyResult << ")\n";
        return 1;
    }

    // enqueue kernel
    enqueueKernelResult = clEnqueueNDRangeKernel(commandQueue, compKernel, 1, 0, &globalWorkSize, &localWorkSize, 0, nullptr, nullptr);
    if (enqueueKernelResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue kernel!\n Failed with error (" << enqueueKernelResult << ")\n";
        return 1;
    }

    // enqueue reads
    cl_int aReadRes = clEnqueueReadBuffer(commandQueue, a, CL_TRUE, 0, sqRolls * sizeof(uint), aData, 0, nullptr, nullptr);
    if (aReadRes != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer read!(a)\n Failed with error (" << aReadRes << ")\n";
        return 1;
    }
    cl_int bReadRes = clEnqueueReadBuffer(commandQueue, b, CL_TRUE, 0, sqRolls * sizeof(uint), bData, 0, nullptr, nullptr);
    if (aReadRes != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer read!(b)\n Failed with error (" << aReadRes << ")\n";
        return 1;
    }

    clFinish(commandQueue);

    // display results
    std::cout << "results!: \n";
    for(int i = 0; i < sqRolls; i++){
        std::cout << i << "\ta:" << aData[i] << "\t\tb:" << bData[i] << std::endl;
    }
 
    cleanup();

    return 0;
}