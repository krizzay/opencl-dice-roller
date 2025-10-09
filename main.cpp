#include <iostream>
#include <string>
#include <fstream>
#include <vector>
#include <cmath>
#include <chrono>
#include <bitset>
#include <array>
#include <numeric>
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

static inline uint64_t rotl(const uint64_t x, int k) {
	return (x << k) | (x >> (64 - k));
}

//xoroshiro
uint64_t next_x(uint64_t* s) {
	const uint64_t result = s[0] + s[3];

	const uint64_t t = s[1] << 17;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];

	s[2] ^= t;

	s[3] = rotl(s[3], 45);

	return result;
}

cl_context context;
cl_command_queue commandQueue;
cl_device_id device;

cl_program rollProgram;
cl_kernel rollKernel;

cl_mem randState;
cl_mem tally;

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

bool setup(const char* _KernelFileName){
    cl_int platformResult = CL_SUCCESS;
    cl_uint numPlatforms = 0;
    cl_platform_id platforms[64];

    platformResult = clGetPlatformIDs( 64, platforms, &numPlatforms );

    if (platformResult != CL_SUCCESS) {
        std::cerr << "Couldnt get platform IDs!\n Failed with error(" << platformResult << ")\n";
        return false;
    }

    for(int i = 0; i < numPlatforms; i++){
	char version[128];
	clGetPlatformInfo(platforms[i], CL_PLATFORM_VERSION, sizeof(version), version, NULL);
	std::cout << "Platform version: " << version << std::endl;


        cl_device_id devices[64];
        unsigned int deviceCount;
        cl_int deviceResult = clGetDeviceIDs( platforms[i], CL_DEVICE_TYPE_GPU, 64, devices, &deviceCount);
	std::cout << deviceCount << " devices found\n";

        if( deviceResult == CL_SUCCESS){
            for (int j = 0; j < deviceCount; j++){
                unsigned long numComputeUnits;
                size_t computeUnitsLen;
                cl_int deviceInfoResult = clGetDeviceInfo( devices[j], CL_DEVICE_MAX_COMPUTE_UNITS, sizeof(cl_uint), &numComputeUnits, &computeUnitsLen);
                std::cout << numComputeUnits << " compute units on the device\n";

                
                if(deviceInfoResult == CL_SUCCESS){
                    // currently picks first device, rank by mem size ig?
                    std::cout << "device found!!\n";
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
    cl_queue_properties props[] = { 0 };
    //commandQueue = clCreateCommandQueue(context, device, 0, &commandQueueResult);
    commandQueue = clCreateCommandQueueWithProperties(context, device, props, &commandQueueResult);
    if (commandQueueResult != CL_SUCCESS){
        std::cerr << "Failed to make command queue!\n Failed with error (" << commandQueueResult << ")\n";
        return false;
    }

    // create program and kernel
    std::string s = readFile(_KernelFileName);
    const char* programSource = s.c_str();
    size_t length = 0;
    cl_int programResult;
    rollProgram = clCreateProgramWithSource(context, 1, &programSource, &length, &programResult);
    if (programResult != CL_SUCCESS){
        std::cerr << "Failed to make program!\n Failed with error (" << programResult << ")\n";
        return false;
    }

    cl_int programBuildResult = clBuildProgram( rollProgram, 1, &device, "-cl-std=CL3.0\0", nullptr, nullptr);
    if (programBuildResult != CL_SUCCESS){
        char log[1024];
        size_t logLength;
        cl_int programBuildInfoResult = clGetProgramBuildInfo(rollProgram, device, CL_PROGRAM_BUILD_LOG, 1024, log, &logLength);

        if(logLength >= 1024){
            char newlog[logLength];
            cl_int programBuildInfoResult = clGetProgramBuildInfo(rollProgram, device, CL_PROGRAM_BUILD_LOG, logLength, newlog, &logLength);

            std::cout << "log len - " << logLength << std::endl;
            std::cout <<  "newlog:\n" << newlog << std::endl << std::endl;
        }else{
            std::cout << "log len - " << logLength << std::endl;
            std::cout <<  "a log:\n" << log << std::endl << std::endl;
        }
        
        if (programBuildInfoResult != CL_SUCCESS){
            std::cerr << "Failed to build program!\n Failed with error (" << programBuildInfoResult << ")\n";
            return false;
        }
    }

    cl_int kernelResult;              // this string must mach entry function name
	rollKernel = clCreateKernel( rollProgram, "roll", &kernelResult);
    if (programResult != CL_SUCCESS){
        std::cerr << "Failed to make kernel!\n Failed with error (" << programResult << ")\n";
        return false;
    }

    return true;
}

void cleanup(){
    clReleaseMemObject(tally);
    clReleaseMemObject(randState);
    clReleaseKernel(rollKernel);
    clReleaseProgram(rollProgram);
    clReleaseCommandQueue(commandQueue);
    clReleaseContext(context);
    //clReleaseDevice(device); // not needed??
}

#define CHECK_RES(res, txt) \
		if(res != CL_SUCCESS){ \
				std::cerr << txt << "\n Failed with error (" << res << ")\n"; \
				cleanup(); \
				return 1; \
		} \

int main(int argc, char* argv[])
{
    const auto start = std::chrono::high_resolution_clock::now();

    uint64_t rolls;
    if( argc != 3){
        std::cout << "wrong amount of args, should be {rolls} {rolls mult}\n";
        return 1;
    }
    rolls = atol(argv[1]);

    if( *argv[2] == 'k'){
        rolls *= 1000;
    }
    else if (*argv[2] == 'm'){
        rolls *= 1000000;
    }
    else if (*argv[2] == 'b'){
        rolls *= 1000000000;
    }
    else if (*argv[2] == 't'){
        rolls *= 1000000000000;
    }
    else{
        std::cerr << "bad multiplier\n";
        return 1;
    }
    
    uint64_t rollsPerThread = rolls / 100000; // this seems to yield best performance

    if (setup("../kernel.cl") != true){
        std::cerr << "Failed to set up open cl\n";
        cleanup();
        return 1;
    }

    std::cout << "start" << std::endl;

    // query max local work group size
    size_t maxLocalWorkSize;
    CHECK_RES( clGetKernelWorkGroupInfo(rollKernel, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &maxLocalWorkSize, nullptr),
            "Failed to query work group info!");
    std::cout << "max local work size - " << maxLocalWorkSize << std::endl;

    uint64_t remainder = rolls % 100000;

    // set work sizes
    globalWorkSize = rolls / rollsPerThread;
    localWorkSize = getLocalWorkSize(maxLocalWorkSize, globalWorkSize);
    
    std::cout << "global size - " << globalWorkSize << " local size - " << localWorkSize  << " remainder : " << remainder << std::endl;

    std::vector<ulong> state;
    int stateNum = localWorkSize; 
    state.resize(4 * stateNum); 

    // initialise randomiser state
    auto now = std::chrono::system_clock::now();
	auto epoch = now.time_since_epoch();
	uint64_t count = epoch.count();

	for(int i = 0; i < 4 * stateNum; i++){
		state[i] = next_s(count);
	}
    
    // setup buffers
    cl_uint eventsInWaitList = 2;
    cl_event events[2];
    
    cl_int randStateResult;
    randState = clCreateBuffer(context, CL_MEM_READ_ONLY,  stateNum * 4 * sizeof(ulong), nullptr, &randStateResult);
    if (randStateResult != CL_SUCCESS){
        std::cerr << "Failed to create buffer rand state!\n Failed with error (" << randStateResult << ")\n";
        return 1;
    }
    CHECK_RES( clEnqueueWriteBuffer(commandQueue, randState, CL_TRUE, 0, stateNum * 4 * sizeof(ulong), state.data(), 0, nullptr, &events[0]),
            "Failed to enqueue buffer write (state)!");

    clFlush(commandQueue);


    ulong tallyData[20] = {0};

    cl_int tallyBufferResult;
    tally = clCreateBuffer(context, CL_MEM_READ_WRITE, 20 * sizeof(uint64_t), nullptr, &tallyBufferResult);
    if (tallyBufferResult != CL_SUCCESS){
        std::cerr << "Failed to create tally buffer!\n Failed with error (" << tallyBufferResult << ")\n";
        return 1;
    }
    CHECK_RES( clEnqueueWriteBuffer(commandQueue, tally, CL_TRUE, 0, 20 * sizeof(uint64_t), tallyData, 0, nullptr, &events[1]),
            "Failed to enqueue buffer write(tally)!" );

    clFlush(commandQueue);
    

    const auto mid = std::chrono::high_resolution_clock::now();


    //set kernel arguments
    CHECK_RES( clSetKernelArg(rollKernel, 0, sizeof(cl_mem), &randState),
            "Failed to set kernal arg(state)!");
    CHECK_RES(clSetKernelArg(rollKernel, 1, sizeof(uint64_t), &rollsPerThread),
             "Failed to set kernal arg(reps)!");
    CHECK_RES(clSetKernelArg(rollKernel, 2, sizeof(uint64_t), &remainder),
             "Failed to set kernal arg(reps)!");
    CHECK_RES(clSetKernelArg(rollKernel, 3, sizeof(cl_mem), &tally),
             "Failed to set kernal arg(tally)!");

    //enqueue kernel
    CHECK_RES( clEnqueueNDRangeKernel(commandQueue, rollKernel, 1, 0, &globalWorkSize, &localWorkSize, 2, events, nullptr),
            "Failed to enqueue roll kernel!");

    clFinish(commandQueue);

    //read results
    CHECK_RES( clEnqueueReadBuffer(commandQueue, tally, CL_TRUE, 0, 20 * sizeof(ulong), tallyData, 0, nullptr, nullptr),
            "Failed to enqueue buffer read!(tally)");

    clFinish(commandQueue);

    const auto end = std::chrono::high_resolution_clock::now();
	const std::chrono::duration<double, std::milli> totTime = end - start;
    const std::chrono::duration<double, std::milli> computationTime = end - mid;
	double totalTime = std::chrono::duration<double>(totTime).count();
    double workTime = std::chrono::duration<double>(computationTime).count();

    std::cout << "\nTotal time taken: " << totalTime << "s\n" << "gpu work time: " << workTime << "s\n(" << "cpu overhead: " << totalTime - workTime << "s)" << std::endl << std::endl;

    // display results
    std::cout << "results!: \n";
    std::cout << std::endl;

    ulong tot = 0;

    for(int i = 0; i < 20; i++){
        ulong tst = (ulong)(rolls);
        double percent = ((long double)tallyData[i] / rolls) * 100.0;
        tot += tallyData[i];
        std::cout << i+1 << "\t: " << tallyData[i] << "\t\t: " << percent << "%" << std::endl;
    }

    if(tot == rolls){
        std::cout << "rolled good" << std::endl;
    }else{
        std::cout << "rolled wrong amount" << std::endl;
    }
 
    cleanup();

    return 0;
}
