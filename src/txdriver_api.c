
#include "txdriver.h"

/*
 * INFO
 * @txd_addr : journal start address.( B unit )
 * @txd_size : journal area size. ( B unit )
 */
int tx_format(uint64_t txd_addr, uint32_t txd_size)
{    
    TXD_PARAMS params;
    txd_j_superblock_t *j_sb = NULL;
    int ret = TXD_FAIL;

    ret = spdk_init(); 
    TXD_CHK_FAIL(ret);
   
    ret = spdk_alloc_qpair(); 
    TXD_CHK_FAIL(ret);

    /*
     * initialize superblock which will be written on disk. 
     */
    char *buf = (char*)calloc(txd_size * LBA_UNIT, sizeof(char));
    j_sb = (txd_j_superblock_t*)buf;
    init_journal_header(&j_sb->s_header, SUPER_BLOCK, 0);
    j_sb->s_blocksize = BLK_SIZE;
    j_sb->s_maxlen = txd_size;
    j_sb->s_first = 1;
    j_sb->s_sequence = 0;
    j_sb->s_start = 0;
    j_sb->s_errno = 0;

    params.buf = (char*)j_sb;
    params.buf_size = sizeof(txd_j_superblock_t);
    params.buf_align = CLSIZE;
    params.lba = txd_addr/LBA_UNIT;
    params.lba_count = txd_size/LBA_UNIT;

    ret = do_format(&params);
    return ret; 
}

int tx_begin(uint64_t txd_addr, uint32_t txd_size)
{
    txd_j_superblock_t *j_sb = NULL;
    TXD_PARAMS params;
    int ret = TXD_FAIL;

//    ret = spdk_init(); 
//    TXD_CHK_FAIL(ret);
   
    ret = spdk_alloc_qpair(); 
    TXD_CHK_FAIL(ret);

    /*
     * get super block from disk.
     */
    params.buf = (char*)calloc(1, sizeof(txd_j_superblock_t));
    params.buf_size = sizeof(txd_j_superblock_t);
    params.buf_align = CLSIZE;
    params.lba = txd_addr;
    params.lba_count = 1;    

    ret = spdk_read(&params);
    assert(ret != TXD_FAIL);
  
    j_sb = (txd_j_superblock_t*)params.buf;

    if(!chk_txd_sb(j_sb, txd_size)){
        printf("\n### Journal super block detected ! ###\n");
        print_sb_info(j_sb);
    }
    else
        printf("\n###  There is no txd journal super block ###\n");

    ret = spdk_free(); 
    assert(ret != TXD_FAIL);
    cleanup();

   return ret; 
}

int tx_write(uint8_t meta_buffer, uint8_t data_buffer)
{

    return TXD_SUCCESS; 
}

int tx_commit(void)
{

    return TXD_SUCCESS; 
}

int tx_abort(void)
{

    return TXD_SUCCESS; 

}



