
#include <unistd.h>
#include <stddef.h>

#include <rte_memory.h>
#include <rte_config.h>
#include <rte_mempool.h>
#include <rte_malloc.h>
#include <spdk/stdinc.h>

#include <spdk/nvme.h>
#include <spdk/env.h>

#include "txdriver.h"

static unsigned long long allocated_mem_size = 0;


/* allocate a alignment-bytes aligned buffer */
void *txd_malloc_spdk(size_t size)
{
	void *p;

	p = spdk_dma_malloc(size, 0x200, NULL);
	if (p == NULL) {
		fprintf(stderr, "   rte_malloc failed\n");
		rte_malloc_dump_stats(stdout, NULL);
		return NULL;
	}
	allocated_mem_size += size;
	return p;
}

/* allocate a alignment-bytes aligned buffer */
void *txd_zmalloc_spdk(size_t size)
{
	void *p;

	p = spdk_dma_zmalloc(size, 0x200, NULL);
	if (p == NULL) {
		fprintf(stderr, "   rte_malloc failed\n");
		rte_malloc_dump_stats(stdout, NULL);
		return NULL;
	}
	allocated_mem_size += size;
	return p;
}

void txd_free_spdk(void* buf, size_t size)
{
	allocated_mem_size -= size;
	spdk_dma_free(buf);
}

/* allocate a alignment-bytes aligned buffer */
void *txd_malloc_dpdk(size_t size)
{
	void *p;

	p = rte_malloc(NULL, size, 0x200);
	if (p == NULL) {
		fprintf(stderr, "   rte_malloc failed\n");
		rte_malloc_dump_stats(stdout, NULL);
		return NULL;
	}
	allocated_mem_size += size;
	return p;
}

/* allocate a alignment-bytes aligned buffer */
void *txd_zmalloc_dpdk(size_t size)
{
	void *p;

	p = rte_zmalloc(NULL, size, 0x200);
	if (p == NULL) {
		fprintf(stderr, "   rte_malloc failed\n");
		rte_malloc_dump_stats(stdout, NULL);
		return NULL;
	}
	allocated_mem_size += size;
	return p;
}

/* allocate a alignment-bytes aligned buffer */
void *txd_calloc_dpdk(size_t num, size_t size)
{
	void *p;

	p = rte_calloc(NULL, num, size, 0x200);
	if (p == NULL) {
		fprintf(stderr, "   rte_malloc failed\n");
		rte_malloc_dump_stats(stdout, NULL);
		return NULL;
	}
	allocated_mem_size += (size*num);
	return p;
}

void txd_free_dpdk(void *ptr, size_t size)
{
	allocated_mem_size -= size;
	rte_free(ptr);
}

void *txd_malloc(size_t size)
{
    allocated_mem_size += size;
	return malloc((size_t)size);
}

void *txd_calloc(size_t num, size_t size)
{
    allocated_mem_size += size;
	return calloc(num,(size_t)size);
}

void txd_free(void *ptr, size_t size)
{
	allocated_mem_size -= size;
	free(ptr);
}

void txd_allocated_mem_info(void){
    fprintf(stdout, "   allocated mem size : %llu", allocated_mem_size);
}

