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

uint64_t next_s(uint64_t &x) {
	uint64_t z = (x += 0x9e3779b97f4a7c15);
	z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9;
	z = (z ^ (z >> 27)) * 0x94d049bb133111eb;
	return z ^ (z >> 31);
}

//random engine state
uint64_t s[4];

cl_context context;
cl_command_queue commandQueue;
cl_device_id device;

cl_program program;
cl_kernel kernel;

cl_mem a;
cl_mem b;
cl_mem results;

size_t globalWorkSize = 256;
size_t localWorkSize = 64;

bool setup(const char* _kernelFileName){
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
                if(deviceInfoResult == CL_SUCCESS){
                    // currently picks first device, rank by mem size ig?
                    //std::cout << "device found!!\n";
                    device = devices[j];
                    break;
                }
            }
            
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

    std::string s = readFile(_kernelFileName);
    const char* programSource = s.c_str();
    size_t length = 0;
    cl_int programResult;
    program = clCreateProgramWithSource(context, 1, &programSource, &length, &programResult);
    if (programResult != CL_SUCCESS){
        std::cerr << "Failed to make program!\n Failed with error (" << programResult << ")\n";
        return false;
    }

    cl_int programBuildResult = clBuildProgram( program, 1, &device, "", nullptr, nullptr);
    if (programBuildResult != CL_SUCCESS){
        char log[256];
        size_t logLength;
        cl_int programBuildInfoResult = clGetProgramBuildInfo(program, device, CL_PROGRAM_BUILD_LOG, 256, log, &logLength);
        // just consider using cassert god damn
        
        std::cout <<  "log:\n" << log << std::endl << std::endl;
        if (programBuildInfoResult != CL_SUCCESS){
            std::cerr << "Failed to build program!\n Failed with error (" << programBuildInfoResult << ")\n";
            return false;
        }
    }

    cl_int kernelResult;              // this string must mach entry function name
	kernel = clCreateKernel( program, "arr_sum", &kernelResult);
    if (programResult != CL_SUCCESS){
        std::cerr << "Failed to make kernel!\n Failed with error (" << programResult << ")\n";
        return false;
    }

    return true;
}

void cleanup(){
    clReleaseMemObject(a);
    clReleaseMemObject(b);
    clReleaseMemObject(results);
    clReleaseKernel(kernel);
    clReleaseProgram(program);
    clReleaseCommandQueue(commandQueue);
    clReleaseContext(context);
    //clReleaseDevice(device); // not needed??
}

int main(int argc, char* argv[])
{
    uint64_t rolls;
    try
    {
        rolls = atol(argv[1]);
    }
    catch(const std::exception& e)
    {
        std::cerr << "Couldnt get the number of rolls\n" << e.what() << '\n';
    }

    if (setup("kernel.cl") != true){
        std::cerr << "Failed to set up open cl\n";
        return 1;
    }

    // initialise randomiser state
    auto now = std::chrono::system_clock::now();
	auto epoch = now.time_since_epoch();
	uint64_t count = epoch.count();
	for(int i = 0; i < 4; i++){
		s[i] = next_s(count);
	}

    // setup buffers
    int sqRolls = sqrt(rolls);
    uint8_t aData[sqRolls];
    uint8_t bData[sqRolls];

    cl_int aDataResult;
    cl_mem a = clCreateBuffer(context, CL_MEM_READ_WRITE, sqRolls * sizeof(uint8_t), nullptr, &aDataResult);
    if (aDataResult != CL_SUCCESS){
        std::cerr << "Failed to create buffer a!\n Failed with error (" << aDataResult << ")";
        return 1;
    }
    // TODO: consider switching out write buffer as theyll be empty rn and thats just fine but idk
    cl_int enqueueAResult = clEnqueueWriteBuffer(commandQueue, a, CL_TRUE, 0, sqRolls * sizeof(uint8_t), aData, 0, nullptr, nullptr);
    if (enqueueAResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer write (a)!\n Failed with error (" << enqueueAResult << ")";
        return 1;
    }

    cl_int bDataResult;
    cl_mem b = clCreateBuffer(context, CL_MEM_READ_WRITE, sqRolls * sizeof(uint8_t), nullptr, &bDataResult);
    if (bDataResult != CL_SUCCESS){
        std::cerr << "Failed to create buffer b!\n Failed with error (" << bDataResult << ")";
        return 1;
    }
    cl_int enqueueBResult = clEnqueueWriteBuffer(commandQueue, b, CL_TRUE, 0, sqRolls * sizeof(uint8_t), bData, 0, nullptr, nullptr);
    if (enqueueBResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer write (b)!\n Failed with error (" << enqueueBResult << ")";
        return 1;
    }

    cl_int tallyBufferResult;
    cl_mem tally = clCreateBuffer(context, CL_MEM_WRITE_ONLY, 20 * sizeof(uint64_t), nullptr, &tallyBufferResult);
    if (tallyBufferResult != CL_SUCCESS){
        std::cerr << "Failed to create tally buffer!\n Failed with error (" << enqueueAResult << ")";
        return 1;
    }

    // set kernel arguments
    cl_int kernalArgAResult = clSetKernelArg(kernel, 0, sizeof(cl_mem), &a);
    if (kernalArgAResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(a)!\n Failed with error (" << kernalArgAResult << ")";
        return 1;
    }
    cl_int kernalArgBResult = clSetKernelArg(kernel, 1, sizeof(cl_mem), &b);
    if (kernalArgBResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(b)!\n Failed with error (" << kernalArgBResult << ")";
        return 1;
    }
    cl_int kernalArgResResult = clSetKernelArg(kernel, 2, sizeof(cl_mem), &results);
    if (kernalArgResResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(res)!\n Failed with error (" << kernalArgResResult << ")";
        return 1;
    }

    // command submission

    cl_int enqueueKernelResult = clEnqueueNDRangeKernel(commandQueue, kernel, 1, 0, &globalWorkSize, &localWorkSize, 0, nullptr, nullptr);
    if (enqueueKernelResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue kernel!\n Failed with error (" << enqueueKernelResult << ")";
        return 1;
    }

    float resultsData[256];
    cl_int readResultsResult = clEnqueueReadBuffer(commandQueue, results, CL_TRUE, 0, 256 * sizeof(float), resultsData, 0, nullptr, nullptr);
    if (readResultsResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer read!\n Failed with error (" << readResultsResult << ")";
        return 1;
    }

    // flush command buffer and awit completion
    clFinish(commandQueue);

    // display results
    std::cout << "results!: \n";
    for(int i = 0; i < 256; i++){
        std::cout << resultsData[i] << "\n";
    }

    cleanup();

    return 0;
}