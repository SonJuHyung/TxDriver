#include "txdriver.h"

int do_format(TXD_PARAMS *params){
    
    int err = TXD_FAIL;
      
    assert(params!=NULL);

    fprintf(stdout,"\n ### Formating Info ### \n");
    fprintf(stdout,"    lba : %ld \n",params->lba * LBA_UNIT );
    fprintf(stdout,"    lba_count : %d \n",params->lba_count * LBA_UNIT);

    err = spdk_write(params);
    if(err == TXD_FAIL)
        fprintf(stdout,"\n ### Format failed !!! exiting program... ### \n");

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

    fprintf(stdout,"\n TXD super block information \n");

    fprintf(stdout,"    signature       : %x\n", sb->s_header.txd_signature);
    fprintf(stdout,"    blocktype       : %x\n", sb->s_header.txd_blocktype);
    fprintf(stdout,"    tid             : %x\n", sb->s_header.txd_t_id);

    fprintf(stdout,"    blocksize       : %d\n", sb->s_blocksize);
    fprintf(stdout,"    maxlen          : %d\n", sb->s_maxlen);
    fprintf(stdout,"    log start address : %d\n", sb->s_first);

    fprintf(stdout,"    sequence        : %d\n", sb->s_sequence);
    fprintf(stdout,"    journal start   : %d\n\n", sb->s_start);
}

#define TXD_TIME_INT_COMMIT 5       /* second unit       */
#define TXD_TIME_MIN_BATCH  0       /* milli second unit */
#define TXD_TIME_MAX_BATCH  15000   /* milli second unit */

