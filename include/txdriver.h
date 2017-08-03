#ifndef __TXD_H__
#define __TXD_H__

#include <stdio.h>
#include <stdint.h>
#include <semaphore.h>
#include <pthread.h>
#include <string.h>
#include "list.h"
#include "ondisk.h"
#include "types.h"

// DEBUGGING Macro
#define NEED_TO_FIND 0
#define DEBUG 1

// Macro variable
#define TXD_SUCCESS    0
#define TXD_FAIL       1


//  Macro functions and structures
#define TXD_CHK_FAIL(res) assert(res!=TXD_FAIL)
#define TXD_PARAMS struct txd_spdk_params
#define CHK_PARAMS(params) \
    if(!params || !params->buf)return TXD_FAIL;

// which data structure?
#define USE_CLLIST  1
#define USE_RBTREE  0
#define USE_BPTREE  0
#define USE_RXTREE  0

// size
#define GB (1024*1024*1024)
#define MB (1024*1024)
#define KB (1024)

// block size 
#define LBA_UNIT    0x0200
#define BLK_SIZE    0x1000

/* cache line size */
#define CLSIZE  64

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
 * txdriver buffer head status 
 */
#define BUFFER_STATUS_UNUSED	0
#define BUFFER_STATUS_CLEAN		1
#define BUFFER_STATUS_DIRTY		2
#define BUFFER_STATUS_LOAD		3
#define BUFFER_STATUS_META		4
#define BUFFER_STATUS_MAX		5

/*
 * txdriver buffer head structure.
 * NOTE
 *  - should I use rbtree or list
 */
struct txd_buffer_head{
#if USE_CLLIST
    struct list_head bh_dirty_list; /* metadata buffer list for specific inode */	
#elif USE_RBNODE
    struct rb_node bh_dirty_rbnode;
#endif	
    int bh_status; /* status (e.g., clean, dirty, meta) */
    char *bh_buf;

};


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

typedef struct txd_transaction_s txd_transaction_t;
typedef struct txd_journal_s txd_journal_t;
typedef struct txd_handle_s txd_handle_t;


/*
 * timer to wake up commit thread.
 */
struct timer_list {
    //	struct hlist_node	entry;
    unsigned long		expires;
    void			(*function)(unsigned long);
    unsigned long		data;
    //	u32			flags;
};


/* 
 * Each revoke record represents one single revoked block.  
 * During
 *  journal replay, this involves recording the transaction ID of the
 *  last transaction to revoke this block. 
 */
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

/*
 * Definitions which augment the buffer_head layer
 */

/* journaling buffer types */
#define BJ_None		0	/* Not journaled */
#define BJ_Metadata	1	/* Normal journaled metadata */
#define BJ_Forget	2	/* Buffer superseded by this transaction */
#define BJ_Shadow	3	/* Buffer contents being shadowed to the log */
#define BJ_Reserved	4	/* Buffer is reserved for access by journal */
#define BJ_Types	5

/*
 * during journaling if transaction is aborred this funtion in this type is called.
 */
struct txd_buffer_trigger_type {
    /*
     * Fired a the moment data to write to the journal are known to be
     * stable - so either at the moment b_frozen_data is created or just
     * before a buffer is written to the journal.  mapped_data is a mapped
     * buffer that is the frozen data for commit.
     */
    void (*t_frozen)(struct txd_buffer_trigger_type *type,
            struct txd_buffer_head *bh, void *mapped_data,
            unsigned int size);

    /*
     * Fired during journal abort for dirty buffers that will not be
     * committed.
     */
    void (*t_abort)(struct txd_buffer_trigger_type *type,
            struct txd_buffer_head *bh);
};


/*
 * manage journal blocks related to transaction.
 */
struct txd_journal_head {
    /*
     * Points back to our buffer_head. [jbd_lock_bh_journal_head()]
     */
    struct txd_buffer_head *b_bh;

    /*
     * Reference count - see description in journal.c
     * [jbd_lock_bh_journal_head()]
     */
    int b_jcount;

    /*
     * Journalling list for this buffer [jbd_lock_bh_state()]
     * NOTE: We *cannot* combine this with b_modified into a bitfield
     * as gcc would then (which the C standard allows but which is
     * very unuseful) make 64-bit accesses to the bitfield and clobber
     * b_jcount if its update races with bitfield modification.
     *
     * BJ_None , BJ_Metadata ..
     */
    unsigned b_jlist;

    /*
     * This flag signals the buffer has been modified by
     * the currently running transaction
     * [jbd_lock_bh_state()]
     */
    unsigned b_modified;
    /*
     * Copy of the buffer data frozen for writing to the log.
     * [jbd_lock_bh_state()]
     *
     * XXX 
     * 필요한가?
     */
    char *b_frozen_data;

    /*
     * Pointer to a saved copy of the buffer containing no uncommitted
     * deallocation references, so that allocations can avoid overwriting
     * uncommitted deletes. [jbd_lock_bh_state()]
     */
    char *b_committed_data;

