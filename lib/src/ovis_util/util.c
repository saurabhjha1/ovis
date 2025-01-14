/* -*- c-basic-offset: 8 -*-
 * Copyright (c) 2013-2015 Open Grid Computing, Inc. All rights reserved.
 * Copyright (c) 2013-2017 Sandia Corporation. All rights reserved.
 * Under the terms of Contract DE-AC04-94AL85000, there is a non-exclusive
 * license for use of this work by or on behalf of the U.S. Government.
 * Export of this program may require a license from the United States
 * Government.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the BSD-type
 * license below:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *      Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *
 *      Redistributions in binary form must reproduce the above
 *      copyright notice, this list of conditions and the following
 *      disclaimer in the documentation and/or other materials provided
 *      with the distribution.
 *
 *      Neither the name of Sandia nor the names of any contributors may
 *      be used to endorse or promote products derived from this software
 *      without specific prior written permission.
 *
 *      Neither the name of Open Grid Computing nor the names of any
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 *      Modified source versions must be plainly marked as such, and
 *      must not be misrepresented as being the original software.
 *
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <sys/errno.h>
#include <linux/limits.h>
#include <string.h>
#include <malloc.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <stdio.h>
#include <regex.h>
#include <assert.h>
#include <errno.h>
#include <pwd.h>
#include <grp.h>
#include <ctype.h>

#include "util.h"
#define DSTRING_USE_SHORT
#include "dstring.h"

static const char *get_env_var(const char *src, size_t start_off, size_t end_off)
{
	static char name[100];
	size_t name_len;

	/* Strip $, {, and } from the name */
	if (src[start_off] == '$')
		start_off += 1;
	if (src[start_off] == '{')
		start_off += 1;
	if (src[end_off-1] == '}')
		end_off -= 1;

	name_len = end_off - start_off;
	assert(name_len < sizeof(name));

	strncpy(name,  &src[start_off], name_len);
	name[name_len] = '\0';
	return name;
}

int _scpy(char **buff, size_t *slen, size_t *alen,
	    const char *str, size_t len)
{
	size_t xlen;
	char *xbuff;
	if (len >= *alen) {
		/* need extension */
		xlen = ((len-1)|4095) + 1;
		xbuff = realloc(*buff, *slen+*alen+xlen);
		if (!xbuff) {
			return errno;
		}
		*buff = xbuff;
	}
	strncpy(*buff+*slen, str, len);
	*alen -= len;
	*slen += len;
	(*buff)[*slen] = 0;
	return 0;
}

char *str_repl_cmd(const char *_str)
{
	char *str = strdup(_str);
	char *buff = NULL;
	char *xbuff;
	const char *eos = str + strlen(str);
	size_t slen = 0; /* strlen of buff */
	size_t alen = 4096; /* available len */
	size_t len;
	char *p0 = str, *p1;
	char *cmd;
	int rc;
	int count;
	FILE *p;

	if (!str)
		goto err;

	buff = malloc(alen);
	if (!buff)
		goto err;

	while (p0 < eos) {
		p1 = strstr(p0, "$(");
		if (!p1) {
			/* no more expansion */
			len = strlen(p0);
			rc = _scpy(&buff, &slen, &alen, p0, len);
			if (rc)
				goto err;
			break;
		}
		/* otherwise, do the expansion */
		/* copy the non-expandable part */
		len = p1 - p0;
		rc = _scpy(&buff, &slen, &alen, p0, len);
		if (rc)
			goto err;
		/* expansion */
		cmd = p1 + 2;
		p0 = cmd;
		count = 1;
		/* matching parenthesis */
		while (count && p0 < eos) {
			if (*p0 == '(')
				count++;
			if (*p0 == ')')
				count--;
			if (*p0 == '\\') {
				/* escape */
				p0++;
			}
			p0++;
		}
		if (count) {
			/* end unexpectedly */
			errno = EINVAL;
			goto err;
		}
		/* p0 points to the char behind ')' */
		*(p0-1) = 0; /* terminate the cmd */
		p = popen(cmd, "re"); /* read & close-on-exec */
		if (!p)
			goto err;
		/* copy the output over */
		while (fgets(buff+slen, alen, p)) {
			len = strlen(buff+slen);
			slen += len;
			alen -= len;
			/* replace trailing spaces with one space */
			while (isspace(buff[slen-1])) {
				slen--;
				alen++;
			}
			if (isspace(buff[slen])) {
				buff[slen] = ' ';
				buff[++slen] = 0;
				alen--;
			}
			if (alen == 1) {
				/* need buff extension */
				xbuff = realloc(buff, slen+alen+4096);
				if (!xbuff)
					goto err;
				buff = xbuff;
				alen += 4096;
			}
		}
		pclose(p);
		/* remove the trailing spaces at the end */
		while (isspace(buff[slen-1])) {
			buff[slen-1] = 0;
			slen--;
			alen++;
		}
	}
	free(str); /* cleanup */
	return buff;

err:
	free(str);
	free(buff);
	return NULL;
}

