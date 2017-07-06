
/* data needed by each thread */
typedef struct threaddata
{
   volatile mydata_t *data;                                 
   char* bufferMem;                 
   unsigned long long addrMem;      
   unsigned long long addrHigh;             
   unsigned long long buffersizeMem;
   unsigned long long iterations;
   unsigned long long flops;
   unsigned long long bytes;
   unsigned long long start_tsc;
   unsigned long long stop_tsc;
   unsigned int alignment;      
   unsigned int cpu_id;
   unsigned int thread_id;
   unsigned int package;
   unsigned int period;                     
   unsigned char FUNCTION;
} threaddata_t;
