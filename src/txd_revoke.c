
#include "txdriver.h"

txd_revoke_table_t *txd_journal_init_revoke_table(int hash_size)
{
	int shift = 0;
	int tmp = hash_size;
	txd_revoke_table_t *table;

	table = (txd_revoke_table_t*)txd_zmalloc_spdk(sizeof(txd_revoke_table_t));
	if (!table)
		goto out;

	while((tmp >>= 1UL) != 0UL)
		shift++;

	table->hash_size = hash_size;
	table->hash_shift = shift;
	table->hash_table = (struct list_head*)txd_zmalloc_spdk(hash_size * sizeof(struct list_head));
	if (!table->hash_table) {
        txd_free_spdk(table, sizeof(txd_revoke_table_t));      
		table = NULL;
		goto out;
	}

	for (tmp = 0; tmp < hash_size; tmp++)
		INIT_LIST_HEAD(&table->hash_table[tmp]);

out:
	return table;
}

static void txd_journal_destroy_revoke_table(txd_revoke_table_t *table)
{
	int i;
	struct list_head *hash_list;

	for (i = 0; i < table->hash_size; i++) {
		hash_list = &table->hash_table[i];
		assert(list_empty(hash_list));
	}

	txd_free_spdk(table->hash_table, sizeof(struct list_head) * table->hash_size);
	txd_free_spdk(table, sizeof(txd_revoke_table_t));      
}

/* Destroy a journal's revoke table.  The table must already be empty! */
void txd_journal_destroy_revoke(txd_journal_t *journal_t)
{
	journal_t->j_revoke = NULL;
	if (journal_t->j_revoke_table[0])
		txd_journal_destroy_revoke_table(journal_t->j_revoke_table[0]);
	if (journal_t->j_revoke_table[1])
		txd_journal_destroy_revoke_table(journal_t->j_revoke_table[1]);
}


/* Initialise the revoke table for a given journal to a given size. */
int txd_journal_init_revoke(txd_journal_t *journal_t, int hash_size)
{
	journal_t->j_revoke_table[0] = txd_journal_init_revoke_table(hash_size);
	if (!journal_t->j_revoke_table[0])
		goto fail0;

	journal_t->j_revoke_table[1] = txd_journal_init_revoke_table(hash_size);
	if (!journal_t->j_revoke_table[1])
		goto fail1;

	journal_t->j_revoke = journal_t->j_revoke_table[1];

//	pthread_spin_init(&journal_t->j_revoke_lock, PTHREAD_PROCESS_PRIVATE);

	return 0;

fail1:
	txd_journal_destroy_revoke_table(journal_t->j_revoke_table[0]);
	journal_t->j_revoke_table[0] = NULL;
fail0:
	return -ENOMEM;
}

