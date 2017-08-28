
#include "txdriver.h"

int main(int argc, char **argv)
{
#if 0
    int res = TXD_FAIL;
    char *tmp_str = "Hello Son\n";

    TXD_PARAMS params_w;
    params_w.buf = (char*)calloc(0x1000, 1);
    params_w.buf_size = 0x1000;
    params_w.buf_align = 0x1000;
    params_w.lba = 1;
    params_w.lba_count = 1;

    memcpy(params_w.buf, tmp_str, strlen(tmp_str));

    res = spdk_init(); 
    TXD_CHK_FAIL(res);
   
    res = spdk_alloc_qpair(); 
    TXD_CHK_FAIL(res);

    res = spdk_write(&params_w); 
    TXD_CHK_FAIL(res);

    TXD_PARAMS params_r;
    params_r.buf = (char*)calloc(0x1000, 1);
    params_r.buf_size = 0x1000;
    params_r.buf_align = 0x1000;
    params_r.lba = 1;
    params_r.lba_count = 1;

    res = spdk_read(&params_r);

    TXD_CHK_FAIL(res);
    fprintf(stdout,"%s\n", params_r.buf);

    spdk_free();

    return TXD_SUCCESS;
#endif

    puts("");
    fprintf(stdout,"super block size        : %ld \n", sizeof(txd_j_superblock_t));
    fprintf(stdout,"descriptor block size   : %ld \n", sizeof(txd_journal_block_tag_t));
    fprintf(stdout,"commit block size       : %ld \n", sizeof(txd_j_commit_header_t)); 
    puts("");

    tx_format(0, 1024);
    tx_begin(0, 1024);

    return 0;
} 
