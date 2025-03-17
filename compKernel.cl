__kernel void comp(__global uint* a, uint aSize, __global uint* b, __global ulong* tally){
	int idx = get_global_id(0);
	uint myNum = b[idx];
    uint myTally[20];

    // normal jess is at taly north house P floor 3
    // room 3 // flat 8

    for(int i = 0; i < aSize; i++){
        myTally[max(a[i],myNum)]++;
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    // allocate a local buffer which has local_size * 20 amount of items
    // each work item generate and update thier part of the local tally
    // then use a local barrier
    // then regressively sum all the local tallys down

    // 1, 2, 3, 4 ..... n
    // | /   | /  
    //  3     7
    //   \   /
    //    10
    // like this

    // then atomically add the local tally to the global tally
    // and then you can read the global tally for results
    
} 