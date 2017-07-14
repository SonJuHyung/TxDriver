#ifndef __TXD_H__
#define __TXD_H__

#include <stdio.h>
#include <stdint.h>
#include <pthread.h>
#include "list.h"
#include "ondisk.h"
#include "types.h"

 // Macro variable
#define TXD_SUCCESS    0
#define TXD_FAIL       1


//  Macro functions and structures
#define TXD_CHK_FAIL(res) assert(res!=TXD_FAIL)
#define TXD_PARAMS struct txd_spdk_params
#define CHK_PARAMS(params) \
    if(!params || !params->buf)return TXD_FAIL;

#define USE_CLLIST  1
#define USE_RBTREE  0
#define USE_BPTREE  0
#define USE_RXTREE  0

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

#define BUFFER_STATUS_UNUSED	0
#define BUFFER_STATUS_CLEAN		1
#define BUFFER_STATUS_DIRTY		2
#define BUFFER_STATUS_LOAD		3
#define BUFFER_STATUS_META		4
#define BUFFER_STATUS_MAX		5

typedef struct txd_buffer_head_s{
#if USE_CLLIST
	struct list_head bh_dirty_list; /* metadata buffer list for specific inode */	
#elif USE_RBNODE
    struct rb_node bh_dirty_rbnode;
#endif	
	int bh_status; /* status (e.g., clean, dirty, meta) */
	char *bh_buf;

} txd_buffer_head_t;


#define jbd2_unmount	0x001	/* journal thread is being destroyed */
#define jbd2_abort	0x002	/* journaling has been aborted for errors. */
#define jbd2_ack_err	0x004	/* the errno in the sb has been acked */
#define jbd2_flushed	0x008	/* the journal superblock has been flushed */
#define jbd2_loaded	0x010	/* the journal superblock has been loaded */
#define jbd2_barrier	0x020	/* use ide barriers */
#define jbd2_abort_on_syncdata_err	0x040	/* abort the journal on file
						 * data write error in ordered
						 * mode */
#define JBD2_REC_ERR	0x080	/* The errno in the sb has been recorded */

struct timer_list {
	//	struct hlist_node	entry;
	unsigned long		expires;
	void			(*function)(unsigned long);
	unsigned long		data;
//	u32			flags;
};


/* Each revoke record represents one single revoked block.  During
   journal replay, this involves recording the transaction ID of the
   last transaction to revoke this block. */

typedef struct txd_revoke_record_s
{
	struct list_head  hash;
	tid_t		  sequence;	/* Used for recovery only */
	unsigned long long	  blocknr;
}txd_revoke_record_t;


/* The revoke table is just a simple hash table of revoke records. */
struct txd_revoke_table_s
{
	/* It is conceivable that we might want a larger hash table
	 * for recovery.  Must be a power of two. */
	int		  hash_size;
	int		  hash_shift;
	struct list_head *hash_table;
}txd_revoke_table_t;

typedef struct txd_transaction_s{

    int temp;
} txd_transaction_t;


#define JBD2_NR_BATCH	64
#define BDEVNAME_SIZE 32

/**
 * struct txd_journal_info - journal information.
 */