void init_wait_queue(wait_queue_t *wq){
    wq->condition = 0;
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

    journal_t = (txd_journal_t*)txd_zmalloc_spdk(sizeof(txd_journal_t*));
	if (!journal_t)
		return NULL;

    txd_bh = (struct txd_buffer_head*)txd_zmalloc_spdk(sizeof(struct txd_buffer_head));
    if(!txd_bh)
        goto out1;
    /*
     * TODO 
     *  - set buffer head related structure.
     */
    txd_bh->bh_buf = (char*)j_sb;
    j_sb = (txd_j_superblock_t*)j_sb;

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
		goto out2;

	/* journal descriptor can store up to n blocks -bzzz */
	journal_t->j_blk_offset = j_sb->s_first;
	journal_t->j_maxlen = j_sb->s_maxlen;
	journal_t->j_blocksize = j_sb->s_blocksize;

    journal_t->j_tail_sequence = j_sb->s_sequence; // set last sequence id to first commit sequence id.
    journal_t->j_tail =  j_sb->s_start; // set oldest log blocknr to first log blocknr
	journal_t->j_first = j_sb->s_first; // set first usable  journal blocknr
	journal_t->j_last = j_sb->s_maxlen; // set last usable journal blocknr
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
	journal_t->j_wbuf = (struct txd_buffer_head**)txd_zmalloc_spdk(n * sizeof(struct txd_buffer_head*));
	if (!journal_t->j_wbuf)
		goto out3;
#if 0
	bh = getblk(start, journal_t->j_blocksize);
	if (!bh) {
		fprintf(stdout,"    Cannot get buffer for journal superblock\n");
		goto out;
	}
#endif
	journal_t->j_sb_buffer = txd_bh;
	journal_t->j_superblock = (txd_j_superblock_t *)txd_bh->bh_buf;

	return journal_t;

out3:
	txd_journal_destroy_revoke(journal_t);
out2:
    txd_free_spdk(txd_bh, sizeof(struct txd_buffer_head));
out1:
	txd_free_spdk(journal_t, sizeof(journal_t));
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
     int err = TXD_SUCCESS; 
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

static int jbd2_write_superblock(txd_journal_t *journal, int write_flags)
{
	int ret = TXD_FAIL;
#if 0
	struct buffer_head *bh = journal->j_sb_buffer;
	journal_superblock_t *sb = journal->j_superblock;

	trace_jbd2_write_superblock(journal, write_flags);
	if (!(journal->j_flags & JBD2_BARRIER))
		write_flags &= ~(REQ_FUA | REQ_PREFLUSH);
	lock_buffer(bh);
	if (buffer_write_io_error(bh)) {
		/*
		 * Oh, dear.  A previous attempt to write the journal
		 * superblock failed.  This could happen because the
		 * USB device was yanked out.  Or it could happen to
		 * be a transient write error and maybe the block will
		 * be remapped.  Nothing we can do but to retry the
		 * write and hope for the best.
		 */
		printk(KERN_ERR "JBD2: previous I/O error detected "
		       "for journal superblock update for %s.\n",
		       journal->j_devname);
		clear_buffer_write_io_error(bh);
		set_buffer_uptodate(bh);
	}
	jbd2_superblock_csum_set(journal, sb);
	get_bh(bh);
	bh->b_end_io = end_buffer_write_sync;
	ret = submit_bh(REQ_OP_WRITE, write_flags, bh);
	wait_on_buffer(bh);
	if (buffer_write_io_error(bh)) {
		clear_buffer_write_io_error(bh);
		set_buffer_uptodate(bh);
		ret = -EIO;
	}
	if (ret) {
		printk(KERN_ERR "JBD2: Error %d detected when updating "
		       "journal superblock for %s.\n", ret,
		       journal->j_devname);
		jbd2_journal_abort(journal, ret);
	}

#endif

	return ret;
}

#define REQ_OP_BITS	8
#define REQ_OP_MASK	((1 << REQ_OP_BITS) - 1)
#define REQ_FLAG_BITS	24

enum req_flag_bits {
	__REQ_FAILFAST_DEV =	/* no driver retries of device errors */
		REQ_OP_BITS,
	__REQ_FAILFAST_TRANSPORT, /* no driver retries of transport errors */
	__REQ_FAILFAST_DRIVER,	/* no driver retries of driver errors */
	__REQ_SYNC,		/* request is sync (sync write or read) */
	__REQ_META,		/* metadata io request */
	__REQ_PRIO,		/* boost priority in cfq */
	__REQ_NOMERGE,		/* don't touch this for merging */
	__REQ_IDLE,		/* anticipate more IO after this one */
	__REQ_INTEGRITY,	/* I/O includes block integrity payload */
	__REQ_FUA,		/* forced unit access */
	__REQ_PREFLUSH,		/* request for cache flush */
	__REQ_RAHEAD,		/* read ahead, can fail anytime */
	__REQ_BACKGROUND,	/* background IO */
	__REQ_NR_BITS,		/* stops here */
};

#define REQ_FAILFAST_DEV	(1ULL << __REQ_FAILFAST_DEV)
#define REQ_FAILFAST_TRANSPORT	(1ULL << __REQ_FAILFAST_TRANSPORT)
#define REQ_FAILFAST_DRIVER	(1ULL << __REQ_FAILFAST_DRIVER)
#define REQ_SYNC		(1ULL << __REQ_SYNC)
#define REQ_META		(1ULL << __REQ_META)
#define REQ_PRIO		(1ULL << __REQ_PRIO)
#define REQ_NOMERGE		(1ULL << __REQ_NOMERGE)
#define REQ_IDLE		(1ULL << __REQ_IDLE)
#define REQ_INTEGRITY		(1ULL << __REQ_INTEGRITY)
#define REQ_FUA			(1ULL << __REQ_FUA)
#define REQ_PREFLUSH		(1ULL << __REQ_PREFLUSH)
#define REQ_RAHEAD		(1ULL << __REQ_RAHEAD)
#define REQ_BACKGROUND		(1ULL << __REQ_BACKGROUND)

/**
 * txd_journal_update_sb_log_tail() - Update log tail in journal sb on disk.
 *  @journal: The journal to update.
 *  @tail_tid: TID of the new transaction at the tail of the log
 *  @tail_block: The first block of the transaction at the tail of the log
 *  @write_op: With which operation should we write the journal sb
 *
 * Update a journal's superblock information about log tail and write it to
 * disk, waiting for the IO to complete.
 */
int txd_journal_update_sb_log_tail(txd_journal_t *journal, 
        tid_t tail_tid, unsigned long tail_block,int write_op )
{   
	int ret = TXD_FAIL;

#if 0
   txd_j_superblock_t *sb = journal->j_superblock;
	sb->s_sequence = tail_tid;
	sb->s_start    = tail_block;

	ret = jbd2_write_superblock(journal, write_op);
	if (ret)
		goto out;

	/* Log is no longer empty */
	pthread_rwlock_wrlock(&journal->j_state_lock);
	journal->j_flags &= ~TXD_FLUSHED;
	pthread_rwlock_unlock(&journal->j_state_lock);

out:
#endif
	return ret;
}

static void* txd_journald(void *arg){
    int *err = NULL;
    txd_journal_t *journal = (txd_journal_t*)arg;
    wait_queue_t *wq = &journal->j_wait_done_commit;

    fprintf(stdout,"\n\n    journaling function - txd_journald \n\n");

#if 1
    pthread_mutex_lock(&wq->wq_mtx);
    wq->condition = 0;
    pthread_cond_signal(&wq->wq_cnd);
    pthread_mutex_unlock(&wq->wq_mtx);
    sleep(5);      
#endif
    return (void*)0;
}

static int txd_journal_start_thread(txd_journal_t *journal)
{
    pthread_t pthread = NULL;
    wait_queue_t *wq = NULL;
    int err = TXD_FAIL;

    fprintf(stdout,"    condition variable area\n");
    wq = &journal->j_wait_done_commit;
#if 1
    wq->condition = 1;
#endif

	err = pthread_create(&pthread,NULL,txd_journald, (void*)journal);
    if(err == TXD_FAIL)
        return err;
 
    pthread_mutex_lock(&wq->wq_mtx);
    while(wq->condition)
        pthread_cond_wait(&wq->wq_cnd, &wq->wq_mtx);    
    wq->condition++;    
    pthread_mutex_unlock(&wq->wq_mtx);        

    fprintf(stdout,"   signal received\n");

    err = TXD_SUCCESS;
	return err;
}

static int txd_journal_reset(txd_journal_t *journal){
	txd_j_superblock_t *j_sb = journal->j_superblock;
	unsigned long long first, last;
    int err = TXD_FAIL;

	first = j_sb->s_first;
	last = j_sb->s_maxlen;
	if (first + TXD_MIN_JOURNAL_BLOCKS > last + 1) {
		fprintf(stdout,"    Journal too short (blocks %llu-%llu).\n",first, last);
//		journal_fail_superblock(journal);
		return err;
	}

	journal->j_first = first; // set first usable journal blocknr to start blocknr in journal area.
	journal->j_last = last; // set last usable journal blocknr to total blocks in journal

	journal->j_head = first; // set first unused blocknr to start blocknr in journal area.
	journal->j_tail = first; // set last still used blocknr to start blocknr in journal area.
	journal->j_free = last - first; // set available journal block in journal to [last - first] which is journal size(total blocknr - ).

	journal->j_tail_sequence = journal->j_transaction_sequence;
	journal->j_commit_sequence = journal->j_transaction_sequence - 1;
	journal->j_commit_request = journal->j_commit_sequence;

	journal->j_max_transaction_buffers = journal->j_maxlen / 4;

	/*
	 * As a special case, if the on-disk copy is already marked as needing
	 * no recovery (s_start == 0), then we can safely defer the superblock
	 * update until the next commit by setting JBD2_FLUSHED.  This avoids
	 * attempting a write to a potential-readonly device.
	 */
	if (j_sb->s_start == 0) {
        fprintf(stdout,"    Skipping superblock update on recovered sb (start %ld, seq %d, errno %d)\n",
                journal->j_tail, journal->j_tail_sequence,
                journal->j_errno);
		journal->j_flags |= TXD_FLUSHED;
	} else {
		/* Lock here to make assertions happy... */
		pthread_mutex_lock(&journal->j_checkpoint_mutex);
		/*
		 * Update log tail information. We use REQ_FUA since new
		 * transaction will start reusing journal space and so we
		 * must make sure information about current log tail 
         * in super block is on disk before that.
		 */
		txd_journal_update_sb_log_tail(journal,
						journal->j_tail_sequence,
						journal->j_tail,
						REQ_FUA);
		pthread_mutex_unlock(&journal->j_checkpoint_mutex);
	}
	return txd_journal_start_thread(journal);


    return err;
}

int txd_journal_load(txd_journal_t *journal){

    assert(journal != NULL);

 	/* Let the recovery code check whether it needs to recover any
	 * data from the journal. */
	if (txd_journal_recover(journal))
		goto out_recovery_error;

	if (journal->j_failed_commit) {
		fprintf(stdout,"    journal transaction %u is corrupt.\n", 
                journal->j_failed_commit);
        return TXD_FAIL;
	}

    	/* OK, we've finished with the dynamic journal bits:
	 * reinitialise the dynamic contents of the superblock in memory
	 * and reset them on disk. */
	if (txd_journal_reset(journal))
		goto out_recovery_error;

    /* OK. recovery is completed and journal is loaded.   */ 
	journal->j_flags &= ~TXD_ABORT;
	journal->j_flags |= TXD_LOADED;

    return TXD_SUCCESS;

out_recovery_error:
	fprintf(stdout,"    recovery failed\n");
    return TXD_FAIL;
}