    /*
     * Pointer to the compound transaction which owns this buffer's
     * metadata: either the running transaction or the committing
     * transaction (if there is one).  Only applies to buffers on a
     * transaction's data or metadata journaling list.
     * [j_list_lock] [jbd_lock_bh_state()]
     * Either of these locks is enough for reading, both are needed for
     * changes.
     */
    txd_transaction_t *b_transaction;

    /*
     * Pointer to the running compound transaction which is currently
     * modifying the buffer's metadata, if there was already a transaction
     * committing it when the new transaction touched it.
     * [t_list_lock] [jbd_lock_bh_state()]
     *
     * 뭐에 필요한 거지?
     */
    txd_transaction_t *b_next_transaction;

    /*
     * Doubly-linked list of buffers on a transaction's data, metadata or
     * forget queue. [t_list_lock] [jbd_lock_bh_state()]
     */
    struct txd_journal_head *b_tnext, *b_tprev;

    /*
     * Pointer to the compound transaction against which this buffer
     * is checkpointed.  Only dirty buffers can be checkpointed.
     * [j_list_lock]
     */
    txd_transaction_t *b_cp_transaction;

    /*
     * Doubly-linked list of buffers still remaining to be flushed
     * before an old transaction can be checkpointed.
     * [j_list_lock]
     */
    struct txd_journal_head *b_cpnext, *b_cpprev;

    /* Trigger type */
    struct txd_buffer_trigger_type *b_triggers;

    /* Trigger type for the committing transaction's frozen data */
    struct txd_buffer_trigger_type *b_frozen_triggers;
};

/*
 * Some stats for checkpoint phase
 */
struct txd_transaction_chp_stats_s {
    unsigned long		cs_chp_time;
    unsigned int		cs_forced_to_close;
    unsigned int		cs_written;
    unsigned int		cs_dropped;
};


/* The transaction_t type is the guts of the journaling mechanism.  It
 * tracks a compound transaction through its various states:
 *
 * RUNNING:	accepting new updates
 * LOCKED:	Updates still running but we don't accept new ones
 * RUNDOWN:	Updates are tidying up but have finished requesting
 *		new buffers to modify (state not used for now)
 * FLUSH:       All updates complete, but we are still writing to disk
 * COMMIT:      All data on disk, writing commit record
 * FINISHED:	We still have to keep the transaction for checkpointing.
 *
 * The transaction keeps track of all of the buffers modified by a
 * running transaction, and all of the buffers committed but not yet
 * flushed to home for finished transactions.
 */

/*
 * Lock ranking:
 *
 *    j_list_lock
 *      ->jbd_lock_bh_journal_head()	(This is "innermost")
 *
 *    j_state_lock
 *    ->jbd_lock_bh_state()
 *
 *    jbd_lock_bh_state()
 *    ->j_list_lock
 *
 *    j_state_lock
 *    ->t_handle_lock
 *
 *    j_state_lock
 *    ->j_list_lock			(journal_unmap_buffer)
 *
 */

struct txd_transaction_s
{
    /* Pointer to the journal for this transaction. 
     * [no locking] 
     */
    txd_journal_t		*t_journal;

    /* Sequence number for this transaction 
     * [no locking]
     */
    tid_t			t_tid;

    /*
     * Transaction's current state
     * [no locking - only kjournald2 alters this]
     * [j_list_lock] guards transition of a transaction into T_FINISHED
     * state and subsequent call of __jbd2_journal_drop_transaction()
     * FIXME: needs barriers
     * KLUDGE: [use j_state_lock]
     */
    enum {
        T_RUNNING,
        T_LOCKED,
        T_FLUSH,
        T_COMMIT,
        T_COMMIT_DFLUSH,
        T_COMMIT_JFLUSH,
        T_COMMIT_CALLBACK,
        T_FINISHED
    }			t_state;

    /*
     * Where in the log does this transaction's commit start? [no locking]
     */
    unsigned long		t_log_start;

    /* Number of buffers on the t_buffers list [j_list_lock] */
    int			t_nr_buffers;

    /*
     * Doubly-linked circular list of all buffers reserved but not yet
     * modified by this transaction [j_list_lock]
     */
    struct txd_journal_head	*t_reserved_list;

    /*
     * Doubly-linked circular list of all metadata buffers owned by this
     * transaction [j_list_lock]
     */
    struct txd_journal_head	*t_buffers;

    /*
     * Doubly-linked circular list of all forget buffers (superseded
     * buffers which we can un-checkpoint once this transaction commits)
     * [j_list_lock]
     */
    struct txd_journal_head	*t_forget;

    /*
     * Doubly-linked circular list of all buffers still to be flushed before
     * this transaction can be checkpointed. [j_list_lock]
     */
    struct txd_journal_head	*t_checkpoint_list;

    /*
     * Doubly-linked circular list of all buffers submitted for IO while
     * checkpointing. [j_list_lock]
     */
    struct txd_journal_head	*t_checkpoint_io_list;

    /*
     * Doubly-linked circular list of metadata buffers being shadowed by log
     * IO.  The IO buffers on the iobuf list and the shadow buffers on this
     * list match each other one for one at all times. [j_list_lock]
     */
    struct txd_journal_head	*t_shadow_list;

