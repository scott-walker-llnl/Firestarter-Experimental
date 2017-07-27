/******************************************************************************
 * FIRESTARTER - A Processor Stress Test Utility
 * Copyright (C) 2017 TU Dresden, Center for Information Services and High
 * Performance Computing
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * Contact: daniel.hackenberg@tu-dresden.de
 *****************************************************************************/

#define NUM_FS_WORKLOADS 6
#define NUM_SLEEP_WORKLOADS 2

#define _GNU_SOURCE

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>
#include <math.h>
#include <time.h>

#include "msr_core.h"

#ifdef ENABLE_VTRACING
#include <vt_user.h>
#endif
#ifdef ENABLE_SCOREP
#include <SCOREP_User.h>
#endif

#define READ 312
#define WRITE 313
#define ENERGY_UNIT 0x606
#define POWER_LIMIT 0x610
#define ENERGY_STATUS 0x611
#define ENERGY_PP0 0x639
#define ENERGY_PP1 0x64D
#define PERF_BIAS 0x1B0
#define APERF 0xE8
#define MPERF 0xE7
#define PERF_CTL 0x199
#define PERF_STAT 0x198
#define TURBO_LIMIT0 0x1AD
#define TURBO_LIMIT1 0x1AE
#define TURBO_LIMIT2 0x1AF
#define FIXED_CTR0 0x309
#define THERM_STAT 0x1B1
#define THERM_INT 0x1B2
#define THERM_CORE 0x19C
#define FIXED_CTR_CTRL 0x38D
#define POWER_INFO 0x614

#define NUM_ITERS 80000UL
#define DUTY_CYCLE 8800U
#define QUARTER_DUTY (DUTY_CYCLE / 4)
#define FIXED_CTR_CTL 0x38D
/*
 * Header for local functions
 */
#include "work.h"
#include "cpu.h"

//#define ENERGY_UNIT (1.0f / 8.0f)
#define MAX_JOULES (0xFFFFFFFFUL / 65536UL)
#define WATTS 90.0 
#define SECONDS 1

int BARRIER_GLOBAL = 0;

int intload();

void barrier(unsigned affinity, threaddata_t *threaddata)
{
	int sibling = -1;

	int itr;
	for (itr = 1; itr <= (int) ceil(log(threaddata->numthreads)); itr++)
	{
		while (threaddata->barrierdata[affinity] != 0);
		threaddata->barrierdata[affinity] = itr;
		sibling = (affinity + (int) (2 << (itr - 1))) % (int) threaddata->numthreads;
		while (threaddata->barrierdata[sibling] != itr);
		threaddata->barrierdata[sibling] = 0;
	}
}

/*
 * low load function
 */
int low_load_function(volatile unsigned long long addrHigh, unsigned int period) __attribute__((noinline));
int low_load_function(volatile unsigned long long addrHigh, unsigned int period)
{
    int nap;

    nap = period / 100;
    __asm__ __volatile__ ("mfence;"
                  "cpuid;" ::: "eax", "ebx", "ecx", "edx");
    while(*((volatile unsigned long long *)addrHigh) == LOAD_LOW){
        __asm__ __volatile__ ("mfence;"
                      "cpuid;" ::: "eax", "ebx", "ecx", "edx");
        usleep(nap);
        __asm__ __volatile__ ("mfence;"
                      "cpuid;" ::: "eax", "ebx", "ecx", "edx");
    }

    return 0;
}

/*
 * function that performs the stress test
 */
inline void _work(volatile mydata_t *data, unsigned long long *high)
{
    unsigned int i;

    //start worker threads
    for(i = 0; i < data->num_threads; i++){
        data->ack = 0;
        data->threaddata[i].addrHigh = (unsigned long long)high;
        data->thread_comm[i] = THREAD_WORK;
        while(!data->ack); // wait for acknowledgment
    }
    data->ack = 0;
}

/*
 * loop for additional worker threads
 * communicating with master thread using shared variables
 */
