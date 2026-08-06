/* SPDX-License-Identifier: GPL-2.0 */
#ifndef SUNXI_RPROC_TRACE_H
#define SUNXI_RPROC_TRACE_H

#include <linux/compiler.h>
#include <linux/types.h>
#include <linux/uaccess.h>

struct dentry;
struct rproc;
struct rproc_debug_trace;

ssize_t sunxi_rproc_trace_read(void *from, int buf_len, char *to, size_t count);
ssize_t sunxi_rproc_trace_read_to_user(void *from, int buf_len, char __user *userbuf,
				       size_t count, loff_t *ppos);
int sunxi_rproc_trace_dump(void *trace_mem, int trace_mem_len);
struct dentry *sunxi_rproc_create_aw_trace_file(const char *name, struct rproc *rproc,
						struct rproc_debug_trace *trace);
void sunxi_rproc_remove_aw_trace_file(struct dentry *tfile);

#endif /* SUNXI_RPROC_TRACE_H */