char *str_repl_env_vars(const char *str)
{
	if (!str)
		return NULL;
	const char *name;
	const char *value;
	const char *values[100];
	char *res, *res_ptr;
	size_t res_len;
	regex_t regex;
	char *expr = "\\$\\{[[:alnum:]_]+\\}";
	int rc, i;
	regmatch_t match[100];
	size_t name_total, value_total;

	name_total = value_total = 0;

	rc = regcomp(&regex, expr, REG_EXTENDED);
	assert(rc == 0);
	int pos;
	rc = 0;
	for (i = pos = 0; !rc; pos = match[i].rm_eo, i++) {
		match[i].rm_so = -1;
		rc = regexec(&regex, &str[pos], 1, &match[i], 0);
		if (rc)
			break;
		res_len = match[i].rm_eo - match[i].rm_so;
		name_total += res_len;
		name = get_env_var(&str[pos], match[i].rm_so, match[i].rm_eo);
		value = getenv(name);
		if (value) {
			value_total += strlen(value);
			values[i] = value;
		} else {
			values[i] = "";
		}
		match[i].rm_eo += pos;
		match[i].rm_so += pos;
	}
	regfree(&regex);
	if (!i)
		return strdup(str);

	/* Allocate the result string */
	res_len = strlen(str) - name_total + value_total + 1;
	res = malloc(res_len);
	if (!res) {
		errno = ENOMEM;
		return NULL;
	}

	size_t so = 0;

	res_ptr = res;
	res[0] = '\0';
	for (i = 0; i < sizeof(match) / sizeof(match[0]); i++) {
		if (match[i].rm_so < 0)
			break;
		size_t len = match[i].rm_so - so;
		memcpy(res_ptr, &str[so], len);
		res_ptr[len] = '\0';
		res_ptr = &res_ptr[len];
		so = match[i].rm_eo;
		strcpy(res_ptr, values[i]);
		res_ptr = &res[strlen(res)];
	}
	/* Copy the remainder of str into the result */
	strcpy(res_ptr, &str[so]);
	assert(res_len == (strlen(res) + 1));

	return res;
}

char *av_name(struct attr_value_list *av_list, int idx)
{
	if (!av_list)
		return NULL;
	if (idx < av_list->count)
		return av_list->list[idx].name;
	return NULL;
}

char *av_value(struct attr_value_list *av_list, const char *name)
{
	if (!av_list)
		return NULL;
	int i;
	for (i = 0; i < av_list->count; i++) {
		if (strcmp(name, av_list->list[i].name))
			continue;
		char *str = str_repl_env_vars(av_list->list[i].value);
		if (str) {
			string_ref_t ref = malloc(sizeof(*ref));
			if (!ref) {
				free(str);
				return NULL;
			}
			ref->str = str;
			LIST_INSERT_HEAD(&av_list->strings, ref, entry);
			return str;
		} else
			break;
	}
	return NULL;
}

int av_idx_of(struct attr_value_list *av_list, const char *name)
{
	if (!av_list)
		return -1;
	int i;
	int k = -1;
	for (i = 0; i < av_list->count; i++) {
		if (strcmp(name, av_list->list[i].name))
			continue;
		if (k == -1) {
			k = i;
			continue;
		}
		if (k < 0)
			k--;
		else
			k = -2;
	}
	return k;
}

