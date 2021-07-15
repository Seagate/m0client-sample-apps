/* -*- C -*- */
/*
 * Copyright (c) 2017-2020 Seagate Technology LLC and/or its Affiliates
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
 * Original author:  Ganesan Umanesan <ganesan.umanesan@seagate.com>
 * Original creation date: 24-Jan-2017
 */

#include <stdio.h>
#include <stdlib.h>
#include <libgen.h>
#include <string.h>
#include <errno.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <inttypes.h>
#include <assert.h>
#include <ctype.h>
#include "c0appz.h"
#include "c0appz_internal.h"

/*
 ******************************************************************************
 * EXTERN VARIABLES
 ******************************************************************************
 */
extern int perf; 	/* performance 		*/
int force=0; 		/* overwrite  		*/
extern uint64_t qos_whgt_served;
extern uint64_t qos_whgt_remain;
extern uint64_t qos_laps_served;
extern uint64_t qos_laps_remain;
extern pthread_mutex_t qos_lock;       /* lock  qos_total_weight */

char *prog;

const char *help_c0cp_txt = "\
Usage:\n\
  c0cp [-fptv] [-x id] [-u sz] [-b [sz]] [-c n] [-a n] idh idl file bsz\n\
  c0cp 1234 56789 file 64\n\
\n\
Copy file to the object store.\n\
\n\
idh - object id high number\n\
idl - object id low  number\n\
bsz - block size (in KiBs)\n\
\n\
  -a n    async mode: do n object store operations in a batch\n\
  -b [sz] block size for the object store i/o (m0bs);\n\
            if sz is not specified, automatically figure out the optimal\n\
            value based on the object size and the pool width (does not\n\
            work for the composite objects atm); (by default, use bsz)\n\
  -c n    write n contiguous copies of the file into the object\n\
  -f      force: overwrite an existing object\n\
  -p      show performance stats\n\
  -u sz   unit size (in KiBs), must be power of 2, >= 4 and <= 4096;\n\
            by default, determined automatically for the new objects\n\
            based on the m0bs and parity configuration of the pool\n\
  -x id   id of the tier (pool) to create the object in (1,2,3,..)\n\
  -t      create m0trace.pid file\n\
  -v      be more verbose\n\
  -h      print this help\n\
\n\
Note: in order to get the maximum performance, m0bs should be multiple\n\
of the data size in the parity group, i.e. multiple of (unit_size * n),\n\
where n is 2 in 2+1 parity group configuration, 8 in 8+2 and so on.\n";

int help()
{
	fprintf(stderr, "%s\n%s\n", help_c0cp_txt, c0appz_help_txt);
	exit(1);
}

