/*-
 * SJH modifying
 */

#include "spdk/stdinc.h"

#include "spdk/nvme.h"
#include "spdk/env.h"

#include "txdriver.h"

/*
 *
 */
struct ctrlr_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct ctrlr_entry	*next;
    char			name[1024];
};

/*
 *
 */
struct ns_entry {
	struct spdk_nvme_ctrlr	*ctrlr;
	struct spdk_nvme_ns	*ns;
	struct ns_entry		*next;
	struct spdk_nvme_qpair	*qpair;
};

/*
 * function parameters in spdk interface functions.
 */
struct spdk_sequence {
	struct ns_entry	*ns_entry;
    char            *buf;    
	char		    *buf_user;
    int             buf_size;
	int		        is_completed;
};


static struct ctrlr_entry *g_controllers = NULL;
static struct ns_entry *g_namespaces = NULL;

static void
register_ns(struct spdk_nvme_ctrlr *ctrlr, struct spdk_nvme_ns *ns)
{
	struct ns_entry *entry;
	const struct spdk_nvme_ctrlr_data *cdata;

	/*
	 * spdk_nvme_ctrlr is the logical abstraction in SPDK for an NVMe
	 *  controller.  During initialization, the IDENTIFY data for the
	 *  controller is read using an NVMe admin command, and that data
	 *  can be retrieved using spdk_nvme_ctrlr_get_data() to get
	 *  detailed information on the controller.  Refer to the NVMe
	 *  specification for more details on IDENTIFY for NVMe controllers.
	 */
	cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	if (!spdk_nvme_ns_is_active(ns)) {
		printf("Controller %-20.20s (%-20.20s): Skipping inactive NS %u\n",
		       cdata->mn, cdata->sn,
		       spdk_nvme_ns_get_id(ns));
		return;
	}

	entry = malloc(sizeof(struct ns_entry));
	if (entry == NULL) {
		perror("ns_entry malloc");
		exit(1);
	}

	entry->ctrlr = ctrlr;
	entry->ns = ns;
	entry->next = g_namespaces;
	g_namespaces = entry;

	printf("  Namespace ID: %d size: %juGB\n", spdk_nvme_ns_get_id(ns),
	       spdk_nvme_ns_get_size(ns) / 1000000000);
}

static void
read_complete(void *arg, const struct spdk_nvme_cpl *completion)
{ 
	struct spdk_sequence *sequence = arg;

	printf("%s", sequence->buf);
    memcpy(sequence->buf_user, sequence->buf, sequence->buf_size);
	spdk_dma_free(sequence->buf);
	sequence->is_completed = 1;
}

static void
write_complete(void *arg, const struct spdk_nvme_cpl *completion)
{
	struct spdk_sequence *sequence = arg;

	spdk_dma_free(sequence->buf);
	sequence->is_completed = 1;
}

static bool
probe_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	 struct spdk_nvme_ctrlr_opts *opts)
{
	printf("Attaching to %s\n", trid->traddr);

	return true;
}

static void
attach_cb(void *cb_ctx, const struct spdk_nvme_transport_id *trid,
	  struct spdk_nvme_ctrlr *ctrlr, const struct spdk_nvme_ctrlr_opts *opts)
{
	int nsid, num_ns;
	struct ctrlr_entry *entry;
	struct spdk_nvme_ns *ns;
	const struct spdk_nvme_ctrlr_data *cdata = spdk_nvme_ctrlr_get_data(ctrlr);

	entry = malloc(sizeof(struct ctrlr_entry));
	if (entry == NULL) {
		perror("ctrlr_entry malloc");
		exit(1);
	}

	printf("Attached to %s\n", trid->traddr);

	snprintf(entry->name, sizeof(entry->name), "%-20.20s (%-20.20s)", cdata->mn, cdata->sn);

	entry->ctrlr = ctrlr;
	entry->next = g_controllers;
	g_controllers = entry;

	/*
	 * Each controller has one or more namespaces.  An NVMe namespace is basically
	 *  equivalent to a SCSI LUN.  The controller's IDENTIFY data tells us how
	 *  many namespaces exist on the controller.  For Intel(R) P3X00 controllers,
	 *  it will just be one namespace.
	 *
	 * Note that in NVMe, namespace IDs start at 1, not 0.
	 */
	num_ns = spdk_nvme_ctrlr_get_num_ns(ctrlr);
	printf("Using controller %s with %d namespaces.\n", entry->name, num_ns);
	for (nsid = 1; nsid <= num_ns; nsid++) {
		ns = spdk_nvme_ctrlr_get_ns(ctrlr, nsid);
		if (ns == NULL) {
			continue;
		}
		register_ns(ctrlr, ns);
	}
}

static void
cleanup(void)
{
	struct ns_entry *ns_entry = g_namespaces;
	struct ctrlr_entry *ctrlr_entry = g_controllers;

	while (ns_entry) {
		struct ns_entry *next = ns_entry->next;
		free(ns_entry);
		ns_entry = next;
	}

	while (ctrlr_entry) {
		struct ctrlr_entry *next = ctrlr_entry->next;

		spdk_nvme_detach(ctrlr_entry->ctrlr);
		free(ctrlr_entry);
		ctrlr_entry = next;
	}
}

