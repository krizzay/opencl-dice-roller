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

cl_program program;
cl_kernel kernel;

cl_mem a;
cl_mem b;
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

/*
a, b buffers will be global
for now generate all randomiser state on cpu and pass it as another buffer
one kernel will generate x amount of numbers into a and b
another kernel will compare a section of b with all of a and record results into the global tally
    copy section of b into local memory, keep a global

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

    if (setup("../kernel.cl") != true){
        std::cerr << "Failed to set up open cl\n";
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
    cl_mem randState = clCreateBuffer(context, CL_MEM_READ_ONLY, sqRolls * 4 * sizeof(ulong), nullptr, &randStateResult);
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
    cl_mem a = clCreateBuffer(context, CL_MEM_READ_WRITE, sqRolls * sizeof(uint), nullptr, &aDataResult);
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
    cl_mem b = clCreateBuffer(context, CL_MEM_READ_WRITE, sqRolls * sizeof(uint), nullptr, &bDataResult);
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



    // fill a
    // set kernel arguments
    cl_int kernalArgStateResult = clSetKernelArg(kernel, 0, sizeof(cl_mem), &randState);
    if (kernalArgStateResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(state)!\n Failed with error (" << kernalArgStateResult << ")\n";
        return 1;
    }
    cl_int kernalArgAResult = clSetKernelArg(kernel, 1, sizeof(cl_mem), &a);
    if (kernalArgAResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(a)!\n Failed with error (" << kernalArgAResult << ")\n";
        return 1;
    }

    // query max local work group size
    size_t sumin;
    size_t maxLocalWorkSize;
    cl_int lWorkSizeResult = clGetKernelWorkGroupInfo(kernel, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &maxLocalWorkSize, &sumin); 
    if ( lWorkSizeResult != CL_SUCCESS){
        std::cerr << "Failed to query work group info!\n Failed with error (" << lWorkSizeResult << ")\n";
        return 1;
    }
    std::cout << "max local work size - " << maxLocalWorkSize << std::endl;


    // command submission
    globalWorkSize = sqRolls; // each thread will generate one number into a
    localWorkSize = getLocalWorkSize(maxLocalWorkSize, globalWorkSize);

    std::cout << "global size - " << globalWorkSize << " local size - " << localWorkSize << std::endl;

    cl_int enqueueKernelResult = clEnqueueNDRangeKernel(commandQueue, kernel, 1, 0, &globalWorkSize, &localWorkSize, 0, nullptr, nullptr);
    if (enqueueKernelResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue kernel!\n Failed with error (" << enqueueKernelResult << ")\n";
        return 1;
    }

    cl_int aReadRes = clEnqueueReadBuffer(commandQueue, a, CL_TRUE, 0, sqRolls * sizeof(uint), aData, 0, nullptr, nullptr);
    if (aReadRes != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer read!\n Failed with error (" << aReadRes << ")\n";
        return 1;
    }
    /*
    uint64_t tallyData[20];
    cl_int readResultsResult = clEnqueueReadBuffer(commandQueue, tally, CL_TRUE, 0, 20 * sizeof(uint64_t), tallyData, 0, nullptr, nullptr);
    if (readResultsResult != CL_SUCCESS){
        std::cerr << "Failed to enqueue buffer read!\n Failed with error (" << readResultsResult << ")\n";
        return 1;
    }
    */

    // flush command buffer and await completion
    clFinish(commandQueue);

    // display results
    std::cout << "results!: \n";
    for(int i = 0; i < sqRolls; i++){
        std::cout << i << " - " << aData[i] << "\n";
    }
 
    cleanup();

    return 0;
}