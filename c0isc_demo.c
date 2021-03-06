/* -*- C -*- */
/*
 * Copyright (c) 2018-2020 Seagate Technology LLC and/or its Affiliates
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * For any questions about this software or licensing,
 * please email opensource@seagate.com or cortx-questions@seagate.com.
 *
 * Original author:  Nachiket Sahasrabuddhe <nachiket.sahasrabuddhe@seagate.com>
 * Original creation date: 06-Sep-2018
 */

#include <libgen.h>      /* dirname */

#include "xcode/xcode.h"
#include "conf/schema.h" /* M0_CST_ISCS */
#include "lib/memory.h"  /* m0_alloc m0_free */

#include "c0appz.h"
#include "c0appz_isc.h"  /* c0appz_isc_req */
#include "isc/libdemo.h"     /* mm_result */
#include "isc/libdemo_xc.h"     /* isc_args_xc */


enum isc_comp_type {
	ICT_PING,
	ICT_MIN,
	ICT_MAX,
};

/** Arguments for min/max operations */
struct mm_args {
	/** Length of an array. */
	uint32_t  ma_len;
	/** Array of doubles. */
	double   *ma_arr;
	/** Number of isc services. */
	uint32_t  ma_svc_nr;
	/** Length of a chunk per service. */
	uint32_t  ma_chunk_len;
	/** A service currently fed with input. */
	uint32_t  ma_curr_svc_id;
};

static void fid_get(const char *f_name, struct m0_fid *fid)
{
	uint32_t f_key = m0_full_name_hash((const unsigned char*)f_name,
					    strlen(f_name));
	uint32_t f_cont = m0_full_name_hash((const unsigned char *)"libdemo",
					    strlen("libdemo"));

	m0_fid_set(fid, f_cont, f_key);
}

static int op_type_parse(const char *op_name)
{
	if (op_name == NULL)
		return -EINVAL;
	else if (!strcmp(op_name, "ping"))
		return ICT_PING;
	else if (!strcmp(op_name, "min"))
		return ICT_MIN;
	else if (!strcmp(op_name, "max"))
		return ICT_MAX;
	else
		return -EINVAL;

}

static int file_to_array(const char *file_name, void **arr, uint32_t *arr_len)
{
	FILE     *fd;
	uint32_t  i;
	int       rc;
	double   *val_arr;

	fd = fopen(file_name, "r");
	if (fd == NULL) {
		fprintf(stderr, "error! Could not open file c0isc_data\n");
		return -EINVAL;
	}
	fscanf(fd, "%d", arr_len);
	/* XXX: Fix sizeof (double) with appropriate macro. */
	M0_ALLOC_ARR(val_arr, *arr_len);
	for (i = 0; i < *arr_len; ++i) {
		rc = fscanf(fd, "%lf", &val_arr[i]);
		if (rc == EOF) {
			fprintf(stderr, "data file (%s) does not contain the "
				"specified number of elements: %d\n",
				file_name, *arr_len);
			m0_free(val_arr);
			fclose(fd);
			return -EINVAL;
		}
	}
	*arr = val_arr;
	fclose(fd);
	return 0;
}

static uint32_t isc_services_count(void)
{
	struct m0_fid start_fid = M0_FID0;
	struct m0_fid proc_fid;
	uint32_t      svc_nr = 0;
	int           rc = 0;

	while (rc == 0) {
		rc = c0appz_isc_nxt_svc_get(&start_fid, &proc_fid,
					    M0_CST_ISCS);
		if (rc == 0)
			++svc_nr;
		start_fid = proc_fid;
	}
	return svc_nr;
}

static int op_init(enum isc_comp_type type, void **ip_args)
{
	struct mm_args *in_info;
	double         *arr;
	uint32_t        arr_len;
	int             rc;

	switch (type) {
	case ICT_PING:
		return 0;
	case ICT_MIN:
	case ICT_MAX:
		M0_ALLOC_PTR(in_info);
		if (in_info == NULL)
			return -ENOMEM;
		rc = file_to_array("c0isc_data", (void **)&arr, &arr_len);
		if (rc != 0)
			return rc;
		in_info->ma_arr = arr;
		in_info->ma_len = arr_len;
		in_info->ma_curr_svc_id = 0;
		in_info->ma_svc_nr = isc_services_count();
		in_info->ma_chunk_len = arr_len / in_info->ma_svc_nr;
		*ip_args = in_info;

		return 0;
	default:
		fprintf(stderr, "Invalid operation %d\n", type);
		return EINVAL;
	}
}

