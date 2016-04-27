#include<iostream>
#include<sys/mman.h>
#include<sys/time.h>
#include<string.h>
#include<emmintrin.h>
#include<unistd.h>
#include<sys/types.h>
using namespace std;

/* size of this struct is exactly 64 bytes, which is the cacheline size on popular platforms */
struct cl {
    uint64_t x[8];
};

#define NUM_CL 10000000
#define NUM_CL_ACCESS (1 << 20)
#ifndef NUM_COALESCED
#define NUM_COALESCED 1
#endif
#ifndef DISTANCE
#define DISTANCE 32
#endif
typedef unsigned long long ticks;

static __inline__ ticks getticks(void)
{
     unsigned a, d;
     asm volatile("rdtsc" : "=a" (a), "=d" (d)::"memory");
     return (((ticks)a) | (((ticks)d) << 32));
}

/*
 * GOAL: This program shows that when you have random accesses in a hash table which doesn't fit in cache,
 * coalescing memory accesses provides significant improvement.
 *
 * IMPLEMENTATION:
 * This program creates a hash table of size far greater than the cache size and accesses random buckets.
 * Coalescing these accesses shows that the avg. time to access these buckets decreases.
 * Reason behind that is that coalescing hides the latency of memory accesses(all but 1).
 *
 * For more details:
 * When a memory access misses cacheline, cpu is halted unless the next instructions are independent of this memory access.
 * Assuming the simple case where cpu is halted, lot of cpu cycles are wasted waiting for one cacheline.
 * If we can fetch several cachelines in approximately the same number of cpu cycles, that would reduce
 * a lot of cpu halts. Note that we can't increase the num. coalesced memory accesses infinitely.
 * The number of coalesced memory accesses is dependent on the number of cpu load buffers (~10 to 40).
 * Hyperthreading  also has an effect on this.
 */
int old_main(void)
{
    if (geteuid()) {
        cout<<"Run as root or sudo user\n";
        exit(-1);
    }
    /* mlockall() so that we remove pagefault overhead */
    int ret =  mlockall(MCL_FUTURE);
    if (ret)
        cout<<"mlockall failed with retvalue = "<<ret<<endl;

    /* make this volatile so that compiler doesn't move around the memory accesses */
    volatile struct cl *x = new cl[NUM_CL];

    /* Use time to generate seed */
    struct timeval tv;
    if (gettimeofday(&tv, NULL)) {
        cout<<"gettimeofday failed\n";
        exit(-1);
    }
    srand((unsigned int) tv.tv_usec);

#define TEMP_SZ 8
    int64_t *temp = new int64_t[TEMP_SZ];
    volatile int sum = 0;
    memset(temp, 8 * sizeof(int), 1);
    volatile auto t0 = getticks();
    for (int i = 0; i < NUM_CL_ACCESS/NUM_COALESCED; i++)
    {
        volatile int j[NUM_COALESCED];
        for (int k = 0; k < NUM_COALESCED; k++)
            j[k] = rand() % NUM_CL;
            for (int k = 0; k < NUM_COALESCED; k++) {
#ifdef PREFETCH
                _mm_prefetch((const void*)&x[j[k]].x[0], 1);
#else
                x[j[k]].x[0]++;
#endif
            }

#if 0
        for (int64_t l = 0; l < DISTANCE * NUM_COALESCED; l++)
            sum += temp[l%TEMP_SZ] * x[j[l % NUM_COALESCED]].x[0];
#else
        for (int64_t l = 0; l < NUM_COALESCED; l++)
            j[l] = x[j[l]].x[0];
        for (int64_t l = 0; l < DISTANCE; l++)
            sum += temp[l % TEMP_SZ] * j[l % NUM_COALESCED];
#endif
        /*
        * If we replace the above two for loops (one for NUM_COALESCED & one for DISTANCE
        * with one single loop 0 to NUM_COALESCED * DISTANCE (in the #if 0 part),
        * there seems to be some performance degradation probably because the above two loops
        * coalesce memory accesses compared to one single loop.
        */
    }
    volatile auto t1 = getticks();
    cout<<"Num. cpu clocks per cacheline access "<<(t1 - t0) / NUM_CL_ACCESS<<endl;
    return 0;
}

int main(void) {
    if (geteuid()) {
        cout<<"Run as root or sudo user\n";
        exit(-1);
    }
    /* mlockall() so that we remove pagefault overhead */
    int ret =  mlockall(MCL_FUTURE);
    if (ret)
        cout<<"mlockall failed with retvalue = "<<ret<<endl;

    volatile uint64_t x = 0;
#define NUM_INCRS (1000000)
    volatile auto t0 = getticks();
        //asm volatile ("inc %0": "+r"(x));
    while(x < NUM_INCRS) {
        asm volatile("addq $0x1,-0x20(%rbp)");
    }


    volatile auto t1 = getticks();
    cout<<"Num. cpu clocks per increment using just addq = "<<(t1 - t0) / NUM_INCRS<<endl;
    x = 0;
    t0 = getticks();
        //asm volatile ("inc %0": "+r"(x));
    while(x < NUM_INCRS) {
        asm volatile("addq $0x1,-0x20(%rbp)");
        asm volatile("mfence":::"memory");
    }

    t1 = getticks();
    cout<<"Num. cpu clocks per increment using addq + mfence = "<<(t1 - t0) / NUM_INCRS<<endl;
    x = 0;
    t0 = getticks();

    while(x < NUM_INCRS)
        //asm volatile ("lock add $0x1, %0": "+r"(x));
        __atomic_add_fetch(&x, 1, __ATOMIC_RELAXED);
    t1 = getticks();
    cout<<"Num. cpu clocks per increment with lock add = "<<(t1 - t0) / NUM_INCRS<<endl;
    return 0;
}
