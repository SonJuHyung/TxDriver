#include "txdriver.h"

int do_format(TXD_PARAMS *params){
    
    int err = TXD_FAIL;
      
    assert(params!=NULL);

    printf("\n ### Formating Info ### \n");
    printf("    lba : %ld \n",params->lba * LBA_UNIT );
    printf("    lba_count : %d \n",params->lba_count * LBA_UNIT);

    err = spdk_write(params);
    if(err == TXD_FAIL)
        printf("\n ### Format failed !!! exiting program... ### \n");

    err = spdk_free();
    assert(err != TXD_FAIL);
    cleanup();
    return err;
}

/*
 * NOTE
 *  - format size of txd_size starting from rxd_addr
 */
void init_journal_header(txd_j_header_t *txd_j_header,txd_type_t blk_type,int tid ){

    /*
     * DEBUG
     */
    assert(txd_j_header != NULL);

    txd_j_header->txd_signature = SIG_TXD;
    txd_j_header->txd_t_id = tid;

    switch(blk_type){
        case DESCRIPTOR_BLOCK : 
            txd_j_header->txd_blocktype =  SIG_DESCRIPTOR_BLOCK;            
            break;
        case COMMIT_BLOCK :
            txd_j_header->txd_blocktype =  SIG_COMMIT_BLOCK;
            break;
        case SUPER_BLOCK :
            txd_j_header->txd_blocktype =  SIG_SUPER_BLOCK;
            break;
        case REVOKE_BLOCK :
            txd_j_header->txd_blocktype =  SIG_REVOKE_BLOCK;
            break;
    }
}

int chk_txd_sb(txd_j_superblock_t *sb, uint32_t txd_size){

    assert(sb != NULL);

    txd_j_header_t *sb_h = &sb->s_header;

    if(sb_h->txd_signature == SIG_TXD 
            && sb->s_maxlen == txd_size)
        return TXD_SUCCESS;
    else
        return TXD_FAIL;
}

void print_sb_info(txd_j_superblock_t *sb){
    /* DEBUG  */
    assert(sb != NULL);

    printf("\n TXD super block information \n");

    printf("    signature       : %x\n", sb->s_header.txd_signature);
    printf("    blocktype       : %x\n", sb->s_header.txd_blocktype);
    printf("    tid             : %x\n", sb->s_header.txd_t_id);

    printf("    blocksize       : %d\n", sb->s_blocksize);
    printf("    maxlen          : %d\n", sb->s_maxlen);
    printf("    log start address : %d\n", sb->s_first);

    printf("    sequence        : %d\n", sb->s_sequence);
    printf("    journal start   : %d\n\n", sb->s_start);
}

#define TXD_TIME_INT_COMMIT 5       /* second unit       */
#define TXD_TIME_MIN_BATCH  0       /* milli second unit */
#define TXD_TIME_MAX_BATCH  15000   /* milli second unit */

void init_wait_queue(wait_queue_t *wq){
    pthread_mutex_init(&wq->wq_mtx, NULL);
    pthread_cond_init(&wq->wq_cnd, NULL);
}

struct txd_buffer_head* getblk(unsigned long long start, int blocksize){
    struct txd_buffer_head* bh = NULL;

    return bh;
}


/*
 * create journal_t 
 *
 * @start : journal start blocknr.
 * @len : journal area's total block size.
 */
txd_journal_t *journal_init_common(unsigned long long start, int len, int blocksize){
    txd_journal_t *journal_t = NULL; 
	int err;
	struct txd_buffer_head *bh;
	int n;
    journal_t = (txd_journal_t*)calloc(1,sizeof(txd_journal_t*));
	if (!journal_t)
		return NULL;

	init_wait_queue(&journal_t->j_wait_transaction_locked);
	init_wait_queue(&journal_t->j_wait_done_commit);
	init_wait_queue(&journal_t->j_wait_commit);
	init_wait_queue(&journal_t->j_wait_updates);
	init_wait_queue(&journal_t->j_wait_reserved);

    pthread_mutex_init(&journal_t->j_barrier, NULL);
	pthread_mutex_init(&journal_t->j_checkpoint_mutex, NULL);
	pthread_spin_init(&journal_t->j_revoke_lock, PTHREAD_PROCESS_PRIVATE);
	pthread_spin_init(&journal_t->j_list_lock, PTHREAD_PROCESS_PRIVATE);
	pthread_rwlock_init(&journal_t->j_state_lock, NULL);

	journal_t->j_commit_interval = TXD_TIME_INT_COMMIT;
	journal_t->j_min_batch_time = TXD_TIME_MIN_BATCH;
	journal_t->j_max_batch_time = TXD_TIME_MAX_BATCH; 
	atomic_set(&journal_t->j_reserved_credits, 0);

	/* The journal is marked for error until we succeed with recovery! */
	journal_t->j_flags = TXD_ABORT;

	/* Set up a default-sized revoke table for the new mount. */
	err = txd_journal_init_revoke(journal_t, JOURNAL_REVOKE_DEFAULT_HASH);
	if (err)
		goto err_cleanup;

	/* journal descriptor can store up to n blocks -bzzz */
	journal_t->j_blocksize = blocksize;
	journal_t->j_blk_offset = start;
	journal_t->j_maxlen = len;
    /* how many block tag can be in a single journal block.  */
	n = journal_t->j_blocksize / sizeof(txd_journal_block_tag_t);
	journal_t->j_wbufsize = n;
    /*FIXME*/
	journal_t->j_wbuf = (struct txd_buffer_head**)calloc(n, sizeof(struct txd_buffer_head*));

	if (!journal_t->j_wbuf)
		goto err_cleanup;

	bh = getblk(start, journal_t->j_blocksize);
	if (!bh) {
		printf("    Cannot get buffer for journal superblock\n");
		goto err_cleanup;
	}
	journal_t->j_sb_buffer = bh;
	journal_t->j_superblock = (txd_j_superblock_t *)bh->bh_buf;

	return journal_t;

err_cleanup:
	free(journal_t->j_wbuf);
	txd_journal_destroy_revoke(journal_t);
	free(journal_t);
	return NULL;
}