    /*
     * List of inodes whose data we've modified in data=ordered mode.
     * [j_list_lock]
     */
    struct list_head	t_inode_list;

    /*
     * Protects info related to handles
     */
    pthread_spinlock_t		t_handle_lock;

    /*
     * Longest time some handle had to wait for running transaction
     */
    unsigned long		t_max_wait;

    /*
     * When transaction started
     */
    unsigned long		t_start;

    /*
     * When commit was requested
     */
    unsigned long		t_requested;

    /*
     * Checkpointing stats [j_checkpoint_sem]
     */
    struct txd_transaction_chp_stats_s t_chp_stats;

    /*
     * Number of outstanding updates running on this transaction
     * [t_handle_lock]
     * 
     * NOTE     
     *  chnaage atomic_t to volatile int.
     */    
    volatile int 		t_updates;

    /*
     * Number of buffers reserved for use by all handles in this transaction
     * handle but not yet modified. [t_handle_lock]
     *
     * NOTE
     *  change atomic_t to volatile int
     */
    volatile int		t_outstanding_credits;

    /*
     * Forward and backward links for the circular list of all transactions
     * awaiting checkpoint. [j_list_lock]
     */
    txd_transaction_t		*t_cpnext, *t_cpprev;

    /*
     * When will the transaction expire (become due for commit), in jiffies?
     * [no locking]
     */
    unsigned long		t_expires;

    /*
     * When this transaction started, in nanoseconds [no locking]
     *
     * NOTE
     *  ktime_t to unsigned long
     */
    unsigned long			t_start_time;

    /*
     * How many handles used this transaction? [t_handle_lock]
     *
     * NOTE
     *  atomic_t to volatime int
     */
    volatile int		t_handle_count;

    /*
     * This transaction is being forced and some process is
     * waiting for it to finish.
     */
    unsigned int t_synchronous_commit;

    /* Disk flush needs to be sent to fs partition [no locking] */
    int			t_need_data_flush;

    /*
     * For use by the filesystem to store fs-specific data
     * structures associated with the transaction
     */
    struct list_head	t_private_list;
};


#define TXD_NR_BATCH	64
#define BDEVNAME_SIZE 32

/**
 * struct txd_journal_info - journal information.
 */
struct txd_journal_s
{
    /* General journaling state flags [j_state_lock] */
    unsigned long		        j_flags;

    /*
     * Is there an outstanding uncleared error on the journal (from a prior
     * abort)? [j_state_lock]
     */
    int			                j_errno;

    /* The superblock buffer */
    struct txd_buffer_head	*j_sb_buffer;
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

    /*
     * TODO
     *  need to find data structure to replace wait_queue_head_t
     */
#if NEED_TO_FIND
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
    struct txd_buffer_head     *j_chkpt_bhs[TXD_NR_BATCH];

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
     * NOTE
     * change atomic_t to volatile int
     */
    volatile int		                    j_reserved_credits;

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
     * NOTE
     * may be quarter of journal area size.
     */
    int			                j_max_transaction_buffers;

    /*
     * What is the maximum transaction lifetime before we begin a commit?
     * NOTE
     * may be 5 seconds
     */
    unsigned long		        j_commit_interval;

    /* The timer used to wakeup the commit thread: 
     *
     * need to implement or find timer replace.
     */
#if NEED_TO_FIND
    struct timer_list	        j_commit_timer;
#endif
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
    struct txd_buffer_head    **j_wbuf;
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

    /* This function is called when a transaction is closed 
     * NOTE
     *  In kernel.... defined in ext4 srouce code - ext4_journal_commit_callback
     */
    void			            (*j_commit_callback)(txd_journal_t *, txd_transaction_t *);

    /* Failed journal commit ID */
    unsigned int		        j_failed_commit;

};


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

struct txd_handle_s{
    /* Which compound transaction is this update a part of? */
    txd_transaction_t	*h_transaction;
    /* Which journal handle belongs to - used iff h_reserved set */
    txd_journal_t	*h_journal;

    /* Number of remaining buffers we are allowed to dirty: */
    int			h_buffer_credits;

    /* Reference count on this handle */
    int			h_ref;

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
};


/*
 *  struct txd_meta is the metadata buffer which belongs to transacatino.
 */
typedef struct txd_inode_s {
    /* transaction this txd_inode is related */
    txd_transaction_t *i_transaction;

    /* meta deta buffer in user space FS to be commited */
    char *meta_buffer;
    /* data buffer in user space related to meta_data buffer.*/
    char *data_buffer; 

    /* List of inodes in the i_transaction [j_list_lock] */
    struct list_head i_list;

    /*
     * TODO
     * need to reconsider structure.
     */
}txd_inode_t;

/* functions in "txdriver_api.c" which are txdriver core functions. */
int tx_format(uint64_t txd_addr, uint32_t txd_size);    
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
void cleanup(void);

/* functinos in txdriver_journal.c  */
void print_sb_info(txd_j_superblock_t *sb);
int chk_txd_sb(txd_j_superblock_t *sb, uint32_t txd_size);
void init_journal_header(txd_j_header_t *txd_j_header,txd_type_t blk_type,int tid );
int do_format(TXD_PARAMS *params);


#endif