char *av_value_at_idx(struct attr_value_list *av_list, int i)
{
	if (i < av_list->count) {
		char *str = str_repl_env_vars(av_list->list[i].value);
		if (str) {
			string_ref_t ref = malloc(sizeof(*ref));
			if (!ref) {
				free(str);
				return NULL;
			}
			ref->str = str;
			LIST_INSERT_HEAD(&av_list->strings, ref, entry);
			return str;
		}
	}
	return NULL;
}

int tokenize(char *cmd,
	     struct attr_value_list *kw_list,
	     struct attr_value_list *av_list)
{
	char *token, *next_token, *ptr;
	struct attr_value *av;
	int next_av, next_kw;
	next_av = next_kw = 0;
	for (token = strtok_r(cmd, " \t\n", &ptr); token;) {
		char *value = strstr(token, "=");
		next_token = strtok_r(NULL, " \t\n", &ptr);
		if (value) {
			if (next_av >= av_list->size)
				goto err;
			av = &(av_list->list[next_av++]);
			av->name = token;
			*value = '\0';
			value++;
			av->value = value;
		} else {
			if (next_kw >= kw_list->size)
				goto err;
			kw_list->list[next_kw].name = token;
			kw_list->list[next_kw].value = NULL;
			next_kw++;
		}
		token = next_token;
	}
	kw_list->count = next_kw;
	av_list->count = next_av;
	return 0;
err:
	return ENOMEM;
}

void av_free(struct attr_value_list *avl)
{
	if (!avl)
		return;
	string_ref_t ref;
	while (!LIST_EMPTY(&avl->strings)) {
		ref = LIST_FIRST(&avl->strings);
		free(ref->str);
		LIST_REMOVE(ref, entry);
		free(ref);
	}
	free(avl);
}

struct attr_value_list *av_new(size_t size)
{
	size_t bytes = sizeof(struct attr_value_list) +
		       (sizeof(struct attr_value) * size);
	struct attr_value_list *avl = malloc(bytes);

	if (avl) {
		memset(avl, 0, bytes);
		avl->size = size;
		LIST_INIT(&avl->strings);
	}
	return avl;
}


/* copy s into string list. return copy, or null if malloc fails. */
static char *copy_string(struct attr_value_list *av_list, const char *s)
{
	char *str = strdup(s);
	if (!str)
		return NULL;
	string_ref_t ref = malloc(sizeof(*ref));
	if (!ref) {
		free(str);
		return NULL;
	}
	ref->str = str;
	LIST_INSERT_HEAD(&av_list->strings, ref, entry);
	return str;
}

struct attr_value_list *av_copy(struct attr_value_list *src)
{
	struct attr_value *av;
	int pos = 0;
	if (!src)
		return NULL;
	struct attr_value_list *r = av_new(src->size);
	for ( ; pos < src->count; pos++) {
		char *name = src->list[pos].name;
		char *value = src->list[pos].value;
		av = &(r->list[pos]);
		if (value) {
			char *sv = copy_string(r, value);
			if (!sv) {
				errno = ENOMEM;
				goto err;
			}
			av->value = sv;
		}
		char *sn = copy_string(r, name);
		av->name = sn;
		if (!sn) {
			errno = ENOMEM;
			goto err;
		}
	}
	r->count = src->count;
	return r;
err:
	av_free(r);
	return NULL;
}

char *av_to_string(struct attr_value_list *av, int replacements)
{
	if (!av)
		return NULL;
	char *val;
	int i;
	dsinit(ds);
	if (av->count < 1)
		dscat(ds, "(empty)");
	if (replacements & AV_EXPAND) {
		for (i=0; i < av->count; i++) {
			dscat(ds, av->list[i].name);
			if ( (val = av_value_at_idx(av, i)) != NULL) {
				dscat(ds, "=");
				dscat(ds, val);
			}
			if (replacements & AV_NL)
			dscat(ds, "\n");
			else
				dscat(ds, " ");
		}
	} else {
		for (i=0; i < av->count; i++) {
			dscat(ds, av->list[i].name);
			if ( av->list[i].value != NULL) {
				dscat(ds, "=");
				dscat(ds, av->list[i].value);
			}
			if (replacements & AV_NL)
			dscat(ds, "\n");
			else
				dscat(ds, " ");
		}
	}
	val = dsdone(ds);
	dstr_free(&ds);
	return val;
}