static void op_fini(enum isc_comp_type op_type, struct mm_args *in_info,
		    void *op_args)
{
	switch (op_type) {
	case ICT_PING:
		break;
	case ICT_MIN:
	case ICT_MAX:
		if (in_info == NULL)
			break;
		m0_free(in_info->ma_arr);
		m0_free(in_info);
		m0_free(op_args);
	}
}

static bool is_last_service(struct mm_args *in_info)
{
	return in_info->ma_curr_svc_id + 1 == in_info->ma_svc_nr;
}

static uint32_t offset_calc(struct mm_args *in_info)
{
	return in_info->ma_curr_svc_id * in_info->ma_chunk_len;
}

static uint32_t chunk_len_calc(struct mm_args *in_info)
{
	/*
	 * All services except one get ma_chunk_len of an array.
	 * Need to incorporate the remainder into last service.
	 */
	return is_last_service(in_info) ?
		in_info->ma_len - offset_calc(in_info) :
		in_info->ma_chunk_len;
}

static int minmax_input_prepare(struct m0_buf *out, struct m0_fid *comp_fid,
				uint32_t *reply_len, enum isc_comp_type type,
				struct mm_args *in_info)
{
	int           rc;
	struct m0_buf buf = M0_BUF_INIT0;
	struct isc_args a;

	*out = M0_BUF_INIT0;

	a.ia_arr = in_info->ma_arr + offset_calc(in_info);
	a.ia_len = chunk_len_calc(in_info);

	rc = m0_xcode_obj_enc_to_buf(&M0_XCODE_OBJ(isc_args_xc, &a),
				     &buf.b_addr, &buf.b_nob);
	if (rc != 0)
		return rc;

	printf("array of %u elements\n", a.ia_len);
	rc = m0_buf_copy_aligned(out, &buf, M0_0VEC_SHIFT);
	m0_buf_free(&buf);

	if (type == ICT_MIN)
		fid_get("arr_min", comp_fid);
	else
		fid_get("arr_max", comp_fid);

	*reply_len = CBL_DEFAULT_MAX;

	return rc;
}

static int ping_input_prepare(struct m0_buf *buf, struct m0_fid *comp_fid,
			      uint32_t *reply_len, enum isc_comp_type type)
{
	char *greeting;

	*buf = M0_BUF_INIT0;
	greeting = m0_strdup("Hello");
	if (greeting == NULL)
		return -ENOMEM;

	m0_buf_init(buf, greeting, strlen(greeting));
	fid_get("hello_world", comp_fid);
	*reply_len = CBL_DEFAULT_MAX;

	return 0;
}

static int input_prepare(struct m0_buf *buf, struct m0_fid *comp_fid,
			 uint32_t *reply_len, enum isc_comp_type type,
			 void *ip_args)
{
	switch (type) {
	case ICT_PING:
		return ping_input_prepare(buf, comp_fid, reply_len, type);
	case ICT_MIN:
	case ICT_MAX:
		return minmax_input_prepare(buf, comp_fid, reply_len, type,
					    ip_args);
	}
	return -EINVAL;
}

struct mm_result *op_result(struct mm_result *x, struct mm_result *y,
			    enum isc_comp_type op_type)
{
	switch (op_type) {
	case ICT_MIN:
		return x->mr_val <= y->mr_val ? x : y;
	case ICT_MAX:
		return x->mr_val >= y->mr_val ? x : y;
	default:
		return NULL;
	}
}

static void *minmax_output_prepare(struct m0_buf *result,
				   struct mm_args *in_info,
				   struct mm_result *prev,
				   enum isc_comp_type type)
{
	int              rc;
	struct mm_result new = {};

	rc = m0_xcode_obj_dec_from_buf(&M0_XCODE_OBJ(mm_result_xc, &new),
				       result->b_addr, result->b_nob);
	if (rc != 0) {
		fprintf(stderr, "failed to parse result: rc=%d", rc);
		goto out;
	}
	if (prev == NULL) {
		M0_ALLOC_PTR(prev);
		*prev = new;
		goto out;
	}
	/* Service sent index from sub-array. */
	new.mr_idx += offset_calc(in_info);
	/* Copy the current resulting value. */
	*prev = *op_result(prev, &new, type);
 out:
	/* Print the output. */
	if (is_last_service(in_info))
		fprintf(stderr, "idx=%d val=%lf\n", prev->mr_idx,
			prev->mr_val);
	/* Bookkeep the count of services communicated so far. */
	++in_info->ma_curr_svc_id;

