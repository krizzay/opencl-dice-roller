static inline ulong rotl(const ulong x, int k){
	return (x << k) | (x >> (64 - k));
}

ulong next_x(ulong* s) {
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

void jump(ulong* s) {
	static const ulong JUMP[] = { 0x180ec6d33cfd0aba, 0xd5a61266f0c9392c, 0xa9582618e03fc9aa, 0x39abdc4529b1661c };

	ulong s0 = 0;
	ulong s1 = 0;
	ulong s2 = 0;
	ulong s3 = 0;
	for(int i = 0; i < sizeof JUMP / sizeof *JUMP; i++)
		for(int b = 0; b < 64; b++) {
			if (JUMP[i] & 1 << b) {
				s0 ^= s[0];
				s1 ^= s[1];
				s2 ^= s[2];
				s3 ^= s[3];
			}
			next_x(s);	
		}
		
	s[0] = s0;
	s[1] = s1;
	s[2] = s2;
	s[3] = s3;
}

void long_jump(ulong* s) {
	static const ulong JUMP[] = { 0x76e15d3efefdcbbf, 0xc5004e441c522fb3, 0x77710069854ee241, 0x39109bb02acbe635 };

	ulong s0 = 0;
	ulong s1 = 0;
	ulong s2 = 0;
	ulong s3 = 0;
	for(int i = 0; i < sizeof JUMP / sizeof *JUMP; i++)
		for(int b = 0; b < 64; b++) {
			if (JUMP[i] & 1 << b) {
				s0 ^= s[0];
				s1 ^= s[1];
				s2 ^= s[2];
				s3 ^= s[3];
			}
			next_x(s);	
		}
		
	s[0] = s0;
	s[1] = s1;
	s[2] = s2;
	s[3] = s3;
}

__kernel void roll(__global ulong* state, ulong reps, ulong remain, /*__local uint* localTally, */ __global ulong* tally){
	int idx = get_global_id(0);
	int groupIdx = get_group_id(0);
	int localIdx = get_local_id(0);

	ulong s[4];
	for(int i = 0; i < 4; i++){
		s[i] = state[i];
	}

	uint localTally[20] = {0};

	// get each thread to a unique place in the random number stream for seed state
	for(int i = 0; i < groupIdx; i++){
		long_jump(s);
	}
	for(int i = 0; i < localIdx; i++){
		jump(s);
	}

	if(get_global_id(0) < remain){ reps++; } // do additional rep for the remainder

	// generate random numbers and populate the localTally
	for(int i = 0; i < reps; i++){
		localTally[max(next_x(s) % 20, next_x(s) % 20)]++;
	}

	// add results to global tally
	for(int i = 0; i < 20; i++){
            atomic_add(&tally[i], localTally[i]);
    }
} 