int av_check_expansion(printf_t log, const char *n, const char *s)
{
	if (!s)
		return 0;
	if (strchr(s,'$') != NULL) {
		log("Unbraced or undefined variable in %s=%s\n", n, s);
		return 1;
	}
	return 0;
}

size_t ovis_get_mem_size(const char *s)
{
    char unit;
    char tmp[256];
    sprintf(tmp, "%s%s", s, "B");
    size_t size;
    sscanf(tmp, "%lu %c", &size, &unit);
    switch (unit) {
    case 'b':
    case 'B':
            return size;
    case 'k':
    case 'K':
            return size * 1024L;
    case 'm':
    case 'M':
            return size * 1024L * 1024L;
    case 'g':
    case 'G':
            return size * 1024L * 1024L * 1024L;
    case 't':
    case 'T':
            return size * 1024L * 1024L * 1024L;
    default:
            return 0;
    }
}

pid_t ovis_execute(const char *command)
{
	char *argv[] = {"/bin/sh", "-c", (char*)command, NULL};
	pid_t pid = fork();
	int i;
	if (pid)
		return pid;
	/* close all parent's file descriptor */
	for (i = getdtablesize() - 1; i > -1; i--) {
		close(i); /* blindly closes everything before exec */
	}
	execv("/bin/sh", argv);
	return pid;
}

#define DSTRING_USE_SHORT
#include "dstring.h"
char *ovis_join(char *pathsep, ...)
{
	va_list ap;
	char *result = NULL;
	char *tmp = NULL;
	const char *n = NULL;
	char *sep = "/";
	if (pathsep)
		sep = pathsep;

	dsinit(ds);
	va_start(ap, pathsep);
	n = va_arg(ap, const char *);
	if (n)
		tmp = dscat(ds, n);
	else
		errno = EINVAL;

	while ( tmp && (n = va_arg(ap, const char *)) != NULL) {
		dscat(ds, sep);
		tmp = dscat(ds, n);
	}

	va_end(ap);

	result = dsdone(ds);
	if (!tmp) {
		free(result);
		return NULL;
	}
	return result;
}

int ovis_join_buf(char *buf, size_t buflen, char *pathsep, ...)
{
	int rc = 0;
	va_list ap;
	const char *n = NULL;
	char *sep = "/";
	if (pathsep)
		sep = pathsep;
	size_t sepsize = strlen(sep);
	size_t len = 0;
	size_t chunk;

	if (!buf)
		return EINVAL;

	va_start(ap, pathsep);
	n = va_arg(ap, const char *);
	if (!n) {
		rc = EINVAL;
		goto out;
	}

	chunk = strlen(n);
	if ( (len + chunk) < buflen) {
		printf("chunk %zu %s\n", chunk, n);
		strncat(buf + len, n, chunk);
		len += chunk;
	} else {
		rc = E2BIG;
	}

	while ( 0 == rc && (n = va_arg(ap, const char *)) != NULL) {
		if ((len + sepsize) < buflen) {
			strncat(buf + len, sep, sepsize);
			len += sepsize;
		} else {
			rc = E2BIG;
			break;
		}
		chunk = strlen(n);
		if ((len + chunk) < buflen) {
			strncat(buf + len, n, chunk);
			len += chunk;
		} else {
			rc = E2BIG;
			break;
		}
	}
 out:
	va_end(ap);
	buf[len] = '\0';

	return rc;
}

int f_file_exists(const char *path)
{
	struct stat st;
	int rc = stat(path, &st);
	return !rc;
}

int f_is_dir(const char *path)
{
	struct stat st;
	int rc = stat(path, &st);
	if (rc == -1)
		return 0;
	rc = S_ISDIR(st.st_mode);
	if (!rc) {
		errno = ENOTDIR;
	}
	return rc;
}

