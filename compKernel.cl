inline void sumLocalTallys(__local ulong* a, __local ulong* b){
    for(int i = 0; i < 20; i++){
        a[i] += b[i];
        if(b[i] == -1){
            printf("whoopsies!!!\n");
        }
        b[i] = -1;
    }
}

inline void printLocalTally(int lSize, ulong* tally){
    for(int i = 0; i < lSize; i++){
        for(int j = 0; j < 20; j++){
            printf("(%ld)  ", tally[(20*i)+j]);
        }
        printf("\n\n");
    }
    printf("---- ---- END OF PRINTING ---- ----\n");
}

inline void printTally(ulong* t, int id, bool pre){
    printf("=\n");
    for(int i = 0; i < 20; i++){
        if(pre){
            printf("(%d) (pre) %d - %ld\n", id, i, t[i]);
        }else{
            printf("(%d) (pos) %d - %ld\n", id, i, t[i]);
        }
        
    }
    printf("+\n");
    
}

__kernel void comp(__global uint* a, uint aSize, __global uint* b,__local ulong* lTally, __global atomic_ulong* tally){
	int idx = get_global_id(0);
    int lIdx = get_local_id(0);
	uint myNum = b[idx];
    ulong myTally[20];

/*     if(idx == 0){
        printTally(tally, idx, true);
        printf("============\n\n");
    } */

    //printf("test string");

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
    int temp = get_group_id(0);

    // deal with lTally being odd
    // change l size to not include odd num????s
    if(lIdx == lSize-2 && lSize % 2 == 1){
        sumLocalTallys(&lTally[(lIdx*20)], &lTally[(lIdx+1)*20]);
    }

    if(idx == 0){
        printLocalTally(lSize, lTally);
        printf("end\n\n");
    }

    barrier(CLK_LOCAL_MEM_FENCE);

    int reps = log2((float)lSize)+1;
    int offset = 1;
    int step = 2;

    for(int i  = 0; i < reps; i++){

        if(lIdx % step == 0 && lIdx+offset < lSize){
            sumLocalTallys(&lTally[lIdx*20], &lTally[(lIdx+offset)*20]);
/*             if(idx < lSize){
                printf("thread %d is adding tally %d with tally %d (offset - %d, step - %d, rep - %d)\n", idx, lIdx, lIdx + offset, offset, step, i);
            } */
        }

        offset *= 2; step *= 2;

        barrier(CLK_LOCAL_MEM_FENCE);

        if(idx == 0){
            printf("rep num %d\n", i);
            printLocalTally(lSize, lTally);
        }

        barrier(CLK_LOCAL_MEM_FENCE);
    }

    barrier(CLK_GLOBAL_MEM_FENCE);


    // sum all tallys down something like this
    //1, 2, 3, 4, 5, 6, 7, 8
    // | /   | /  |  /  | /
    //  3     7   11    15
    // |  /---/   | /---/
    //  10        26

    if(lIdx == 0){
/*         printTally(tally, idx, true); */
        for(int i = 0; i < 20; i++){
            atomic_add(&tally[i], lTally[i]);
        }
/*         printTally(tally, idx, false);
        printf("thread %d did the atomic addition\n", idx); */
    }
} 