	return prev;
}

/**
 * Apart from processing the output this can deserialize the buffer into
 * a structure relevant to the result of invoked computation.
 * and return the same.
 */
static void* output_process(struct m0_buf *result, void *in_args,
			    void *op_args, enum isc_comp_type type)
{
	switch (type) {
	case ICT_PING:
		printf ("Hello-%s@"FID_F"\n", (char *)result->b_addr,
			FID_P((struct m0_fid *)op_args));
		memset(result->b_addr, 'a', result->b_nob);
		return NULL;
	case ICT_MIN:
	case ICT_MAX:
		return minmax_output_prepare(result, in_args, op_args, type);
	}
	return NULL;
}

char *prog;

static void usage_print()
{
	fprintf(stderr, "Usage: %s op_name\n\n", prog);
	fprintf(stderr, "  Supported operations: ping, min, max.\n");
	fprintf(stderr, "  Refer to README.isc.\n");
}

int main(int argc, char **argv)
{
	struct m0_buf          buf;
	struct m0_buf          result;
	struct m0_fid          comp_fid;
	struct c0appz_isc_req  req;
	/* Holds arguments specific to an operation. */
	void                  *ip_args;
	/* Holds output specific to a computation. */
	void                  *op_args;
	struct m0_fid          svc_fid;
	struct m0_fid          start_fid = M0_FID0;
	uint32_t               reply_len;
	int                    op_type;
	int                    rc;

	prog = basename(strdup(argv[0]));

	/* check input */
	if (argc != 2) {
		usage_print();
		return -1;
	}

	/* time in */
	c0appz_timein();

	c0appz_setrc(prog);
	c0appz_putrc();

	op_type = op_type_parse(argv[1]);
	if (op_type == -EINVAL) {
		usage_print(prog);
		return -EINVAL;
	}

	m0trace_on = true;

	/* initialize resources */
	rc = c0appz_init(0);
	if (rc != 0) {
		fprintf(stderr,"error! c0appz_init() failed: %d\n", rc);
		return -2;
	}

	if (isc_services_count() == 0) {
		fprintf(stderr, "\nISC services are not started\n");
		rc = -EINVAL;
		goto out;
	}

	m0_xc_isc_libdemo_init();

	op_args = NULL;
	ip_args = NULL;

	/* Initialise the  parameters for operation. */
	rc = op_init(op_type, &ip_args);
	while (rc == 0) {
		rc = c0appz_isc_nxt_svc_get(&start_fid, &svc_fid, M0_CST_ISCS);
		if (rc != 0)
			break;

		/* Prepare arguments for computation. */
		rc = input_prepare(&buf, &comp_fid, &reply_len,
				   op_type, ip_args);
		if (rc != 0) {
			fprintf(stderr,
				"error! Input preparation failed: %d\n", rc);
			break;
		}
		rc = c0appz_isc_req_prepare(&req, &buf, &comp_fid, &result,
					    &svc_fid, reply_len);
		if (rc != 0) {
			m0_buf_free(&buf);
			fprintf(stderr,
				"error! request preparation failed: %d\n", rc);
			goto out;
		}
		/*
		 * XXX: To achieve parallelism across services need to change
		 * this to async.
		 */
		rc = c0appz_isc_req_send_sync(&req);
		if (rc == 0) {
			if (op_type == ICT_PING)
				op_args = &svc_fid;
			op_args = output_process(&result, ip_args, op_args,
						 op_type);
		} else {
			fprintf(stderr, "Error from "FID_F" received: rc=%d\n"
				"Check if the relevant library is loaded\n",
				FID_P(&svc_fid), rc);
		}
		c0appz_isc_req_fini(&req);
		start_fid = svc_fid;
	}
	/* Ignore the case when all services are iterated. */
	if (rc == -ENOENT)
		rc = 0;
	op_fini(op_type, ip_args, op_args);
 out:
	/* free resources*/
	c0appz_free();

	/* time out */
	c0appz_timeout(0);

	return rc;
}

/*
 *  Local variables:
 *  c-indentation-style: "K&R"
 *  c-basic-offset: 8
 *  tab-width: 8
 *  fill-column: 80
 *  scroll-step: 1
 *  End:
 */