int f_mkdir_p(const char *path, __mode_t mode)
{
	char *str = strdup(path);
	char *_str;
	int rc = 0;
	if (!str)
		return ENOMEM;
	_str = str;
	int len = strlen(str);
	if (str[len-1] == '/') {
		len--;
		str[len] = 0;
	}
	if (_str[0] == '/')
		_str++; /* skip the leading '/' */
	while ((_str = strstr(_str, "/"))) {
		*_str = 0;
		if (!f_file_exists(str)) {
			if (mkdir(str, mode) == -1) {
				rc = errno;
				goto cleanup;
			}
		}
		if (!f_is_dir(str)) {
			errno = ENOTDIR;
			rc = ENOTDIR;
			goto cleanup;
		}
		*_str = '/';
		_str++;
	}
	rc = mkdir(str, mode);
	if (rc)
		rc = errno;
cleanup:
	free(str);
	return rc;
}

FILE *fopen_perm(const char *path, const char *f_mode, int o_mode)
{
	int fd;
	int flags = O_RDWR|O_CREAT;

	if (*f_mode == 'w')
		flags |= O_TRUNC;
	fd = open(path, flags, o_mode);
	if (fd < 0)
		return NULL;
	return fdopen(fd, f_mode);
}

/*
 * \retval 0 OK
 * \retval errno for error
 */
int __uid_gid_check(uid_t uid, gid_t gid)
{
	struct passwd pw;
	struct passwd *p;
	int rc;
	char *buf;
	int i;
	int n = 128;
	gid_t gid_list[n];

	buf = malloc(BUFSIZ);
	if (!buf) {
		rc = ENOMEM;
		goto out;
	}

	rc = getpwuid_r(uid, &pw, buf, BUFSIZ, &p);
	if (!p) {
		rc = rc?rc:ENOENT;
		goto out;
	}

	rc = getgrouplist(p->pw_name, p->pw_gid, gid_list, &n);
	if (rc == -1) {
		rc = ENOBUFS;
		goto out;
	}

	for (i = 0; i < n; i++) {
		if (gid == gid_list[i]) {
			rc = 0;
			goto out;
		}
	}

	rc = ENOENT;

out:
	if (buf)
		free(buf);
	return rc;
}
int ovis_access_check(uid_t auid, gid_t agid, int acc,
		      uid_t ouid, gid_t ogid, int perm)
{
	int macc = acc & perm;
	/* other */
	if (07 & macc) {
		return 0;
	}
	/* owner */
	if (0700 & macc) {
		if (auid == ouid)
			return 0;
	}
	/* group  */
	if (070 & macc) {
		if (agid == ogid)
			return 0;
		/* else need to check x->ruid group list */
		if (0 == __uid_gid_check(auid, ouid))
			return 0;
	}
	return EACCES;
}

