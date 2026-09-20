/*
 * XZSFS (Xperia XZs File System) Kernel Driver Header
 *
 * Status: FROZEN D5-M3
 * Kernel: XNU Darwin 24 (Kryo / Sony Xperia XZs)
 */

#ifndef _XZSFS_H_
#define _XZSFS_H_

#include <sys/appleapiopts.h>
#include <sys/cdefs.h>
#include <sys/param.h>
#include <sys/types.h>
#include <sys/vnode.h>
#include <sys/mount.h>
#include <sys/uio.h>
#include <sys/buf.h>
#include <sys/dirent.h>
#include <kern/locks.h>

#include "xzsfs_core.h"

#define XZSFS_NAME              "xzsfs"

/* In-memory filesystem node structure */
struct xzsfs_node {
    struct xzsfs_core_node  core;           /* Parsed core node data */
    vnode_t                 vnode;          /* Pointer to active vnode (NULL if not active) */
    struct xzsfs_mount     *xmp;            /* Pointer back to mount */
};

/* In-memory mount structure */
struct xzsfs_mount {
    mount_t                 mp;             /* Backing VFS mount */
    vnode_t                 devvp;          /* Backing block device vnode (e.g. md0) */
    struct xzsfs_core_fs    fs;             /* Core parsed filesystem */
    struct xzsfs_node       nodes[XZSFS_MAX_OBJECTS];
    lck_mtx_t               lock;           /* Mutex protecting mount and node lists */
};

/* Instrumentation counters for D5-M3 and D5-M4 proof */
extern uint32_t xzsfs_get_vnode_call_count;
extern uint32_t vnode_create_call_count_from_xzsfs;
extern uint32_t xzsfs_root_vnode_create_count;
extern uint32_t xzsfs_root_vnode_reclaim_count;
extern uint32_t xzsfs_registration_count;

/* Lock group */
extern lck_grp_t *xzsfs_lck_grp;

/* Vnode operations vector declaration */
extern int (**xzsfs_vnodeop_p)(void *);
extern const struct vnodeopv_desc xzsfs_vnodeop_opv_desc;

/* VFS operations */
extern struct vfsops xzsfs_vfsops;

/* VFS interface routines */
int xzsfs_mount(struct mount *mp, vnode_t devvp, user_addr_t data, vfs_context_t context);
int xzsfs_start(struct mount *mp, int flags, vfs_context_t context);
int xzsfs_unmount(struct mount *mp, int mntflags, vfs_context_t context);
int xzsfs_root(struct mount *mp, struct vnode **vpp, vfs_context_t context);
int xzsfs_vfs_getattr(struct mount *mp, struct vfs_attr *vfa, vfs_context_t context);
int xzsfs_sync(struct mount *mp, int waitfor, vfs_context_t context);
int xzsfs_vget(struct mount *mp, ino64_t ino, struct vnode **vpp, vfs_context_t context);
int xzsfs_init(struct vfsconf *vfsc);

/* Filesystem registration */
int xzsfs_vfs_register(void);

/* Vnode lifecycle routines */
int xzsfs_get_vnode(mount_t mp, struct xzsfs_node *node, vnode_t *vpp);
int xzsfs_node_reclaim(struct xzsfs_node *node, vnode_t vp);

/* Read-only error stub */
int xzsfs_rofs_err(void *ap);

/* Kernel block I/O adapter */
int xzsfs_kernel_block_read(void *ctx, uint64_t lba, uint32_t count, void *buf);

/* Core helper formatting routines (for verification in D5-M3) */
int xzsfs_helper_format_dirent(const struct xzsfs_core_node *node, const char *name, uint8_t dtype, struct dirent *de);
int xzsfs_helper_format_getattr(const struct xzsfs_core_node *node, struct vnode_attr *vap);

/* D5-M3 Silicon Diagnostic Probe */
int xzsfs_d5m3_probe(dev_t rootdev);

/* D5-M4 Silicon Diagnostic Probe */
int xzsfs_d5m4_probe(dev_t rootdev);

#endif /* _XZSFS_H_ */
