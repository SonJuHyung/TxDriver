
#include "txdriver.h"

/*
 * INFO
 * format journal super block to txd_addr in NVMe through spdk.
 *  @txd_addr : journal start address.( B unit )
 *  @txd_size : journal area size. ( B unit )
 */
int tx_format(uint64_t txd_addr, uint32_t txd_size)
{    
    TXD_PARAMS params;
    txd_j_superblock_t *j_sb = NULL;
    int err = TXD_FAIL;

    err = spdk_init(); 
    TXD_CHK_FAIL(err);
   
    err = spdk_alloc_qpair(); 
    TXD_CHK_FAIL(err);

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

    err = do_format(&params);
    if(err != TXD_FAIL)
        printf("\n### Journal super block formatted ! ###\n\n");

#if 0
    err = spdk_free();
    assert(err != TXD_FAIL);
    cleanup();
#endif

    return err; 
}

txd_journal_t* tx_begin(uint64_t txd_addr, uint32_t txd_size)
{
    txd_j_superblock_t *j_sb = NULL;
    txd_journal_t *journal = NULL;
    TXD_PARAMS params;
    int err = TXD_FAIL;

#if 0
    err = spdk_init(); 
    TXD_CHK_FAIL(err);
   
    err = spdk_alloc_qpair(); 
    TXD_CHK_FAIL(err);
#endif

    /*
     * get super block from disk.
     */
    params.buf = (char*)calloc(1, sizeof(txd_j_superblock_t));
    params.buf_size = sizeof(txd_j_superblock_t);
    params.buf_align = CLSIZE;
    params.lba = txd_addr;
    params.lba_count = 1;    

    err = spdk_read(&params);
    assert(err != TXD_FAIL);
  
    j_sb = (txd_j_superblock_t*)params.buf;

    if(!chk_txd_sb(j_sb, txd_size)){
        printf("\n### Journal super block detected ! ###\n\n");
        print_sb_info(j_sb);

        /* now I need to create txd_journal_t to manage journal information.*/       
        journal = txd_get_journal(j_sb);
        if(journal == NULL){
            perror("Can't allocate txd_journal_t ");
            goto out;
        }

        /*
         * TODO
         *  need to implement this function below.
         *       - txd_journal_needs_recovery()
         *       - txd_wipe_journal_log()
         */
        if(!txd_journal_need_recovery(journal))
            err = txd_wipe_journal_log(journal);

        if(!err)
            err = txd_journal_load(journal);        

        if(err){
            perror("Can't load journal ");
            txd_journal_destroy(journal);
            goto out;
        }
        
    }
    else{
        printf("\n###  There is no txd journal super block ###\n\n");
        goto out;
    }

    /* 
     * now I can start journaling mechanism
     * first I need to allocate txd_journal_t structure.
     */

    return journal;
out:
    /* freeing  */
    err = spdk_free(); 
    journal = NULL;
    assert(err != TXD_FAIL);    
    cleanup();

   return journal; 
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



