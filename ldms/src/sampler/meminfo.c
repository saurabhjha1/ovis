/**

Developer : Saurabh Jha <saurabh.jha.2010@gmail.com>

**/

/**
 * \file meminfo.c
 * \brief It will measure the ping latency between the hosts in a cluster.
 */
#define _GNU_SOURCE
#include <inttypes.h>
#include <unistd.h>
#include <sys/errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <pthread.h>
#include "ldms.h"
#include "ldmsd.h"
#include "sampler_base.h"


static ldms_set_t set = NULL;
static ldmsd_msg_log_f msglog;
#define SAMP "meminfo"
static int metric_offset;
static base_data_t base;

__inline__ static uint64_t get_ping_latency(char* host){

  FILE *fp;
  char path[1035];
  float latency = -2;
  int rc;
  /* Open the command for reading. */
  fp = popen("/bin/ping -qc1 -w 10 -s $((1024*10)) master 2>&1 | awk -F'/' 'END{ print (/^rtt/?  $5:-1) }'", "r");
  if (fp == NULL) {
	return latency;
  }

  /* Read the output a line at a time - output it. */
  while (fgets(path, sizeof(path)-1, fp) != NULL) {
    rc = sscanf(path, "%f", &latency);
    if (rc < 0) 
        return -2;
  }

  /* close */
  pclose(fp);
  return (uint64_t)(latency*1e06);
}
static int create_metric_set(base_data_t base)
{
	ldms_schema_t schema;
	int rc, i;

	schema = base_schema_new(base);
	if (!schema) {
		msglog(LDMSD_LERROR,
		       "%s: The schema '%s' could not be created, errno=%d.\n",
		       __FILE__, base->schema_name, errno);
		rc = errno;
		return rc;
	}

	/* Location of first metric from netping file */
	metric_offset = ldms_schema_metric_count_get(schema);

	/*
	 * Process the file to define all the metrics.
	 */
        rc = ldms_schema_metric_add(schema, "ping-localhost", LDMS_V_U64);
		if (rc < 0) {
			return rc;
		}
		rc = ldms_schema_metric_add(schema, "ping-remote", LDMS_V_U64);
		if (rc < 0) { return rc; }

	set = base_set_new(base);
	if (!set) {
		rc = errno;
		return rc;
	}
	return 0;
}

/**
 * check for invalid flags, with particular emphasis on warning the user about
 */
static int config_check(struct attr_value_list *kwl, struct attr_value_list *avl, void *arg)
{
	char *value;
	int i;

	char* deprecated[]={"set"};

	for (i = 0; i < (sizeof(deprecated)/sizeof(deprecated[0])); i++){
		value = av_value(avl, deprecated[i]);
		if (value){
			msglog(LDMSD_LERROR, SAMP ": config argument %s has been deprecated.\n",
			       deprecated[i]);
			return EINVAL;
		}
	}

	return 0;
}

static const char *usage(struct ldmsd_plugin *self)
{
	return  "config name=" SAMP BASE_CONFIG_USAGE;
}

static int config(struct ldmsd_plugin *self, struct attr_value_list *kwl, struct attr_value_list *avl)
{
	int rc;

	if (set) {
		msglog(LDMSD_LERROR, SAMP ": Set already created.\n");
		return EINVAL;
	}

	rc = config_check(kwl, avl, NULL);
	if (rc != 0){
		return rc;
	}

	base = base_config(avl, SAMP, SAMP, msglog);
	if (!base) {
		rc = errno;
		goto err;
	}

	rc = create_metric_set(base);
	if (rc) {
		msglog(LDMSD_LERROR, SAMP ": failed to create a metric set.\n");
		goto err;
	}
	return 0;
 err:
	base_del(base);
	return rc;
}

static ldms_set_t get_set(struct ldmsd_sampler *self)
{
	return set;
}

static int sample(struct ldmsd_sampler *self)
{
	int metric_no;
	union ldms_value v;

	if (!set) {
		msglog(LDMSD_LDEBUG, SAMP ": plugin not initialized\n");
		return EINVAL;
	}

	metric_no = metric_offset;
	base_sample_begin(base);

	v.v_u64 =  get_ping_latency("localhost");
	ldms_metric_set(set, metric_no, &v);
	metric_no += 1;

    // set remote
    v.v_u64 = 1000;
    ldms_metric_set(set, metric_no, &v);
    metric_no++;
out:
	base_sample_end(base);
	return 0;
}

static void term(struct ldmsd_plugin *self)
{
	if (base)
		base_del(base);
	if (set)
		ldms_set_delete(set);
	set = NULL;
}

static struct ldmsd_sampler meminfo_plugin = {
	.base = {
		.name = SAMP,
		.type = LDMSD_PLUGIN_SAMPLER,
		.term = term,
		.config = config,
		.usage = usage,
	},
	.get_set = get_set,
	.sample = sample,
};

struct ldmsd_plugin *get_plugin(ldmsd_msg_log_f pf)
{
	msglog = pf;
	set = NULL;
	return &meminfo_plugin.base;
}
