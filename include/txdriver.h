#ifndef __TXD_H__
#define __TXD_H__

#include <stdio.h>
#include <stdint.h>
#include <semaphore.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
//#include <sys/type.h>

#include "list.h"
#include "ondisk.h"
#include "types.h"

// DEBUGGING Macro
#define NEED_TO_FIND 0
#define DEBUG 1

// Macro variable
#define TXD_SUCCESS     0
#define TXD_FAIL        1
#define TXD_TRUE        1
#define TXD_FALSE       0


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
    int bh_status; /* status (e.g., clean, dirty, meta) */
    char *bh_buf;
    unsigned int bh_pno;
#if USE_CLLIST
    struct list_head bh_dirty_list; /* metadata buffer list for specific inode */	
#elif USE_RBNODE
    struct rb_node bh_dirty_rbnode;
#endif	
};


#define TXD_UNMOUNT	0x001	/* journal thread is being destroyed */
#define TXD_ABORT	0x002	/* journaling has been aborted for errors. */
#define TXD_ACK_ERR	0x004	/* the errno in the sb has been acked */
#define TXD_FLUSHED	0x008	/* the journal superblock has been flushed */
#define TXD_LOADED	0x010	/* the journal superblock has been loaded */
#define TXD_BARRIER	0x020	/* use ide barriers */
#define TXD_ABORT_ON_SYNCDATA_ERR	0x040	/* abort the journal on file
                                             * data write error in ordered
                                             * mode */
#define TXD_REC_ERR	0x080	/* The errno in the sb has been recorded */

typedef struct txd_transaction_s txd_transaction_t;
typedef struct txd_journal_s txd_journal_t;
typedef struct txd_handle_s txd_handle_t;
typedef struct txd_revoke_table_s txd_revoke_table_t;
typedef struct txd_revoke_record_s txd_revoke_record_t;

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
struct txd_revoke_record_s
{
    struct list_head  hash;
    tid_t		  sequence;	/* Used for recovery only */
    unsigned long long	  blocknr;
}t;


/* The revoke table is just a simple hash table of revoke records. */
struct txd_revoke_table_s
{
    /* It is conceivable that we might want a larger hash table
     * for recovery.  Must be a power of two. */
    int		  hash_size;
    int		  hash_shift;
    struct list_head *hash_table;
};

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
     *  chnaage atomic_t to int.
     */    
    int 		t_updates;

    /*
     * Number of buffers reserved for use by all handles in this transaction
     * handle but not yet modified. [t_handle_lock]
     *
     * NOTE
     *  change atomic_t to int
     */
    int		t_outstanding_credits;

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
    int		t_handle_count;

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


typedef struct wait_queue_s{
    int condition;
    pthread_mutex_t     wq_mtx;
    pthread_cond_t      wq_cnd;
} wait_queue_t;

/**
 * struct txd_journal_info - journal information.
 */
struct txd_journal_s
{
    /* General journaling state flags [j_state_lock] */
    unsigned long		        j_flags;                                    // x...o

    /*
     * Is there an outstanding uncleared error on the journal (from a prior
     * abort)? [j_state_lock]
     */
    int			                j_errno;

    /* The superblock buffer */
    struct txd_buffer_head	*j_sb_buffer;                                   // o
    txd_j_superblock_t	        *j_superblock;                              // o

    /*
     * Protect the various scalars in the journal
     */
    pthread_rwlock_t		    j_state_lock;                               // o

    /*
     * Number of processes waiting to create a barrier lock [j_state_lock]
     */
    int			                j_barrier_count;

    /* The barrier lock itself */
     pthread_mutex_t             j_barrier;                                 // o

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
     * Wait queue for waiting for a locked transaction to start committing,
     * or for a barrier lock to be released 
     *
     * wait queue in kernel space is similar to condition variable in user space.
     */
    wait_queue_t	        j_wait_transaction_locked;                      // o

    /* Wait queue for waiting for commit to complete */
    wait_queue_t	        j_wait_done_commit;                             // o

    /* Wait queue to trigger commit */
    wait_queue_t	        j_wait_commit;                                  // o

    /* Wait queue to wait for updates to complete */
    wait_queue_t	        j_wait_updates;                                 // o

    /* Wait queue to wait for reserved buffer credits to drop */
    wait_queue_t	        j_wait_reserved;                                // o

