#include <iostream>
#include <string>
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

                //cl_int deviceInfoResTwo = clGetDeviceInfo( devices[j], CL_MAX_GR)  // was thinking of checking for max fuckin uhhhh global work size if there is one?
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

        if(logLength >= 1024){
            char newlog[logLength];
            cl_int programBuildInfoResult = clGetProgramBuildInfo(fillProgram, device, CL_PROGRAM_BUILD_LOG, logLength, newlog, &logLength);

            std::cout << "log len - " << logLength << std::endl;
            std::cout <<  "newlog:\n" << newlog << std::endl << std::endl;
        }else{
            std::cout << "log len - " << logLength << std::endl;
            std::cout <<  "a log:\n" << log << std::endl << std::endl;
        }
        
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
        size_t logLength;
        cl_int programBuildInfoResult = clGetProgramBuildInfo(compProgram, device, CL_PROGRAM_BUILD_LOG, 0, nullptr, &logLength);

        char log[logLength];

        programBuildInfoResult = clGetProgramBuildInfo(compProgram, device, CL_PROGRAM_BUILD_LOG, logLength, log, &logLength);

        std::cout << "log len - " << logLength << std::endl;
        std::cout <<  "a log:\n" << log << std::endl << "*end of log*" << std::endl;

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
    clReleaseMemObject(tally);
    clReleaseMemObject(randState);
    clReleaseKernel(fillKernel);
    clReleaseProgram(fillProgram);
    clReleaseKernel(compKernel);
    clReleaseProgram(compProgram);
    clReleaseCommandQueue(commandQueue);
    clReleaseContext(context);
    //clReleaseDevice(device); // not needed??
}

