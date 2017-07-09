#ifndef __TXDRIVER__
#define __TXDRIVER__

#include <stdio.h>

#define TXDRIVER_SUCCESS    0
#define TXDRIVER_FAIL       1

#define TXD_FAIL(res) assert(res!=TXDRIVER_FAIL)
#define TXD_PARAMS struct txd_spdk_params
#define CHK_PARAMS(params) \
    if(!params || !params->buf)return TXDRIVER_FAIL;

struct txd_spdk_params{
    char *      buf;
    int         buf_size;
    int         buf_align;
    uint64_t    lba;           // starting logical block address to write the data.
    uint32_t    lba_count;     // length(in sectors) for the write operation. 
};

struct spdk_sequence {
	struct ns_entry	*ns_entry;
    char            *buf;    
	char		    *buf_user;
    int             buf_size;
	int		        is_completed;
};

struct nvfuse_io_manager {
    char *io_name;
    char *dev_path;
    int type;
    char *ramdisk;
    FILE *fp;
    int dev;

    int blk_size; /* 512B, 4KB */
    long start_blk; /* start block (512B)*/
    long total_blkcount; /* number of sectors (512B) */
    int cpu_core_mask; /* cpu core mask for SPDK */
 
    int (*TxInit)();   
    int (*TxBegin)();   
    int (*TxWrite)();   
    int (*TxCommit)();    
    int (*TxAbort)();   

};


#endif