int spdk_init(void){ 
	int rc;
	struct spdk_env_opts opts;

	/*
	 * SPDK relies on an abstraction around the local environment
	 * named env that handles memory allocation and PCI device operations.
	 * This library must be initialized first.
	 *
	 */ 
	spdk_env_opts_init(&opts);
	opts.name = "txdriver";
	opts.shm_id = 0;
	spdk_env_init(&opts);

	printf("Initializing NVMe Controllers\n");

	/*
	 * Start the SPDK NVMe enumeration process.  probe_cb will be called
	 *  for each NVMe controller found, giving our application a choice on
	 *  whether to attach to each controller.  attach_cb will then be
	 *  called for each controller after the SPDK NVMe driver has completed
	 *  initializing the controller we chose to attach.
	 */
	rc = spdk_nvme_probe(NULL, NULL, probe_cb, attach_cb, NULL);
	if (rc != 0) {
		fprintf(stderr, "spdk_nvme_probe() failed\n");
		cleanup();
		return TXD_FAIL;
	}

	if (g_controllers == NULL) {
		fprintf(stderr, "no NVMe controllers found\n");
		cleanup();
		return TXD_FAIL;
	}

    return TXD_SUCCESS;
}

int spdk_alloc_qpair(void){
	struct ns_entry *ns_entry;
	ns_entry = g_namespaces;

    //allocate io qpair
    ns_entry->qpair = spdk_nvme_ctrlr_alloc_io_qpair(ns_entry->ctrlr, 0);
    if(ns_entry->qpair == NULL){
        printf("ERROR: apdk_nvme_ctrlr_alloc_io_qpair failed\n");
        return TXD_FAIL;
    }
    return TXD_SUCCESS;
}

int spdk_write(TXD_PARAMS *params){ 
	struct ns_entry *ns_entry;
	struct spdk_sequence sequence;
	int rc;

    CHK_PARAMS(params);
	ns_entry = g_namespaces;

    //allocate memory and pin it
    sequence.buf = spdk_dma_zmalloc(params->buf_size, params->buf_align, NULL);   
    sequence.is_completed = 0;        
    sequence.ns_entry = ns_entry;
    sequence.buf_user = params->buf;
    sequence.buf_size = params->buf_size;

    memcpy(sequence.buf, params->buf, params->buf_size);

    /* write to lba 0, "write_complete" and "&sequence" are the completion
     * callback and argument.
     */
    rc = spdk_nvme_ns_cmd_write(ns_entry->ns, ns_entry->qpair, sequence.buf,
            params->lba /* LBA */, 
            params->lba_count /* LBA Count */,
            write_complete, &sequence, 0);

    if(rc != TXD_SUCCESS){
        fprintf(stderr, "starting write io failed\n");
        exit(1);
    }

    //poll for completion
    while(!sequence.is_completed){
        spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
    }  
    return TXD_SUCCESS;
} 

int spdk_read(TXD_PARAMS *params){
	struct ns_entry *ns_entry;
	struct spdk_sequence sequence;
	int rc;

    CHK_PARAMS(params);
	ns_entry = g_namespaces;

    //allocate memory and pin it
    sequence.buf = spdk_dma_zmalloc(params->buf_size, params->buf_align, NULL);
    sequence.is_completed = 0;
    sequence.ns_entry = ns_entry;
    sequence.buf_user = params->buf;
    sequence.buf_size = params->buf_size;

    rc = spdk_nvme_ns_cmd_read(ns_entry->ns, ns_entry->qpair, sequence.buf,
            params->lba /* LBA */, 
            params->lba_count /* LBA Count */,
            read_complete, &sequence, 0);

    if(rc != TXD_SUCCESS){
        fprintf(stderr, "starting write io failed\n");
        exit(1);
    }

    //poll for completion
    while(!sequence.is_completed){
        spdk_nvme_qpair_process_completions(ns_entry->qpair, 0);
    } 
    return TXD_SUCCESS;
}

int spdk_free(void){
    struct ns_entry *ns_entry = g_namespaces;

    //free qpair
    return spdk_nvme_ctrlr_free_io_qpair(ns_entry->qpair);
} 


int main(int argc, char **argv)
{
    int res = TXD_FAIL;
    char *tmp_str = "Son Ju Hyung\n";

    TXD_PARAMS params_w;
    params_w.buf = (char*)calloc(0x1000, 1);
    params_w.buf_size = 0x1000;
    params_w.buf_align = 0x1000;
    params_w.lba = 0;
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
    params_r.lba = 0;
    params_r.lba_count = 1;

    res = spdk_read(&params_r);
    TXD_CHK_FAIL(res);
    printf("%s\n", params_r.buf);

    spdk_free();

    return TXD_SUCCESS;
} 