int inline checkRes(cl_int res, std::string txt){
    if(res != CL_SUCCESS){
        std::cerr << txt << "\n Failed with error (" << res << ")\n";
        cleanup();
        return 1;
    }
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

/*
pretty sure rn im runnig into a stack overflow
allocate all buffers on the heap first,
refactor all the yucky yuck yuck code,
then idk figure out how to optimise the state generation step, cuz like a lot of it is cpu side when it could be gpu side but like a hash function would be the same each time
maybe you generate a single long cpu side and set it as an arg in th kernel and bitwise or it with the hash or some shit
*/

int main(int argc, char* argv[])
{

/*
	const auto end = chrono::high_resolution_clock::now();
	const std::chrono::duration<double, std::milli> diff = end - start;
	double time = chrono::duration<double>(diff).count();
*/
    const auto start = std::chrono::high_resolution_clock::now();

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
        else if (*argv[2] == 'b'){
            rolls *= 1000000000;
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
    checkRes( clEnqueueWriteBuffer(commandQueue, randState, CL_TRUE, 0, sqRolls * 4 * sizeof(ulong), s, 0, nullptr, nullptr),
            "Failed to enqueue buffer write (state)!");

    cl_int aDataResult;
    a = clCreateBuffer(context, CL_MEM_READ_WRITE, sqRolls * sizeof(uint), nullptr, &aDataResult);
    if (aDataResult != CL_SUCCESS){
        std::cerr << "Failed to create buffer a!\n Failed with error (" << aDataResult << ")\n";
        return 1;
    }
    // TODO: consider switching out write buffer as theyll be empty rn and thats just fine but idk
    checkRes( clEnqueueWriteBuffer(commandQueue, a, CL_TRUE, 0, sqRolls * sizeof(uint), aData, 0, nullptr, nullptr),
            "Failed to enqueue buffer write (a)!");

    cl_int bDataResult;
    b = clCreateBuffer(context, CL_MEM_READ_WRITE, sqRolls * sizeof(uint), nullptr, &bDataResult);
    if (bDataResult != CL_SUCCESS){
        std::cerr << "Failed to create buffer b!\n Failed with error (" << bDataResult << ")\n";
        return 1;
    }
    checkRes( clEnqueueWriteBuffer(commandQueue, b, CL_TRUE, 0, sqRolls * sizeof(uint), bData, 0, nullptr, nullptr),
            "Failed to enqueue buffer write (b)!");

    cl_int tallyBufferResult;
    tally = clCreateBuffer(context, CL_MEM_READ_WRITE, 20 * sizeof(uint64_t), nullptr, &tallyBufferResult);
    if (tallyBufferResult != CL_SUCCESS){
        std::cerr << "Failed to create tally buffer!\n Failed with error (" << enqueueAResult << ")\n";
        return 1;
    }


    // query max local work group size
    size_t sumin;
    size_t maxLocalWorkSize;
    checkRes( clGetKernelWorkGroupInfo(fillKernel, device, CL_KERNEL_WORK_GROUP_SIZE, sizeof(size_t), &maxLocalWorkSize, &sumin),
            "Failed to query work group info!");
    std::cout << "max local work size - " << maxLocalWorkSize << std::endl;

    // set work sizes
    globalWorkSize = sqRolls; // each thread will generate one number into a
    localWorkSize = getLocalWorkSize(maxLocalWorkSize, globalWorkSize);
    std::cout << "global size - " << globalWorkSize << " local size - " << localWorkSize << std::endl;

    const auto mid = std::chrono::high_resolution_clock::now();

    // fill a
    // set kernel arguments
    checkRes( clSetKernelArg(fillKernel, 0, sizeof(cl_mem), &randState),
            "Failed to set kernal arg(state)!");

    checkRes(clSetKernelArg(fillKernel, 0, sizeof(cl_mem), &a),
             "Failed to set kernal arg(a)!");

/*     cl_int kernalArgAResult = clSetKernelArg(fillKernel, 1, sizeof(cl_mem), &a);
    if (kernalArgAResult != CL_SUCCESS){
        std::cerr << "Failed to set kernal arg(a)!\n Failed with error (" << kernalArgAResult << ")\n";
        return 1;
    } */

    //enqueu kernel
    checkRes( clEnqueueNDRangeKernel(commandQueue, fillKernel, 1, 0, &globalWorkSize, &localWorkSize, 0, nullptr, nullptr),
            "Failed to enqueue kernel(fill a)!");

    clFinish(commandQueue);

    // fill b
    // set kernel arguments

    checkRes( clSetKernelArg(fillKernel, 0, sizeof(cl_mem), &randState),
            "Failed to set kernal arg(state)!");

    checkRes( clSetKernelArg(fillKernel, 1, sizeof(cl_mem), &b),
            "Failed to set kernal arg(b)!");

    // enqueue kernel
    checkRes( clEnqueueNDRangeKernel(commandQueue, fillKernel, 1, 0, &globalWorkSize, &localWorkSize, 0, nullptr, nullptr),
            "Failed to enqueue kernel(fill b)!");

    clFinish(commandQueue);

    //comparison
    checkRes( clSetKernelArg(compKernel, 0, sizeof(cl_mem), &a), 
            "Failed to set kernal arg(a for comp)!");

    checkRes( clSetKernelArg(compKernel, 1, sizeof(uint), &sqRolls),
            "Failed to set kernal arg(a size)!");

    checkRes( clSetKernelArg(compKernel, 2, sizeof(cl_mem), &b),
            "Failed to set kernal arg(b for comp)!");

    // local buffer
    checkRes( clSetKernelArg(compKernel, 3, localWorkSize * 20 * sizeof(ulong), nullptr),
            "Failed to set kernal arg(local tally)!");

    checkRes( clSetKernelArg(compKernel, 4, sizeof(cl_mem), &tally),
            "Failed to set kernal arg(global tally)!");

    // enqueue kernel
    checkRes( clEnqueueNDRangeKernel(commandQueue, compKernel, 1, 0, &globalWorkSize, &localWorkSize, 0, nullptr, nullptr),
            "Failed to enqueue kernel!");

    // enqueue reads
    checkRes( clEnqueueReadBuffer(commandQueue, a, CL_TRUE, 0, sqRolls * sizeof(uint), aData, 0, nullptr, nullptr),
            "Failed to enqueue buffer read!(a)");

    checkRes( clEnqueueReadBuffer(commandQueue, b, CL_TRUE, 0, sqRolls * sizeof(uint), bData, 0, nullptr, nullptr),
            "Failed to enqueue buffer read!(b)");

    clFinish(commandQueue);

    ulong tallyData[20];

    checkRes( clEnqueueReadBuffer(commandQueue, tally, CL_TRUE, 0, 20 * sizeof(ulong), tallyData, 0, nullptr, nullptr),
            "Failed to enqueue buffer read!(tally)");

    clFinish(commandQueue);

    const auto end = std::chrono::high_resolution_clock::now();
	const std::chrono::duration<double, std::milli> totTime = end - start;
    const std::chrono::duration<double, std::milli> computationTime = end - mid;
	double totalTime = std::chrono::duration<double>(totTime).count();
    double workTime = std::chrono::duration<double>(computationTime).count();

    std::cout << "\nTotal time taken: " << totalTime << "\n" << "Total work time: " << workTime << "\n(" << "overhead: " << totalTime - workTime << ")" << std::endl << std::endl;

    // display results
    std::cout << "results!: \n";
/*     for(int i = 0; i < sqRolls; i++){
        std::cout << i << "\ta:" << aData[i] << "\t\tb:" << bData[i] << std::endl;
    }  */

    std::cout << std::endl;

    for(int i = 0; i < 20; i++){
        ulong tst = (ulong)(rolls);
        double percent = ((long double)tallyData[i] / rolls) * 100.0;
        //std::cout << "tally data: " << tallyData[i] << " rolls: " << rolls << " quotient: " << tallyData[i] / (ulong)rolls << "\n\n";
        std::cout << i+1 << "\t: " << tallyData[i] << "\t: " << percent << "%" << std::endl;
    }
 
    cleanup();

    return 0;
}