inline void sumLocalTallys(__local ulong* a, __local ulong* b){
    for(int i = 0; i < 20; i++){
        a[i] += b[i];
    }
}

__kernel void comp(__global uint* a, uint aSize, __global uint* b,__local ulong* lTally, __global atomic_ulong* tally){
	int idx = get_global_id(0);
    int lIdx = get_local_id(0);
	uint myNum = b[idx];
    ulong myTally[20];

    // normal jess is at taly north house P floor 3
    // room 3 // flat 8

    // compare all of a with mynum
    for(int i = 0; i < aSize; i++){
        myTally[max(a[i],myNum)]++;
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    for(int i = 0; i < 20; i++){
        lTally[(lIdx*20)+i] = myTally[i];
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    int lSize = get_local_size(0);

    // deal with lTally being odd
    if(lIdx == lSize-2 && lSize % 2 == 1){
        sumLocalTallys(&lTally[(lIdx*20)], &lTally[(lIdx+1)*20]);
    }

    int reps = log2((float)lSize)+1;
    int offset = 1;
    int step = 2;

    for(int i  = 0; i < reps; i++){

        if(lIdx % step == 0 && lIdx+offset < lSize){
            sumLocalTallys(&lTally[lIdx*20], &lTally[(lIdx+offset)*20]);
        }

        offset *= 2; step *= 2;

        barrier(CLK_LOCAL_MEM_FENCE);
    }


    // sum all tallys down something like this
    //1, 2, 3, 4, 5, 6, 7, 8
    // | /   | /  |  /  | /
    //  3     7   11    15
    // |  /---/   | /---/
    //  10        26

    //if(lIdx == 0) atomic
    if(lIdx == 0){
        for(int i = 0; i < 20; i++){
            //atomic_fetch_add(tally[i], lTally[i]);
            atomic_add(&tally[i], lTally[i]);
        }
    }
} 