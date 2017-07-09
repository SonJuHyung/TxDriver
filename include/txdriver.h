#ifndef __TXD_H__
#define __TXD_H__

#include <stdio.h>
#include <stdint.h>
#include "list.h"

 // Macro variable
#define TXD_SUCCESS    0
#define TXD_FAIL       1


//  Macro functions and structures
#define TXD_CHK_FAIL(res) assert(res!=TXD_FAIL)
#define TXD_PARAMS struct txd_spdk_params
#define CHK_PARAMS(params) \
    if(!params || !params->buf)return TXD_FAIL;

/*
 * function parameter structure which will 
 * be passed to spdk interface functions.
 */
struct txd_spdk_params{
    char *      buf;
    int         buf_size;
    int         buf_align;
    uint64_t    lba;           // starting logical block address to write the data.
    uint32_t    lba_count;     // length(in sectors) for the write operation. 
};

/*
 *  struct txd_meta is the metadata buffer which belongs to transacatino.
 */
struct txd_meta {
	/* transaction this txd_inode is related */
//	transaction_t *i_transaction;

	/* List of inodes in the i_transaction [j_list_lock] */
	struct list_head i_list;

	/* meta deta buffer in user space FS to be commited */
    char *meta_buffer;
    /* data buffer in user space related to meta_data buffer.*/
    char *data_buffer; 

    /*
     * TODO
     * need to reconsider structure.
     */
};


/* functions in "txdriver_api.c" which are txdriver core functions. */
int tx_begin(uint64_t txd_addr, uint32_t txd_size); 
int tx_write(uint8_t meta_buffer, uint8_t data_buffer);

/*
 * TODO
 * need to consider params
 */
int tx_commit(void);
int tx_abort(void);

/* functions in txdriver_spdk.c  */
int spdk_init(void);
int spdk_alloc_qpair(void);
int spdk_write(TXD_PARAMS *params);
int spdk_read(TXD_PARAMS *params);
int spdk_free(void);



#endif
