
static inline ulong rotl(const ulong x, int k){
	return (x << k) | (x >> (64 - k));
}

ulong next_x(ulong s[4]) {
	const ulong result = s[0] + s[3];

	const ulong t = s[1] << 17;

	s[2] ^= s[0];
	s[3] ^= s[1];
	s[1] ^= s[2];
	s[0] ^= s[3];

	s[2] ^= t;

	s[3] = rotl(s[3], 45);

	return result;
}

__kernel void arr_sum(__global ulong* state, __global uint* output){
	int idx = get_global_id(0);
	ulong s[4];
	for(int i = 0; i < 4; i++){
		s[i] = state[idx*i];
	}

	//output[idx] = next_x(s) % 20;
	output[idx] = next_x(s) % 20;
} 