void *thread(void *threaddata)
{
    int id = ((threaddata_t *)threaddata)->thread_id;
    volatile mydata_t *global_data = ((threaddata_t *)threaddata)->data; //communication with master thread
    threaddata_t *mydata = (threaddata_t *)threaddata;
    unsigned int tmp = 0;
    unsigned long long old = THREAD_STOP;
	unsigned affinity = ((threaddata_t *) threaddata)->cpu_id;
	unsigned barrier_affinity = ((threaddata_t *) threaddata)->cpu_id;

    /* wait untill master thread starts initialization */
    while(global_data->thread_comm[id] != THREAD_INIT);

    while(1){
        switch(global_data->thread_comm[id]){
            case THREAD_INIT: // allocate and initialize memory
                if(old != THREAD_INIT){
                    old = THREAD_INIT;
                    
                    /* set affinity  */
                    #if (defined(linux) || defined(__linux__)) && defined (AFFINITY)
                    cpu_set(((threaddata_t *) threaddata)->cpu_id);
                    #endif

                    /* allocate memory */
                    if(mydata->buffersizeMem){
                        mydata->bufferMem = _mm_malloc(mydata->buffersizeMem, mydata->alignment);
                        mydata->addrMem = (unsigned long long)(mydata->bufferMem);
                    }
                    if(mydata->bufferMem == NULL){
                        global_data->ack = THREAD_INIT_FAILURE;
                    }
                    else{ 
                        global_data->ack = id + 1; 
                    }

                    /* call init function */
                    switch (mydata->FUNCTION)
                    {
                        case FUNC_KNL_XEONPHI_AVX512_4T:
                            tmp = init_knl_xeonphi_avx512_4t(mydata);
                            break;
                        case FUNC_SKL_COREI_FMA_1T:
                            tmp = init_skl_corei_fma_1t(mydata);
                            break;
                        case FUNC_SKL_COREI_FMA_2T:
                            tmp = init_skl_corei_fma_2t(mydata);
                            break;
                        case FUNC_HSW_COREI_FMA_1T:
                            tmp = init_hsw_corei_fma_1t(mydata);
                            break;
                        case FUNC_HSW_COREI_FMA_2T:
                            tmp = init_hsw_corei_fma_2t(mydata);
                            break;
                        case FUNC_HSW_XEONEP_FMA_1T:
                            tmp = init_hsw_xeonep_fma_1t(mydata);
                            break;
                        case FUNC_HSW_XEONEP_FMA_2T:
                            tmp = init_hsw_xeonep_fma_2t(mydata);
                            break;
                        case FUNC_SNB_COREI_AVX_1T:
                            tmp = init_snb_corei_avx_1t(mydata);
                            break;
                        case FUNC_SNB_COREI_AVX_2T:
                            tmp = init_snb_corei_avx_2t(mydata);
                            break;
                        case FUNC_SNB_XEONEP_AVX_1T:
                            tmp = init_snb_xeonep_avx_1t(mydata);
                            break;
                        case FUNC_SNB_XEONEP_AVX_2T:
                            tmp = init_snb_xeonep_avx_2t(mydata);
                            break;
                        case FUNC_NHM_COREI_SSE2_1T:
                            tmp = init_nhm_corei_sse2_1t(mydata);
                            break;
                        case FUNC_NHM_COREI_SSE2_2T:
                            tmp = init_nhm_corei_sse2_2t(mydata);
                            break;
                        case FUNC_NHM_XEONEP_SSE2_1T:
                            tmp = init_nhm_xeonep_sse2_1t(mydata);
                            break;
                        case FUNC_NHM_XEONEP_SSE2_2T:
                            tmp = init_nhm_xeonep_sse2_2t(mydata);
                            break;
                        case FUNC_BLD_OPTERON_FMA4_1T:
                            tmp = init_bld_opteron_fma4_1t(mydata);
                            break;
                        default:
                            fprintf(stderr, "Error: unknown function %i\n", mydata->FUNCTION);
                            pthread_exit(NULL);
                    }
                    if (tmp != EXIT_SUCCESS){
                        fprintf(stderr, "Error in function %i\n", mydata->FUNCTION);
                        pthread_exit(NULL);
                    } 

                }
                else{
                    tmp = 100;
                    while(tmp > 0) tmp--;
                }
                break; // end case THREAD_INIT
            case THREAD_WORK: // perform stress test
                if (old != THREAD_WORK){
                    old = THREAD_WORK;
                    global_data->ack = id + 1;

                   /* record thread's start timestamp */
                   ((threaddata_t *)threaddata)->start_tsc = timestamp();

                    /* will be terminated by watchdog 
                     * watchdog also alters mydata->addrHigh to switch between high and low load function
                     */
					FILE *config = fopen("fsconfig", "r");
					unsigned long iteration_cap = (unsigned long) NUM_ITERS;
					unsigned sec = SECONDS, usec = 50;
					double watts = WATTS, uwatts = 120;
					unsigned long freq = 0x2D00;
					double maxfreq = 4.2;
					unsigned duty = 8800;
					unsigned partitions = 4;
					char turbo = 't';
					unsigned cores_per_socket, socket;
					if (config == NULL)
					{
							fprintf(stderr, "Error opening config file, using defaults\n");
					}
					else
					{
						fscanf(config, "%lu\n", &iteration_cap);
						fscanf(config, "%u\n", &sec);
						fscanf(config, "%lf\n", &watts);
						fscanf(config, "%u\n", &usec);
						fscanf(config, "%lf\n", &uwatts);
						fscanf(config, "%lx\n", &freq);
						fscanf(config, "%c\n", &turbo);	
						fscanf(config, "%u\n", &duty);	
						fscanf(config, "%u\n", &partitions);	
						fscanf(config, "%lf\n", &maxfreq);
						fscanf(config, "%u", &cores_per_socket);
						freq &= 0xFFFFUL;
						fprintf(stderr, "Using Config: %lu, %u, %lf, %u, %lf, %lx, %c, %u, %u, %lf\n",
						iteration_cap, sec, watts, usec, uwatts, freq, turbo, duty, partitions, maxfreq);
						fclose(config);
					}

					if (affinity > cores_per_socket)
					{
						affinity = affinity - cores_per_socket;
						socket = 1;
					}
					else
					{
						socket = 0;
					}

					unsigned long num_iters = 0;
					if (((threaddata_t *) threaddata)->msrdata == NULL)
					{
							((threaddata_t *) threaddata)->msrdata = (msrdata_t *) malloc(iteration_cap * sizeof(msrdata_t));
							if (((threaddata_t *) threaddata)->msrdata == NULL)
							{
								printf("ERROR: thread %u unable to allocate\n", ((threaddata_t *) threaddata)->cpu_id);
							}
							((threaddata_t *) threaddata)->iter = 0;
					}
					int res;
					uint64_t perfstat, inst_ret, inst_ret_a;
					uint64_t low, high, low_a, high_a;
					uint64_t energy, energy_a, pp0, pp0_a;
					uint64_t aperf, aperf_a, mperf, mperf_a, perf;
					uint64_t mperf_tot, aperf_tot, mperf_tot_a, aperf_tot_a;
					uint64_t power_unit;
					struct timeval time_before;
					unsigned long before, after;

					uint64_t unit = 0;
					uint64_t turbo_ratio_limit = 0;
					double energy_unit = 0.0;
					uint64_t ctrl = (0x3UL) | (0x1UL << 4) | (0x1UL << 8);
#ifdef MCK
					syscall(READ, ENERGY_UNIT, &unit);
					syscall(WRITE, FIXED_CTR_CTRL, &ctrl);
#endif
#ifndef MCK
					read_msr_by_coord(socket, affinity, 0, ENERGY_UNIT, &unit);
					write_msr_by_coord(socket, affinity, 0, FIXED_CTR_CTRL, ctrl);
#endif
					energy_unit = 1.0 / (0x1 << ((unit & 0x1F00) >> 8));

					//struct timeval profa, profb;
					if (affinity == 0)
					{
						//gettimeofday(&profb, NULL);
						uint64_t old_perf;
#ifdef MCK
						syscall(READ, PERF_CTL, &old_perf);
#endif
#ifndef MCK
						read_msr_by_coord(socket, affinity, 0, PERF_CTL, &old_perf);
#endif
						//disable turbo
						//perf = perf | 0x100000000UL;
						// this enables turbo
						//perf = perf & (~0x100000000UL);
						if (turbo == 't')
						{
							perf = (old_perf & 
								0xFFFFFFFEFFFF0000UL) |
								(freq & 0xFFFFUL);
						}
						else
						{
							perf = (old_perf & 
								0xFFFFFFFFFFFF0000UL) |
								(freq & 0xFFFFUL) | 
								0x100000000UL;
						}

						gettimeofday(&time_before, NULL);
#ifdef MCK
						syscall(WRITE, PERF_CTL, &perf);
						syscall(READ, ENERGY_STATUS, &energy);
						syscall(READ, ENERGY_PP0, &pp0);
						syscall(READ, APERF, &aperf_tot);
						syscall(READ, MPERF, &mperf_tot);
#endif
#ifndef MCK
						write_msr_by_coord(socket, affinity, 0, PERF_CTL, perf);
						read_msr_by_coord(socket, affinity, 0, ENERGY_STATUS, &energy);
						read_msr_by_coord(socket, affinity, 0, ENERGY_PP0, &pp0);
						read_msr_by_coord(socket, affinity, 0, APERF, &aperf_tot);
						read_msr_by_coord(socket, affinity, 0, MPERF, &mperf_tot);
#endif
						power_unit = unit & 0xF;
						double pu = 1.0 / (0x1 << power_unit);
						fprintf(stderr, "power unit: %lx\n", power_unit);
						uint64_t seconds_unit = (unit >> 16) & 0x1F;
						double su = 1.0 / (0x1 << seconds_unit);
						fprintf(stderr, "seconds unit: %lx\n", seconds_unit);
						uint64_t power = (unsigned long) (watts / pu);
						uint64_t upower = (unsigned long) (uwatts / pu);
						uint64_t seconds;
						uint64_t timeval_y = 0, timeval_x = 0;
						double logremainder = 0;
						if (sec > 40)
						{
							fprintf(stderr, "ERROR: seconds too high\n");
							//sec = 40;
						}
						if (usec > 127)
						{
							fprintf(stderr, "ERROR: usec too high\n");
							usec = 127;
						}
						timeval_y = (uint64_t) log2(sec / su);
						fprintf(stderr, "time unit is %lf, field 1 is %lx\n", su, timeval_y);
						// store the mantissa of the log2
						logremainder = (double) log2(sec / su) - (double) timeval_y;
						timeval_x = 0;
						// based on the mantissa, we can choose the appropriate multiplier
						if (logremainder > 0.15 && logremainder <= 0.45)
						{
								timeval_x = 1;
						}
						else if (logremainder > 0.45 && logremainder <= 0.7)
						{
								timeval_x = 2;
						}
						else if (logremainder > 0.7)
						{
								timeval_x = 3;
						}
						// store the bits in the Intel specified format
						seconds = (uint64_t) (timeval_y | (timeval_x << 5));
						uint64_t rapl = power | (seconds << 17);
						uint64_t urapl = upower | (usec << 17);
						urapl |= (1LL << 15) | (1LL << 16);
						urapl <<= 32;
						rapl |= urapl;

						if (power & 0xFFFFFFFFFFFF8000)
						{
							fprintf(stderr, "ERROR: power\n");
						}
						if (seconds & 0xFFFFFFFFFFFFFF80)
						{
							fprintf(stderr, "ERROR: seconds\n");
						}

						rapl |= (1LL << 15) | (1LL << 16);
						fprintf(stderr, "RAPL is: %lx\n", rapl);
#ifdef MCK
						syscall(WRITE, POWER_LIMIT, &rapl);
#endif
#ifndef MCK
						write_msr_by_coord(socket, affinity, 0, POWER_LIMIT, rapl);
#endif
					}
					else
					{
						// give the other threads something to do
						// to keep threads closer in sync
						usleep(100);	
					}
					short workload = 0;
					unsigned enr_samp_counter = 0;
					double *pow_dat = (double *) calloc(1024, sizeof(double));
					struct timeval psamp_b, psamp_a;
					// start the state at one since itr starts at 1
					short state = 1;
					uint64_t last = 0;
#ifdef MCK
					syscall(READ, ENERGY_STATUS, &last);
#endif
#ifndef MCK
					read_msr_by_coord(socket, affinity, 0, ENERGY_STATUS, &last);
#endif

					gettimeofday(&psamp_b, NULL);
										
					// barrier to keep threads in sync
					barrier(barrier_affinity, ((threaddata_t *)threaddata));
										
					for (num_iters = 0; num_iters < iteration_cap; num_iters++) 
					{
						((threaddata_t *) threaddata)->iter++;
						if (!(((threaddata_t *) threaddata)->iter % (duty / 4)))
						{
							// barrier to keep threads in sync
							barrier(barrier_affinity, ((threaddata_t *)threaddata));
							uint64_t enr;
#ifdef MCK
							syscall(READ, ENERGY_STATUS, &enr);
#endif
#ifndef MCK
							read_msr_by_coord(socket, affinity, 0, ENERGY_STATUS, &enr);
#endif
							gettimeofday(&psamp_a, NULL);
							double t = (psamp_a.tv_sec - psamp_b.tv_sec) + (psamp_a.tv_usec - psamp_b.tv_usec) / 1000000.0;
							pow_dat[enr_samp_counter] = ((enr - last) * energy_unit) / t;

							//fprintf(stderr, "tdiff %lf, pow %lf\n", t, pow_dat[enr_samp_counter]);
							enr_samp_counter++;
							last = enr;
							psamp_b = psamp_a;
						}
						// 75% duty
						if (!(((threaddata_t *) threaddata)->iter % (duty / partitions)))
						{
							state++;
							if (state <= NUM_FS_WORKLOADS)
							{
								workload = 0;
							}
							else if ((state - NUM_FS_WORKLOADS) <= NUM_SLEEP_WORKLOADS)
							{
								workload = 1;
							}
							else
							{
								state = 0;
							}
						}
						if (workload == 1)
						{
#ifdef MCK
							syscall(READ, FIXED_CTR0, &inst_ret);
							syscall(READ, APERF, &aperf);
							syscall(READ, MPERF, &mperf);
#endif
#ifndef MCK
							read_msr_by_coord(socket, affinity, 0, FIXED_CTR0, &inst_ret);
							read_msr_by_coord(socket, affinity, 0, APERF, &aperf);
							read_msr_by_coord(socket, affinity, 0, MPERF, &mperf);
#endif
							__asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high));
							//usleep(220);
							res = intload();
							__asm__ __volatile__("rdtsc" : "=a" (low_a), "=d" (high_a));
#ifdef MCK
							syscall(READ, PERF_STAT, &perfstat);
							syscall(READ, APERF, &aperf_a);
							syscall(READ, MPERF, &mperf_a);
							syscall(READ, FIXED_CTR0, &inst_ret_a);
#endif
#ifndef MCK
							read_msr_by_coord(socket, affinity, 0, PERF_STAT, &perfstat);
							read_msr_by_coord(socket, affinity, 0, APERF, &aperf_a);
							read_msr_by_coord(socket, affinity, 0, MPERF, &mperf_a);
							read_msr_by_coord(socket, affinity, 0, FIXED_CTR0, &inst_ret_a);
#endif
							
							before = (high << 32) | low;
							after = (high_a << 32) | low_a;
							struct msrdata *ptr = &(((threaddata_t *) threaddata)->msrdata[((threaddata_t *) threaddata)->iter - 1]);
							ptr->tsc = after - before;
							ptr->aperf = aperf_a - aperf;
							ptr->mperf = mperf_a - mperf;
							ptr->retired = inst_ret_a - inst_ret;
							ptr->log = 0;
							ptr->pmc0 = 0xFFFF & perfstat;
							ptr->pmc2 = res;
							ptr->pmc3 = workload;
							continue;
						}
					//while(1)
			//printf("starting iteration %lu\n", num_iters);
						/* call high load function */
						#ifdef ENABLE_VTRACING
						VT_USER_START("HIGH_LOAD_FUNC");
						#endif
						#ifdef ENABLE_SCOREP
						SCOREP_USER_REGION_BY_NAME_BEGIN("HIGH", SCOREP_USER_REGION_TYPE_COMMON);
						#endif
#ifdef MCK
						syscall(READ, FIXED_CTR0, &inst_ret);
						syscall(READ, APERF, &aperf);
						syscall(READ, MPERF, &mperf);
#endif
#ifndef MCK
						read_msr_by_coord(socket, affinity, 0, FIXED_CTR0, &inst_ret);
						read_msr_by_coord(socket, affinity, 0, APERF, &aperf);
						read_msr_by_coord(socket, affinity, 0, MPERF, &mperf);
#endif
						__asm__ __volatile__("rdtsc" : "=a" (low), "=d" (high));
						res = 0x12345;
						switch (mydata->FUNCTION)
						{
							case FUNC_KNL_XEONPHI_AVX512_4T:
								tmp = asm_work_knl_xeonphi_avx512_4t(mydata);
								break;
							case FUNC_SKL_COREI_FMA_1T:
								tmp = asm_work_skl_corei_fma_1t(mydata);
								break;
							case FUNC_SKL_COREI_FMA_2T:
								tmp = asm_work_skl_corei_fma_2t(mydata);
								break;
							case FUNC_HSW_COREI_FMA_1T:
								tmp = asm_work_hsw_corei_fma_1t(mydata);
								break;
							case FUNC_HSW_COREI_FMA_2T:
								tmp = asm_work_hsw_corei_fma_2t(mydata);
								break;
							case FUNC_HSW_XEONEP_FMA_1T:
								tmp = asm_work_hsw_xeonep_fma_1t(mydata);
								break;
							case FUNC_HSW_XEONEP_FMA_2T:
								tmp = asm_work_hsw_xeonep_fma_2t(mydata);
								break;
							case FUNC_SNB_COREI_AVX_1T:
								tmp = asm_work_snb_corei_avx_1t(mydata);
								break;
							case FUNC_SNB_COREI_AVX_2T:
								tmp = asm_work_snb_corei_avx_2t(mydata);
								break;
							case FUNC_SNB_XEONEP_AVX_1T:
								tmp = asm_work_snb_xeonep_avx_1t(mydata);
								break;
							case FUNC_SNB_XEONEP_AVX_2T:
								tmp = asm_work_snb_xeonep_avx_2t(mydata);
								break;
							case FUNC_NHM_COREI_SSE2_1T:
								tmp = asm_work_nhm_corei_sse2_1t(mydata);
								break;
							case FUNC_NHM_COREI_SSE2_2T:
								tmp = asm_work_nhm_corei_sse2_2t(mydata);
								break;
							case FUNC_NHM_XEONEP_SSE2_1T:
								tmp = asm_work_nhm_xeonep_sse2_1t(mydata);
								break;
							case FUNC_NHM_XEONEP_SSE2_2T:
								tmp = asm_work_nhm_xeonep_sse2_2t(mydata);
								break;
							case FUNC_BLD_OPTERON_FMA4_1T:
								tmp = asm_work_bld_opteron_fma4_1t(mydata);
								break;
							default:
								fprintf(stderr,"Error: unknown function %i\n",mydata->FUNCTION);
								pthread_exit(NULL);
						}
						__asm__ __volatile__("rdtsc" : "=a" (low_a), "=d" (high_a));
#ifdef MCK
						syscall(READ, PERF_STAT, &perfstat);
						syscall(READ, APERF, &aperf_a);
						syscall(READ, MPERF, &mperf_a);
						syscall(READ, FIXED_CTR0, &inst_ret_a);
#endif
#ifndef MCK
						read_msr_by_coord(socket, affinity, 0, PERF_STAT, &perfstat);
						read_msr_by_coord(socket, affinity, 0, APERF, &aperf_a);
						read_msr_by_coord(socket, affinity, 0, MPERF, &mperf_a);
						read_msr_by_coord(socket, affinity, 0, FIXED_CTR0, &inst_ret_a);
#endif
						((threaddata_t *) threaddata)->msrdata[((threaddata_t *) threaddata)->iter - 1].pmc0 = perfstat & 0xFFFF;
						before = (high << 32) | low;
						after = (high_a << 32) | low_a;
						struct msrdata *ptr = &(((threaddata_t *) threaddata)->msrdata[((threaddata_t *) threaddata)->iter - 1]);
						ptr->tsc = after - before;
						ptr->aperf = aperf_a - aperf;
						ptr->mperf = mperf_a - mperf;
						ptr->log = 0;
						ptr->retired = inst_ret_a - inst_ret;
						ptr->pmc0 = 0xFFFF & perfstat;
						ptr->pmc2 = res; // dummy value to prevent optimization
						ptr->pmc3 = workload;
						/*
						((threaddata_t *) threaddata)->msrdata[((threaddata_t *) threaddata)->iter - 1].tsc = after - before;
						((threaddata_t *) threaddata)->msrdata[((threaddata_t *) threaddata)->iter - 1].aperf = aperf_a - aperf;
						((threaddata_t *) threaddata)->msrdata[((threaddata_t *) threaddata)->iter - 1].mperf = mperf_a - mperf;
						((threaddata_t *) threaddata)->msrdata[((threaddata_t *) threaddata)->iter - 1].retired = inst_ret_a - inst_ret;
						((threaddata_t *) threaddata)->msrdata[((threaddata_t *) threaddata)->iter - 1].pmc0 = perfstat & 0xFFFF;
						((threaddata_t *) threaddata)->msrdata[((threaddata_t *) threaddata)->iter - 1].pmc2 = res;
						*/

						if(tmp != EXIT_SUCCESS){
							fprintf(stderr, "Error in function %i\n", mydata->FUNCTION);
							pthread_exit(NULL);
						}

						/* call low load function */
						#ifdef ENABLE_VTRACING
						VT_USER_END("HIGH_LOAD_FUNC");
						VT_USER_START("LOW_LOAD_FUNC");
						#endif
						#ifdef ENABLE_SCOREP
						SCOREP_USER_REGION_BY_NAME_END("HIGH");
						SCOREP_USER_REGION_BY_NAME_BEGIN("LOW", SCOREP_USER_REGION_TYPE_COMMON);
						#endif
						low_load_function(mydata->addrHigh, mydata->period);
						#ifdef ENABLE_VTRACING
						VT_USER_END("LOW_LOAD_FUNC");
						#endif
						#ifdef ENABLE_SCOREP
						SCOREP_USER_REGION_BY_NAME_END("LOW");
						#endif

						/* terminate if master signals end of run */
						if(*((volatile unsigned long long *)(mydata->addrHigh)) == LOAD_STOP) {
							((threaddata_t *)threaddata) -> stop_tsc = timestamp();

							pthread_exit(NULL);
						}
			//gettimeofday(&profa, NULL);
			//double tprof = (profa.tv_sec - profb.tv_sec) * 1000000.0 + (profa.tv_usec - profb.tv_usec);
			//printf("%d: proftime %lfus\n", affinity, tprof);
				
					} // end while
					unsigned long delta_joules, delta_pp0;
					struct timeval time_after;
					if (affinity == 0)
					{ 
						gettimeofday(&time_after, NULL);
						printf("energy unit is: %lf\n", energy_unit);
#ifdef MCK
						syscall(READ, ENERGY_STATUS, &energy_a);
						syscall(READ, ENERGY_PP0, &pp0_a);
						syscall(READ, APERF, &aperf_tot_a);
						syscall(READ, MPERF, &mperf_tot_a);
#endif
#ifndef MCK
						read_msr_by_coord(socket, affinity, 0, ENERGY_STATUS, &energy_a);
						read_msr_by_coord(socket, affinity, 0, ENERGY_PP0, &pp0_a);
						read_msr_by_coord(socket, affinity, 0, APERF, &aperf_tot_a);
						read_msr_by_coord(socket, affinity, 0, MPERF, &mperf_tot_a);
#endif
						if (energy_a - energy < 0)
						{
							delta_joules = energy_a + MAX_JOULES - energy;
						}
						else
						{
							delta_joules = energy_a - energy;
						}
						if (pp0_a - pp0 < 0)
						{
							delta_pp0 = pp0_a + MAX_JOULES - pp0;
						}
						else
						{
							delta_pp0 = pp0_a - pp0;
						}
						uint64_t therm_stat = 0, therm_int = 0;
						uint64_t core_therm = 0;
						uint64_t pow_info = 0;
						uint64_t ratio_limit1, ratio_limit2;
#ifdef MCK
						syscall(READ, TURBO_LIMIT, &turbo_ratio_limit);
						syscall(READ, THERM_STAT, &therm_stat);
						syscall(READ, THERM_INT, &therm_int);
						syscall(READ, THERM_CORE, &core_therm);
						syscall(READ, POWER_INFO, &pow_info);
#endif
#ifndef MCK
						read_msr_by_coord(socket, affinity, 0, TURBO_LIMIT0, &turbo_ratio_limit);
						read_msr_by_coord(socket, affinity, 0, TURBO_LIMIT1, &ratio_limit1);
						read_msr_by_coord(socket, affinity, 0, TURBO_LIMIT2, &ratio_limit2);
						read_msr_by_coord(socket, affinity, 0, THERM_STAT, &therm_stat);
						read_msr_by_coord(socket, affinity, 0, THERM_INT, &therm_int);
						read_msr_by_coord(socket, affinity, 0, THERM_CORE, &core_therm);
						read_msr_by_coord(socket, affinity, 0, POWER_INFO, &pow_info);
#endif
						double time = (time_after.tv_sec - time_before.tv_sec) + (time_after.tv_usec - time_before.tv_usec) / 1000000.0;
						printf("TIME: %lf\n", time);
						printf("POWER: %lf\n", delta_joules * energy_unit / time);
						printf("PP0: %lf\n", delta_pp0 * energy_unit / time);
						printf("FREQ: %lf\n", (double) (aperf_a - aperf) / (double) (mperf_a - mperf) * maxfreq);
						printf("1 core limit: %f\n", (float) (turbo_ratio_limit & 0xFF));
						printf("2 core limit: %f\n", (float) ((turbo_ratio_limit & 0xFF00) >> 8));
						printf("3 core limit: %f\n", (float) ((turbo_ratio_limit & 0xFF0000) >> 16));
						printf("4 core limit: %f\n", (float) ((turbo_ratio_limit & 0xFF000000) >> 24));
						printf("5 core limit: %f\n", (float) ((turbo_ratio_limit & 0xFF00000000) >> 32));
						printf("6 core limit: %f\n", (float) ((turbo_ratio_limit & 0xFF0000000000) >> 40));
						printf("7 core limit: %f\n", (float) ((turbo_ratio_limit & 0xFF000000000000) >> 48));
						printf("8 core limit: %f\n", (float) ((turbo_ratio_limit & 0xFF00000000000000) >> 56));

						printf("9 core limit: %f\n", (float) (ratio_limit1 & 0xFF));
						printf("10 core limit: %f\n", (float) ((ratio_limit1 & 0xFF00) >> 8));
						printf("11 core limit: %f\n", (float) ((ratio_limit1 & 0xFF0000) >> 16));
						printf("12 core limit: %f\n", (float) ((ratio_limit1 & 0xFF000000) >> 24));
						printf("13 core limit: %f\n", (float) ((ratio_limit1 & 0xFF00000000) >> 32));
						printf("14 core limit: %f\n", (float) ((ratio_limit1 & 0xFF0000000000) >> 40));
						printf("15 core limit: %f\n", (float) ((ratio_limit1 & 0xFF000000000000) >> 48));
						printf("16 core limit: %f\n", (float) ((ratio_limit1 & 0xFF00000000000000) >> 56));

						printf("17 core limit: %f\n", (float) (ratio_limit2 & 0xFF));
						printf("18 core limit: %f\n", (float) ((ratio_limit2 & 0xFF00) >> 8));
						printf("19 core limit: %f\n", (float) ((ratio_limit2 & 0xFF0000) >> 16));
						printf("20 core limit: %f\n", (float) ((ratio_limit2 & 0xFF000000) >> 24));
						printf("21 core limit: %f\n", (float) ((ratio_limit2 & 0xFF00000000) >> 32));
						printf("22 core limit: %f\n", (float) ((ratio_limit2 & 0xFF0000000000) >> 40));
						printf("23 core limit: %f\n", (float) ((ratio_limit2 & 0xFF000000000000) >> 48));
						printf("24 core limit: %f\n", (float) ((ratio_limit2 & 0xFF00000000000000) >> 56));

						printf("TEMPERATURE: %lu\n", ((therm_int >> 8) & 0x3F) - ((therm_stat >> 22) & 0x3F));
						printf("CRIT TEMP: %lu\n", ((therm_int >> 8) & 0x3F));
						printf("CORE THERM 0: %lu\n", core_therm & 0x1);
						printf("CORE THERM 1: %lu\n", (core_therm & 0x2) >> 1);
						printf("CORE THERM 2: %lu\n", (core_therm & 0x4) >> 2);
						printf("CORE THERM 3: %lu\n", (core_therm & 0x8) >> 3);
						printf("CORE THERM 4: %lu\n", (core_therm & 0x10) >> 4);
						printf("CORE THERM 5: %lu\n", (core_therm & 0x20) >> 4);
						printf("CORE THERM 6: %lu\n", (core_therm & 0x40) >> 4);
						printf("CORE THERM 7: %lu\n", (core_therm & 0x80) >> 4);
						printf("CORE THERM 8: %lu\n", (core_therm & 0x160) >> 4);
						printf("CORE THERM 9: %lu\n", (core_therm & 0x320) >> 4);
						printf("CORE TEMP: %lu\n", (core_therm >> 16) & 0x3F);
						printf("MIN RAPL POW: %lu (%lx)\n", ((pow_info >> 16) & 0x3FFF) / 8, pow_info); //power_unit);
					}
					fflush(stdout);
					char fname[64];
					snprintf(fname, 64, "core%d.msrdat", affinity);
					FILE * out = fopen(fname, "w");
					fprintf(out, "tsc\tretired\taperf\tmperf\tfreq\tlog\tstat\tworkload\n");
					for (num_iters = 0; num_iters < iteration_cap; num_iters++)
					{
					fprintf(out, "%lu\t%lu\t%lu\t%lu\t%.1lf\t%lx\t%lx\t%lu\n",
						((threaddata_t *) threaddata)->msrdata[num_iters].tsc,
						((threaddata_t *) threaddata)->msrdata[num_iters].retired,
						((threaddata_t *) threaddata)->msrdata[num_iters].aperf,
						((threaddata_t *) threaddata)->msrdata[num_iters].mperf,
						(double) (((threaddata_t *) threaddata)->msrdata[num_iters].aperf) /
						(double) (((threaddata_t *) threaddata)->msrdata[num_iters].mperf) *
						maxfreq,
						((threaddata_t *) threaddata)->msrdata[num_iters].log,
						((threaddata_t *) threaddata)->msrdata[num_iters].pmc0,
						((threaddata_t *) threaddata)->msrdata[num_iters].pmc3);
						//((threaddata_t *) threaddata)->msrdata[num_iters].pmc1);
					}
					fflush(out);
					fclose(out);
					free(((threaddata_t *) threaddata)->msrdata);
					snprintf(fname, 64, "core%d.pow", affinity);
					out = fopen(fname, "w");
					unsigned itr = 0;
					while (pow_dat[itr] != 0)
					{
					fprintf(out, "%lf\n", pow_dat[itr]);
					itr++;
					}
					free(pow_dat);
					fclose(out);
					pthread_exit(NULL);
                }
                else{
                    tmp = 100;
                    while(tmp > 0) tmp--;
                }
                break; //end case THREAD_WORK
            case THREAD_WAIT:
                if(old != THREAD_WAIT){
                    old = THREAD_WAIT;
                    global_data->ack = id + 1;
                }
                else { 
                    tmp = 100;
                    while(tmp > 0) tmp--;
                }
                break;
            case THREAD_STOP: // exit
            default:
                pthread_exit(0);
        } 
    }
}

int intload()
{
	int a, b, c;
	unsigned itr;
	for (itr = 0; itr < 550000; itr++)
	{
		__asm__ __volatile__(
			"addq %%rdx, %%rax\n\t"
			"addq %%rax, %%rbx\n\t"
			"addq %%rbx, %%rcx\n\t"
			: "=a" (a), "=b" (b), "=c" (c)
			: "d" (itr)
			:
		);
	}
	return a | b | c;
}