typedef struct txd_journal_s
{
	/* General journaling state flags [j_state_lock] */
	unsigned long		        j_flags;

	/*
	 * Is there an outstanding uncleared error on the journal (from a prior
	 * abort)? [j_state_lock]
	 */
	int			                j_errno;
	
    /* The superblock buffer */
	struct txd_buffer_head_t	*j_sb_buffer;
	txd_j_superblock_t	        *j_superblock;
	
	/*
	 * Protect the various scalars in the journal
	 */
	pthread_rwlock_t		    j_state_lock;

	/*
	 * Number of processes waiting to create a barrier lock [j_state_lock]
	 */
	int			                j_barrier_count;

	/* The barrier lock itself */
    pthread_mutex_t             j_barrier;

	/*
	 * Transactions: The current running transaction...
	 * [j_state_lock] [caller holding open handle]
	 */
	txd_transaction_t		    *j_running_transaction;

	/*
	 * the transaction we are pushing to disk
	 * [j_state_lock] [caller holding open handle]
	 */
	txd_transaction_t		    *j_committing_transaction;

	/*
	 * ... and a linked circular list of all transactions waiting for
	 * checkpointing. [j_list_lock]
	 */
	txd_transaction_t		    *j_checkpoint_transactions;

#if 0
	/*
	 * Wait queue for waiting for a locked transaction to start committing,
	 * or for a barrier lock to be released
	 */
	wait_queue_head_t	        j_wait_transaction_locked;

	/* Wait queue for waiting for commit to complete */
	wait_queue_head_t	        j_wait_done_commit;

	/* Wait queue to trigger commit */
	wait_queue_head_t	        j_wait_commit;

	/* Wait queue to wait for updates to complete */
	wait_queue_head_t	        j_wait_updates;

	/* Wait queue to wait for reserved buffer credits to drop */
	wait_queue_head_t	        j_wait_reserved;
#endif
	/* Semaphore for locking against concurrent checkpoints */
	pthread_mutex_t		        j_checkpoint_mutex;

	/*
	 * List of buffer heads used by the checkpoint routine.  
     * Access to this array is controlled by the
	 * j_checkpoint_mutex.  [j_checkpoint_mutex]
	 */
	struct txdbuffer_head_t     *j_chkpt_bhs[JBD2_NR_BATCH];
	
	/*
	 * Journal head: identifies the first unused block in the journal.
	 * [j_state_lock]
	 */
	unsigned long		        j_head;

	/*
	 * Journal tail: identifies the oldest still-used block in the journal.
	 * [j_state_lock]
	 */
	unsigned long		        j_tail;

	/*
	 * Journal free: how many free blocks are there in the journal?
	 * [j_state_lock]
	 */
	unsigned long		        j_free;

	/*
	 * Journal start and end: the block numbers of the first usable block
	 * and one beyond the last usable block in the journal. [j_state_lock]
     * XXX
     *
	 */
	unsigned long		        j_first;
	unsigned long		        j_last;

	/*
	 * Device, blocksize and starting block offset for the location where we
	 * store the journal.
     * XXX
	 */
	int			                j_blocksize;
	unsigned long long	        j_blk_offset;
	char			            j_devname[BDEVNAME_SIZE];

	/* Total maximum capacity of the journal region on disk. */
	unsigned int		        j_maxlen;

	/* Number of buffers reserved from the running transaction 
     * FIXME
     * should I have to implement simple atomic increment function in inline assembly?
     */
    int		                    j_reserved_credits;
//	atomic_t		j_reserved_credits;

	/*
	 * Protects the buffer lists and internal buffer state.
	 */
	pthread_spinlock_t		    j_list_lock;

	/*
	 * Sequence number of the oldest transaction in the log 
     * [j_state_lock]
	 */
	tid_t			            j_tail_sequence;

	/*
	 * Sequence number of the next transaction to grant 
     * [j_state_lock]
	 */
	tid_t			            j_transaction_sequence;

	/*
	 * Sequence number of the most recently committed transaction
	 * [j_state_lock].
	 */
	tid_t			            j_commit_sequence;

	/*
	 * Sequence number of the most recent transaction wanting commit
	 * [j_state_lock]
	 */
	tid_t			            j_commit_request;

	/*
	 * Maximum number of metadata buffers to allow in a single compound commit transaction
     * can be set in initilization.
     * XXX
     * may be quarter of journal area size.
	 */
	int			                j_max_transaction_buffers;

	/*
	 * What is the maximum transaction lifetime before we begin a commit?
     * XXX
     * may be 5 seconds
	 */
	unsigned long		        j_commit_interval;

	/* The timer used to wakeup the commit thread: */
	struct timer_list	        j_commit_timer;

	/*
	 * The revoke table: maintains the list of revoked blocks in the
	 * current transaction.  [j_revoke_lock]
	 */
	pthread_spinlock_t		    j_revoke_lock;
	struct jbd2_revoke_table_s  *j_revoke;
	struct jbd2_revoke_table_s  *j_revoke_table[2];

	/*
	 * array of bhs for jbd2_journal_commit_transaction
	 */
	struct txd_buffer_head_t    **j_wbuf;
    /*
     *	journal->j_blocksize / sizeof(txd_block_tag_t);
     */
	int			                j_wbufsize;

	/*
	 * this is the pid of hte last person to run a synchronous operation
	 * through the journal
	 */
	pid_t			            j_last_sync_writer;



	/*
	 * the average amount of time in nanoseconds it takes to commit a
	 * transaction to disk. [j_state_lock]
	 */
	u64			                j_average_commit_time;

	/*
	 * minimum and maximum times that we should wait for
	 * additional filesystem operations to get batched into a
	 * synchronous handle in microseconds
     * default : 
     *  min - 0
     *  max - 15000 (15 ms)
	 */
	u32			                j_min_batch_time;
	u32			                j_max_batch_time;
#if 0
	/* This function is called when a transaction is closed */
	void			            (*j_commit_callback)(txd_journal_s *, txd_transaction_t *);
#endif
	/* Failed journal commit ID */
	unsigned int		        j_failed_commit;

} txd_journal_t;


/**
 * struct txd_j_handle - handle structure to be compounded to transaction.
 * @h_transaction: Which compound transaction is this update a part of?
 * @h_buffer_credits: Number of remaining buffers we are allowed to dirty.
 * @h_ref: Reference count on this handle
 * @h_err: Field for caller's use to track errors through large fs operations
 * @h_sync: flag for sync-on-close
 * @h_jdata: flag to force data journaling
 * @h_aborted: flag indicating fatal error on handle
 **/

/* Docbook can't yet cope with the bit fields, but will leave the documentation
 * in so it can be fixed later.
 */

typedef struct txd_handle_s
{
    /* Which compound transaction is this update a part of? */
    txd_transaction_t	*h_transaction;
    /* Which journal handle belongs to - used iff h_reserved set */
    txd_journal_t	*h_journal;

	/* Number of remaining buffers we are allowed to dirty: */
	int			h_buffer_credits;

	/* Reference count on this handle */
	int			h_ref;
#if 0
    /*
     * TODO
     * modify 
     */
	/* Field for caller's use to track errors through large fs */
	/* operations */
	int			h_err;

	/* Flags [no locking] */
	unsigned int	h_sync:		1;	/* sync-on-close */
	unsigned int	h_jdata:	1;	/* force data journaling */
	unsigned int	h_reserved:	1;	/* handle with reserved credits */
	unsigned int	h_aborted:	1;	/* fatal error on handle */
	unsigned int	h_type:		8;	/* for handle statistics */
	unsigned int	h_line_no:	16;	/* for handle statistics */

	unsigned long		h_start_jiffies;
	unsigned int		h_requested_credits;
#endif
} txd_handle_t;


/*
 *  struct txd_meta is the metadata buffer which belongs to transacatino.
 */
struct txd_inode_s {
	/* transaction this txd_inode is related */
	txd_transaction_t *i_transaction;

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
}txd_inode_t;


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
