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


#define TXD_J_COMMIT_INTERVAL   5
#define TXD_J_MIN_BATCH_TIME    0
#define TXD_J_MAX_BATCH_TIME    15000  // micro second unit. -> 15 milli second

/*
 * create journal_t 
 *  @start : journal start blocknr.
 *  @len : journal area's total block size.
 *  @blocksize : blocksize
 */
txd_journal_t *txd_journal_init_common(txd_j_superblock_t *j_sb){
    txd_journal_t *journal_t = NULL;
    struct txd_buffer_head *txd_bh = NULL; 
	int err, n;

    journal_t = (txd_journal_t*)calloc(1,sizeof(txd_journal_t*));
	if (!journal_t)
		return NULL;

    txd_bh = (struct txd_buffer_head*)calloc(1, sizeof(struct txd_buffer_head));
    if(!txd_bh)
        goto out;

    /*
     * TODO 
     *  - set buffer head related structure.
     */
    txd_bh->bh_buf = (char*)j_sb;

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
		goto out;

	/* journal descriptor can store up to n blocks -bzzz */
	journal_t->j_blk_offset = j_sb->s_first;
	journal_t->j_maxlen = j_sb->s_maxlen;
	journal_t->j_blocksize = j_sb->s_blocksize;

    journal_t->j_tail_sequence = j_sb->s_sequence; // set last sequence id to first commit sequence id.
    journal_t->j_tail =  j_sb->s_start; // set oldest log blocknr to first log blocknr
	journal_t->j_first = j_sb->s_first; // set first journal blocknr
	journal_t->j_last = j_sb->s_maxlen; // set last journal blocknr
	journal_t->j_errno = j_sb->s_errno; // set by txd_journal_abort()

    /* 
     * how many block tag can be in a single journal block.  
     *
     * commit bufer head array length 
     *  : journal blocksize / descriptor blocksize
     */
	n = journal_t->j_blocksize / sizeof(txd_journal_block_tag_t);
	journal_t->j_wbufsize = n;
    /*FIXME*/
	journal_t->j_wbuf = (struct txd_buffer_head**)calloc(n, sizeof(struct txd_buffer_head*));

	if (!journal_t->j_wbuf)
		goto out;
#if 0
	bh = getblk(start, journal_t->j_blocksize);
	if (!bh) {
		printf("    Cannot get buffer for journal superblock\n");
		goto out;
	}
#endif
	journal_t->j_sb_buffer = txd_bh;
	journal_t->j_superblock = (txd_j_superblock_t *)txd_bh->bh_buf;

	return journal_t;

out:
	free(journal_t->j_wbuf);
	txd_journal_destroy_revoke(journal_t);
	free(journal_t);
	return NULL;
}


txd_journal_t* txd_get_journal(txd_j_superblock_t *j_sb){
    txd_journal_t *journal = NULL;

    assert(j_sb != NULL);

    journal =  txd_journal_init_common(j_sb);

    if(!journal)
        return NULL;

    journal->j_commit_interval = TXD_J_COMMIT_INTERVAL;
    journal->j_min_batch_time = TXD_J_MIN_BATCH_TIME;
    journal->j_max_batch_time = TXD_J_MAX_BATCH_TIME;

    /*
     * NOTE 
     * In kernel code flow looks below .. should I consider this...?
     */
#if 0     
	write_lock(&journal->j_state_lock);
	if (test_opt(sb, BARRIER))
		journal->j_flags |= JBD2_BARRIER;
	else
		journal->j_flags &= ~JBD2_BARRIER;
	if (test_opt(sb, DATA_ERR_ABORT))
		journal->j_flags |= JBD2_ABORT_ON_SYNCDATA_ERR;
	else
		journal->j_flags &= ~JBD2_ABORT_ON_SYNCDATA_ERR;
	write_unlock(&journal->j_state_lock);
#endif

    return journal;
}

/*
 * TODO
 * If there is any log to recover, to it in this function.
 */
int txd_journal_recover(txd_journal_t *journal){
     int err = TXD_FAIL; 
    return err;
   
}

/*
 * TODO
 * Do I have to redo logs?
 */
int txd_journal_need_recovery(txd_journal_t *journal){
    int err = TXD_TRUE;
     
    return err; 
}

/*
 * TODO
 * Ok. There is nothing to recover. I wipe out all logs in this function.
 */
int txd_wipe_journal_log(txd_journal_t *journal){
    int err = TXD_FAIL; 
    return err;
}

/*
 * TODO
 * destroy journal
 */
int txd_journal_destroy(txd_journal_t *journal){
    int err = TXD_FAIL;

    return err;

}

static int txd_journal_reset(txd_journal_t *journal){
    int err = TXD_FAIL;

    return err;
}


int txd_journal_load(txd_journal_t *journal){

    assert(journal != NULL);

 	/* Let the recovery code check whether it needs to recover any
	 * data from the journal. */
	if (txd_journal_recover(journal))
		goto out_recovery_error;

	if (journal->j_failed_commit) {
		printf("    journal transaction %u is corrupt.\n", 
                journal->j_failed_commit);
        return TXD_FAIL;
	}

    	/* OK, we've finished with the dynamic journal bits:
	 * reinitialise the dynamic contents of the superblock in memory
	 * and reset them on disk. */
	if (txd_journal_reset(journal))
		goto out_recovery_error;

	journal->j_flags &= ~TXD_ABORT;
	journal->j_flags |= TXD_LOADED;

    return TXD_SUCCESS;

out_recovery_error:
	printf("    recovery failed\n");
    return TXD_FAIL;
}


