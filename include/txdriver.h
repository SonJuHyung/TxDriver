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


int spdk_init(void);
int spdk_alloc_qpair(void);
int spdk_write(TXD_PARAMS *params);
int spdk_read(TXD_PARAMS *params);
int spdk_free(void);

#endif