    /* Semaphore for locking against concurrent checkpoints */
    pthread_mutex_t		        j_checkpoint_mutex;                         // o

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
    int			                j_blocksize;                                // o
    unsigned long long	        j_blk_offset;                               // o

    /* Total maximum capacity of the journal region on disk. */
    unsigned int		        j_maxlen;                                   // o

    /* Number of buffers reserved from the running transaction 
     */
    int		                    j_reserved_credits;                        // o

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
    unsigned long		        j_commit_interval;                          // o

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
    pthread_spinlock_t		    j_revoke_lock;                              // o
    struct txd_revoke_table_s  *j_revoke;                                   // o
    struct txd_revoke_table_s  *j_revoke_table[2];                          // o

    /*
     * array of bhs for jbd2_journal_commit_transaction
     */
    struct txd_buffer_head    **j_wbuf;                                     // o
    /*
     *	journal->j_blocksize / sizeof(txd_block_tag_t);
     */
    int			                j_wbufsize;                                 // o

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
    u32			                j_min_batch_time;                           // o
    u32			                j_max_batch_time;                           // o

    /* This function is called when a transaction is closed 
     * NOTE
     *  In kernel.... defined in ext4 srouce code - ext4_journal_commit_callback
     */
    void			            (*j_commit_callback)(txd_journal_t *, txd_transaction_t *);

    /* Failed journal commit ID */
    unsigned int		        j_failed_commit;
    void*                       j_private;
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
txd_journal_t* tx_begin(uint64_t txd_addr, uint32_t txd_size); 
int tx_write(uint8_t meta_buffer, uint8_t data_buffer);

/*
 * TODO
 * need to consider params
 */
int tx_commit(void);
int tx_abort(void);

/* txdriver_spdk.c  */
int spdk_init(void);
int spdk_alloc_qpair(void);
int spdk_write(TXD_PARAMS *params);
int spdk_read(TXD_PARAMS *params);
int spdk_free(void);
void cleanup(void);

/* txdriver_journal.c  */

#define TXD_MIN_JOURNAL_BLOCKS 1024

void print_sb_info(txd_j_superblock_t *sb);
int chk_txd_sb(txd_j_superblock_t *sb, uint32_t txd_size);
void init_journal_header(txd_j_header_t *txd_j_header,txd_type_t blk_type,int tid );
int do_format(TXD_PARAMS *params);
txd_journal_t* txd_get_journal(txd_j_superblock_t *j_sb);
int txd_journal_need_recovery(txd_journal_t *journal);
int txd_wipe_journal_log(txd_journal_t *journal);
int txd_journal_load(txd_journal_t *journal);
int txd_journal_destroy(txd_journal_t *journal);
txd_journal_t *txd_journal_init_common(txd_j_superblock_t *j_sb);


/* atomic.c */ 

#define atomic_set(addr, newval)   atomic_xchg(addr, newval)

void atomic_add( int * value, int inc_val);
void atomic_sub( int * value, int dec_val);
void atomic_inc( int * value);
void atomic_dec( int * value);
int atomic_dec_and_test( int * value);
int atomic_inc_and_test( int * value);
int test_and_set(int *lock);
int atomic_xchg(int *addr, int newval);

/* revoke.c */
#define JOURNAL_REVOKE_DEFAULT_HASH 256
int txd_journal_init_revoke(txd_journal_t *journal, int hash_size);
void txd_journal_destroy_revoke(txd_journal_t *journal_t);

/* malloc.c  */
/* allocate a alignment-bytes aligned buffer */
void *txd_malloc_spdk(size_t size);
void *txd_zmalloc_spdk(size_t size);
void txd_free_spdk(void* buf, size_t size);
void *txd_malloc_dpdk(size_t size);
void *txd_zmalloc_dpdk(size_t size);
void *txd_calloc_dpdk(size_t num, size_t size);
void txd_free_dpdk(void *ptr, size_t size);
void *txd_malloc(size_t size);
void *txd_calloc(size_t num,size_t size);
void txd_free(void *ptr, size_t size);
void txd_allocated_mem_info(void);

#define unlikely(expr)          __builtin_expect(!!(expr), 0)
#define likely(expr)            __builtin_expect(!!(expr), 1)
#define likely_success(expr)    unlikely(expr)
#define likely_true(expr)       likely(expr)

#endif
