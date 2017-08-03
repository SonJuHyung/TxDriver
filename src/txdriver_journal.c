#include "txdriver.h"

int do_format(TXD_PARAMS *params){
    
    int ret = TXD_FAIL;
      
    assert(params!=NULL);

    printf("\n ### Formating Info ### \n");
    printf("    lba : %ld \n",params->lba * LBA_UNIT );
    printf("    lba_count : %d \n",params->lba_count * LBA_UNIT);

    ret = spdk_write(params);
    if(ret == TXD_FAIL)
        printf("\n ### Format failed !!! exiting program... ### \n");

    ret = spdk_free();
    assert(ret != TXD_FAIL);
    cleanup();
    return ret;
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

