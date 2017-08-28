
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
    char *buf = NULL;

    err = spdk_init(); 
    TXD_CHK_FAIL(err);
   
    err = spdk_alloc_qpair(); 
    TXD_CHK_FAIL(err);

    /*
     * initialize superblock which will be written on disk. 
     */
    buf = (char*)txd_zmalloc_spdk(sizeof(txd_j_superblock_t));
    j_sb = (txd_j_superblock_t*)buf;
    init_journal_header(&j_sb->s_header, SUPER_BLOCK, 0);
    j_sb->s_blocksize = BLK_SIZE;
    j_sb->s_maxlen = txd_size;
    j_sb->s_first = 1;
    j_sb->s_sequence = 0;
    j_sb->s_start = 0;
    j_sb->s_errno = 0;

    params.buf = buf;
    params.buf_size = sizeof(txd_j_superblock_t);
    params.buf_align = CLSIZE;
    params.lba = txd_addr;
    params.lba_count = 1;

    fprintf(stdout,"\n### Journal super block formatted ! ###\n\n");

out:
#if 0
    err = spdk_free();
    assert(err != TXD_FAIL);
    cleanup();
#endif
    txd_free_spdk(params.buf, sizeof(txd_j_superblock_t));

    return err; 
}

txd_journal_t* tx_begin(uint64_t txd_addr, uint32_t txd_size)
{
    txd_j_superblock_t *j_sb = NULL;
    txd_journal_t *journal = NULL;
    TXD_PARAMS params;
    int err = TXD_FAIL;
    char *buf = NULL;

#if 0
    err = spdk_init(); 
    TXD_CHK_FAIL(err);
   
    err = spdk_alloc_qpair(); 
    TXD_CHK_FAIL(err);
#endif

    /*
     * get super block from disk.
     */
    buf = (char*)txd_calloc(1,sizeof(txd_j_superblock_t));
    if(unlikely(buf == NULL))
        goto out1;

    params.buf = buf;
    params.buf_size = sizeof(txd_j_superblock_t);
    params.buf_align = CLSIZE;
    params.lba = txd_addr;
    params.lba_count = 1;    

    err = spdk_read(&params);
    if(unlikely(err == TXD_FAIL))
        goto out2;
  
    j_sb = (txd_j_superblock_t*)params.buf;

    if(!chk_txd_sb(j_sb, txd_size)){
        fprintf(stdout,"\n### Journal super block detected ! ###\n\n");
        print_sb_info(j_sb);

        /* now I need to create txd_journal_t to manage journal information.*/       
        journal = txd_get_journal(j_sb);
        if(unlikely(journal == NULL)){
            perror("Can't allocate txd_journal_t ");
            goto out2;
        }
        /*
         * TODO
         *  need to implement this function below.
         *       - txd_journal_needs_recovery()
         *       - txd_wipe_journal_log()
         */
        if(!txd_journal_need_recovery(journal))
            err = txd_wipe_journal_log(journal);

        if(unlikely(err == TXD_FAIL)){
            perror("Can't load journal ");
            txd_journal_destroy(journal);
            goto out2;           
        }

        txd_journal_load(journal);
    }
    else{
        fprintf(stdout,"\n### txd_j_superblock_t allocation fault ###\n\n");
        goto out2;
    }

    /* 
     * now I can start journaling mechanism
     * first I need to allocate txd_journal_t structure.
     */

    return journal;
out2:
    txd_free(buf, sizeof(txd_j_superblock_t)*1);
    /* freeing  */
out1:    
    fprintf(stdout,"\n### Journal super block detected ! ###\n\n");

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



