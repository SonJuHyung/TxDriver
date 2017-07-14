#ifndef __TXD_ONDSK_H__
#define __TXD_ONDSK_H__

#include <stdio.h>
#include <stdint.h>
#include "list.h"

/*
 * Txdriver ondisk structures.
 */

/*
 * Descriptor block types:
 */
#define TXD_DESCRIPTOR_BLOCK	1
#define TXD_COMMIT_BLOCK	    2
#define TXD_SUPERBLOCK_V1	    3
#define TXD_REVOKE_BLOCK	    4
#define TXD_TYPE_BLOCK          5

/*
 * Standard header for all descriptor blocks:
 */
typedef struct txd_journal_header_s
{
	uint32_t		txd_blocktype;
    uint32_t		txd_t_id;
} txd_j_header_t;

/*
 * Commit block header for storing transactional checksums:
 */
struct txd_journal_commit_header_s {
	uint32_t		    txd_magic;
	uint32_t            txd_blocktype;
	uint32_t            txd_sequence;
#if 0
    unsigned char       h_chksum_type;
	unsigned char       h_chksum_size;
	unsigned char 	    h_padding[2];
	uint32_t 		    h_chksum[JBD2_CHECKSUM_BYTES];
#endif
	uint64_t		        h_commit_sec;
	uint32_t		    h_commit_nsec;
} txd_j_commit_header_t;

/*
 * The journal superblock.
 */
typedef struct txd_journal_superblock_s
{
	txd_j_header_t s_header;

	/* Static information describing the journal */
	uint32_t	s_blocksize;		/* journal device blocksize */
	uint32_t	s_maxlen;		/* total blocks in journal file */
	uint32_t	s_first;		/* first block of log information */

	/* Dynamic information describing the current state of the log */
	uint32_t	s_sequence;		/* first commit ID expected in log */
	uint32_t	s_start;		/* blocknr of start of log */

	/* Error value, as set by jbd2_journal_abort(). */
	uint32_t	s_errno;

    /* Transaction information */
	uint32_t	s_max_transaction;	/* Limit of journal blocks per trans.*/
	uint32_t	s_max_trans_data;	/* Limit of data blocks per trans. */

#if 0
	uint8_t	s_checksum_type;	/* checksum type */
	uint8_t	s_padding2[3];
	uint32_t	s_padding[42];
	uint32_t	s_checksum;		/* crc32c(superblock) */
/* 0x0100 */
	uint8_t	s_users[16*48];		/* ids of all fs'es sharing the log */
/* 0x0400 */

#endif
} txd_j_superblock_t;

/* Definitions for the journal tag flags word: */
#define TXD_FLAG_ESCAPE		1	/* on-disk block is escaped */
#define TXD_FLAG_SAME_UUID	2	/* block has same uuid as previous */
#define TXD_FLAG_DELETED	4	/* block deleted by this transaction */
#define TXD_FLAG_LAST_TAG	8	/* last tag in this descriptor block */

/*
 * The block tag: used to describe a single buffer in the journal.
 */
typedef struct txd_journal_block_tag_s
{
	uint32_t		t_blocknr;	/* The on-disk block number */
	uint16_t		t_checksum;	/* truncated crc32c(uuid+seq+block) */
	uint16_t		t_flags;	/* See below */
} txd_journal_block_tag_t;

/* Tail of descriptor or revoke block, for checksumming */
struct txd_journal_block_tail_s {
	uint32_t		t_checksum;	/* crc32c(uuid+descr_block) */
} txd_journal_block_tail_t;

#endif