const char* ovis_errno_abbvr(int e)
{
	const char *estr[] = {
		[EPERM]            =  "EPERM",
		[ENOENT]           =  "ENOENT",
		[ESRCH]            =  "ESRCH",
		[EINTR]            =  "EINTR",
		[EIO]              =  "EIO",
		[ENXIO]            =  "ENXIO",
		[E2BIG]            =  "E2BIG",
		[ENOEXEC]          =  "ENOEXEC",
		[EBADF]            =  "EBADF",
		[ECHILD]           =  "ECHILD",
		[EAGAIN]           =  "EAGAIN",
		[ENOMEM]           =  "ENOMEM",
		[EACCES]           =  "EACCES",
		[EFAULT]           =  "EFAULT",
		[ENOTBLK]          =  "ENOTBLK",
		[EBUSY]            =  "EBUSY",
		[EEXIST]           =  "EEXIST",
		[EXDEV]            =  "EXDEV",
		[ENODEV]           =  "ENODEV",
		[ENOTDIR]          =  "ENOTDIR",
		[EISDIR]           =  "EISDIR",
		[EINVAL]           =  "EINVAL",
		[ENFILE]           =  "ENFILE",
		[EMFILE]           =  "EMFILE",
		[ENOTTY]           =  "ENOTTY",
		[ETXTBSY]          =  "ETXTBSY",
		[EFBIG]            =  "EFBIG",
		[ENOSPC]           =  "ENOSPC",
		[ESPIPE]           =  "ESPIPE",
		[EROFS]            =  "EROFS",
		[EMLINK]           =  "EMLINK",
		[EPIPE]            =  "EPIPE",
		[EDOM]             =  "EDOM",
		[ERANGE]           =  "ERANGE",
		[EDEADLK]          =  "EDEADLK",
		[ENAMETOOLONG]     =  "ENAMETOOLONG",
		[ENOLCK]           =  "ENOLCK",
		[ENOSYS]           =  "ENOSYS",
		[ENOTEMPTY]        =  "ENOTEMPTY",
		[ELOOP]            =  "ELOOP",
		[ENOMSG]           =  "ENOMSG",
		[EIDRM]            =  "EIDRM",
		[ECHRNG]           =  "ECHRNG",
		[EL2NSYNC]         =  "EL2NSYNC",
		[EL3HLT]           =  "EL3HLT",
		[EL3RST]           =  "EL3RST",
		[ELNRNG]           =  "ELNRNG",
		[EUNATCH]          =  "EUNATCH",
		[ENOCSI]           =  "ENOCSI",
		[EL2HLT]           =  "EL2HLT",
		[EBADE]            =  "EBADE",
		[EBADR]            =  "EBADR",
		[EXFULL]           =  "EXFULL",
		[ENOANO]           =  "ENOANO",
		[EBADRQC]          =  "EBADRQC",
		[EBADSLT]          =  "EBADSLT",
		[EBFONT]           =  "EBFONT",
		[ENOSTR]           =  "ENOSTR",
		[ENODATA]          =  "ENODATA",
		[ETIME]            =  "ETIME",
		[ENOSR]            =  "ENOSR",
		[ENONET]           =  "ENONET",
		[ENOPKG]           =  "ENOPKG",
		[EREMOTE]          =  "EREMOTE",
		[ENOLINK]          =  "ENOLINK",
		[EADV]             =  "EADV",
		[ESRMNT]           =  "ESRMNT",
		[ECOMM]            =  "ECOMM",
		[EPROTO]           =  "EPROTO",
		[EMULTIHOP]        =  "EMULTIHOP",
		[EDOTDOT]          =  "EDOTDOT",
		[EBADMSG]          =  "EBADMSG",
		[EOVERFLOW]        =  "EOVERFLOW",
		[ENOTUNIQ]         =  "ENOTUNIQ",
		[EBADFD]           =  "EBADFD",
		[EREMCHG]          =  "EREMCHG",
		[ELIBACC]          =  "ELIBACC",
		[ELIBBAD]          =  "ELIBBAD",
		[ELIBSCN]          =  "ELIBSCN",
		[ELIBMAX]          =  "ELIBMAX",
		[ELIBEXEC]         =  "ELIBEXEC",
		[EILSEQ]           =  "EILSEQ",
		[ERESTART]         =  "ERESTART",
		[ESTRPIPE]         =  "ESTRPIPE",
		[EUSERS]           =  "EUSERS",
		[ENOTSOCK]         =  "ENOTSOCK",
		[EDESTADDRREQ]     =  "EDESTADDRREQ",
		[EMSGSIZE]         =  "EMSGSIZE",
		[EPROTOTYPE]       =  "EPROTOTYPE",
		[ENOPROTOOPT]      =  "ENOPROTOOPT",
		[EPROTONOSUPPORT]  =  "EPROTONOSUPPORT",
		[ESOCKTNOSUPPORT]  =  "ESOCKTNOSUPPORT",
		[EOPNOTSUPP]       =  "EOPNOTSUPP",
		[EPFNOSUPPORT]     =  "EPFNOSUPPORT",
		[EAFNOSUPPORT]     =  "EAFNOSUPPORT",
		[EADDRINUSE]       =  "EADDRINUSE",
		[EADDRNOTAVAIL]    =  "EADDRNOTAVAIL",
		[ENETDOWN]         =  "ENETDOWN",
		[ENETUNREACH]      =  "ENETUNREACH",
		[ENETRESET]        =  "ENETRESET",
		[ECONNABORTED]     =  "ECONNABORTED",
		[ECONNRESET]       =  "ECONNRESET",
		[ENOBUFS]          =  "ENOBUFS",
		[EISCONN]          =  "EISCONN",
		[ENOTCONN]         =  "ENOTCONN",
		[ESHUTDOWN]        =  "ESHUTDOWN",
		[ETOOMANYREFS]     =  "ETOOMANYREFS",
		[ETIMEDOUT]        =  "ETIMEDOUT",
		[ECONNREFUSED]     =  "ECONNREFUSED",
		[EHOSTDOWN]        =  "EHOSTDOWN",
		[EHOSTUNREACH]     =  "EHOSTUNREACH",
		[EALREADY]         =  "EALREADY",
		[EINPROGRESS]      =  "EINPROGRESS",
		[ESTALE]           =  "ESTALE",
		[EUCLEAN]          =  "EUCLEAN",
		[ENOTNAM]          =  "ENOTNAM",
		[ENAVAIL]          =  "ENAVAIL",
		[EISNAM]           =  "EISNAM",
		[EREMOTEIO]        =  "EREMOTEIO",
		[EDQUOT]           =  "EDQUOT",
		[ENOMEDIUM]        =  "ENOMEDIUM",
		[EMEDIUMTYPE]      =  "EMEDIUMTYPE",
		[ECANCELED]        =  "ECANCELED",
		[ENOKEY]           =  "ENOKEY",
		[EKEYEXPIRED]      =  "EKEYEXPIRED",
		[EKEYREVOKED]      =  "EKEYREVOKED",
		[EKEYREJECTED]     =  "EKEYREJECTED",
		[EOWNERDEAD]       =  "EOWNERDEAD",
		[ENOTRECOVERABLE]  =  "ENOTRECOVERABLE",
		[ERFKILL]          =  "ERFKILL",
		[EHWPOISON]        =  "EHWPOISON",
	};
	if (e < sizeof(estr)/sizeof(*estr) && estr[e]) {
		return estr[e];
	}
	return "UNKNOWN_ERRNO";
}