int main(int argc, char **argv)
{
	uint64_t idh;        /* object id high    */
	uint64_t idl;        /* object is low     */
	uint64_t bsz;        /* block size        */
	uint64_t m0bs=0;     /* m0 block size     */
	uint64_t cnt;        /* count             */
	uint64_t pos;        /* starting position */
	char    *fname;      /* input filename    */
	struct stat64 fs;    /* file statistics   */
	int      opt;        /* options           */
	int      rc;         /* return code       */
	char    *fbuf;       /* file buffer       */
	int      cont=0;     /* continuous mode   */
	int      pool=0;     /* default pool ID   */
	int      op_cnt=0;   /* number of parallel ops */

	prog = basename(strdup(argv[0]));

	while ((opt = getopt(argc, argv, ":a:b:pfc:x:u:tvh")) != -1) {
		switch (opt) {
		case 'p':
			perf = 1;
			break;
		case 'f':
			force = 1;
			break;
		case 'a':
			op_cnt = atoi(optarg);
			if (op_cnt < 1) {
				ERR("invalid -a option argument: %s\n", optarg);
				help();
			}
			break;
		case 'b':
			if (sscanf(optarg, "%li", &m0bs) != 1) {
				/* optarg might contain some other options */
				optind--; /* rewind */
				m0bs = 1; /* get automatically */
			} else
				m0bs *= 1024;
			break;
		case 'c':
			cont = atoi(optarg);
			if (cont < 1) {
				ERR("invalid -c option argument: %s\n", optarg);
				help();
			}
			break;
		case 'u':
			if (sscanf(optarg, "%i", &unit_size) != 1) {
				ERR("invalid unit size: %s\n", optarg);
				help();
			}
			break;
		case 'x':
			pool = atoi(optarg);
			if (pool < 1 || pool > 3) {
				ERR("invalid pool index: %s (allowed \n"
				    "values are 1, 2 or 3 atm)\n", optarg);
				help();
			}
			break;
		case 't':
			m0trace_on = true;
			break;
		case 'v':
			trace_level++;
			break;
		case 'h':
			help();
			break;
		case ':':
			switch (optopt) {
			case 'b':
				m0bs = 1; /* get automatically */
				break;
			default:
				ERR("option -%c needs a value\n", optopt);
				help();
			}
			break;
		case '?':
			fprintf(stderr, "unknown option: %c\n", optopt);
			help();
			break;
		default:
			fprintf(stderr, "unknown option: %c\n", optopt);
			help();
			break;
		}
	}

	/* check input */
	if (argc - optind != 4)
		help();

	c0appz_setrc(prog);
	c0appz_putrc();

	/* set input */
	if (sscanf(argv[optind+0], "%li", &idh) != 1) {
		ERR("invalid idhi - %s", argv[optind+0]);
		help();
	}
	if (sscanf(argv[optind+1], "%li", &idl) != 1) {
		ERR("invalid idlo - %s", argv[optind+1]);
		help();
	}
	fname = argv[optind+2];
	if (sscanf(argv[optind+3], "%li", &bsz) != 1) {
		ERR("invalid block size - %s", argv[optind+3]);
		help();
	}
	bsz *= 1024;

	if (bsz == 0) {
		fprintf(stderr,"%s: bsz must be > 0\n", prog);
		help();
	}

	rc = stat64(fname, &fs);
	if (rc != 0) {
		ERRS("%s", fname);
		exit(1);
	}
	cnt = (fs.st_size + bsz - 1) / bsz;

	/* init */
	c0appz_timein();
	rc = c0appz_init(0);
	if (rc != 0) {
		fprintf(stderr,"%s(): error: c0appz_init() failed: %d\n",
			__func__, rc);
		return 222;
	}
	ppf("%8s","init");
	c0appz_timeout(0);

	if (m0bs == 1) {
		m0bs = c0appz_m0bs(idh, idl, bsz * cnt, pool);
		if (!m0bs)
			LOG("WARNING: failed to figure out the optimal m0bs,"
			    " will use bsz for m0bs\n");
	}
	if (!m0bs)
		m0bs = bsz;

	/* create object */
	c0appz_timein();
	rc = c0appz_cr(idh, idl, pool, m0bs);
	if (rc < 0 || (rc != 0 && !force)) {
		if (rc < 0)
			fprintf(stderr,"%s(): object create failed: rc=%d\n",
				__func__, rc);
		rc = 333;
		goto end;
	}
	ppf("%8s","create");
	c0appz_timeout(0);

	/* continuous write */
	if (cont) {
		fbuf = malloc(fs.st_size + bsz - 1);
		if (!fbuf) {
			fprintf(stderr,"%s(): error: not enough memory.\n",
				__func__);
			rc = 111;
			goto end;
		}
		rc = c0appz_fr(fbuf, fname, bsz, cnt);
		if (rc != 0) {
			fprintf(stderr,"%s(): c0appz_fr failed: rc=%d\n",
				__func__, rc);
			rc = 555;
			goto end;
		}

		qos_whgt_served = 0;
		qos_whgt_remain = bsz * cnt * cont;
		qos_laps_served = 0;
		qos_laps_remain = cont;
		qos_pthread_start();
		c0appz_timein();

		for (pos = 0; cont > 0; cont--, pos += cnt * bsz) {
			rc = op_cnt ?
				c0appz_mw_async(fbuf, idh, idl, pos, bsz, cnt,
						op_cnt, m0bs) :
				c0appz_mw(fbuf, idh, idl, pos, bsz, cnt, m0bs);
			if (rc != 0) {
				ERR("copying failed at pos %lu: %d\n", pos, rc);
				break;
			}
			pthread_mutex_lock(&qos_lock);
			qos_laps_served++;
			qos_laps_remain--;
			pthread_mutex_unlock(&qos_lock);
		}

		ppf("%8s","write");
		c0appz_timeout(pos);
		qos_pthread_wait();
		printf("%" PRIu64 " x %" PRIu64 " = %" PRIu64 "\n", cnt, bsz,
		       cnt * bsz);
		free(fbuf);
		goto end;
	}

	qos_whgt_served = 0;
	qos_whgt_remain = bsz * cnt;
	qos_laps_served = 0;
	qos_laps_remain = 1;
	qos_pthread_start();
	c0appz_timein();

	/* copy */
	if (op_cnt)
		rc = c0appz_cp_async(idh, idl, fname, bsz, cnt, op_cnt, m0bs);
	else
		rc = c0appz_cp(idh, idl, fname, bsz, cnt, m0bs);
	if (rc != 0) {
		ERR("copying failed: rc=%d\n", rc);
		rc = 222;
		goto end;
	};
	pthread_mutex_lock(&qos_lock);
	qos_laps_served++;
	qos_laps_remain--;
	pthread_mutex_unlock(&qos_lock);
	ppf("%8s","copy");
	c0appz_timeout(bsz*cnt);
	qos_pthread_wait();

end:
	/* free */
	c0appz_timein();
	c0appz_free();
	ppf("%8s","free");
	c0appz_timeout(0);

	/* failure */
	if (rc != 0) {
		printf("%s %" PRIu64 "\n",fname,fs.st_size);
		fprintf(stderr,"%s failed!\n", prog);
		return rc;
	}

	/* success */
	c0appz_dump_perf();
	printf("%s %" PRIu64 "\n",fname,fs.st_size);
	fprintf(stderr,"%s success\n", prog);
	return 0;
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
