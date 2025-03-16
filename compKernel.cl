__kernel void comp(__global uint* a, __global uint* b, __global ulong* tally){
	int idx = get_global_id(0);
	tally[idx] = 1;
} 