#define _GNU_SOURCE     /* To get defns of and NI_MAXHOST */
#include <arpa/inet.h>
#include <sys/socket.h>
#include <netdb.h>
#include <ifaddrs.h>
#include <unistd.h>
#include <linux/if_link.h>

int ovis_host_is_self(const char *remote)
{
	if (!remote) {
		return EINVAL;
	}

	/* dig up remote ip */
	struct addrinfo hi, *si, *p;
	struct sockaddr_in *h;
	int rv;

	memset(&hi, 0, sizeof hi);
	hi.ai_family = AF_UNSPEC;
	hi.ai_socktype = SOCK_STREAM;

	if ( (rv = getaddrinfo( remote , NULL , &hi , &si)) != 0) 
	{
		return 0;
	}

	char rhost[NI_MAXHOST];
	/* dig up local addrs */
	struct ifaddrs *ifaddr, *ifa;
	int family, n;

	if (getifaddrs(&ifaddr) == -1) {
		return 0;
	}

	int rc = 0;
	for(p = si; p != NULL; p = p->ai_next) 
	{
		h = (struct sockaddr_in *) p->ai_addr;
		strcpy(rhost , inet_ntoa( h->sin_addr ) );
		for (ifa = ifaddr, n = 0; ifa != NULL; ifa = ifa->ifa_next, n++) {
			if (ifa->ifa_addr == NULL) {
				continue;
			}
			family = ifa->ifa_addr->sa_family;
			if (family != AF_INET) {
				continue;
			}

			struct sockaddr_in *pAddr = (struct sockaddr_in *)ifa->ifa_addr;
			if (!strcmp(rhost, inet_ntoa(pAddr->sin_addr))) {
				rc = 1;
				break;
			}
		}
	}

	freeaddrinfo(si);
	freeifaddrs(ifaddr);
	return rc;
}

