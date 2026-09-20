/*
 * XZSFS VFS Operations and Registration
 *
 * Implements VFS operation table and dynamic registration via vfs_fsadd.
 */

#include "xzsfs.h"
#include <sys/mount_internal.h>
#include <sys/vnode_internal.h>
#include <sys/malloc.h>

uint32_t xzsfs_registration_count = 0;
static vfstable_t xzsfs_vfstable_handle = NULL;

static LCK_GRP_DECLARE(xzsfs_mtx_grp, "xzsfs-mutex");
lck_grp_t *xzsfs_lck_grp = &xzsfs_mtx_grp;

int
xzsfs_mount(struct mount *mp, vnode_t devvp, __unused user_addr_t data, __unused vfs_context_t context)
{
    struct xzsfs_mount *xmp = NULL;
    struct xzsfs_disk_object disk_objects[XZSFS_MAX_OBJECTS];
    char disk_strtab[XZSFS_MAX_STRTAB_SIZE];
    int error = 0;

    if (!mp || !devvp) {
        return EINVAL;
    }

    xmp = kalloc_type(struct xzsfs_mount, Z_WAITOK | Z_ZERO);
    if (!xmp) {
        return ENOMEM;
    }

    xmp->mp = mp;
    xmp->devvp = devvp;
    lck_mtx_init(&xmp->lock, xzsfs_lck_grp, LCK_ATTR_NULL);

    /* Read and validate superblock */
    error = xzsfs_core_read_superblock(xzsfs_kernel_block_read, devvp, &xmp->fs.sb);
    if (error != 0) {
        goto fail;
    }

    error = xzsfs_core_validate_superblock(&xmp->fs.sb);
    if (error != 0) {
        goto fail;
    }

    /* Load and validate metadata tables */
    error = xzsfs_core_load_metadata(xzsfs_kernel_block_read, devvp, &xmp->fs.sb,
                                     disk_objects, XZSFS_MAX_OBJECTS,
                                     disk_strtab, XZSFS_MAX_STRTAB_SIZE);
    if (error != 0) {
        goto fail;
    }

    error = xzsfs_core_validate_metadata(&xmp->fs.sb, disk_objects, disk_strtab);
    if (error != 0) {
        goto fail;
    }

    /* Initialize core filesystem */
    error = xzsfs_core_init_fs(&xmp->fs, &xmp->fs.sb, disk_objects, disk_strtab);
    if (error != 0) {
        goto fail;
    }

    /* Initialize in-memory nodes */
    for (uint32_t i = 0; i < xmp->fs.node_count; i++) {
        xmp->nodes[i].core = xmp->fs.nodes[i];
        xmp->nodes[i].vnode = NULL;
        xmp->nodes[i].xmp = xmp;
    }

    mp->mnt_data = (qaddr_t)xmp;
    mp->mnt_flag |= MNT_LOCAL | MNT_RDONLY;
    return 0;

fail:
    if (xmp) {
        lck_mtx_destroy(&xmp->lock, xzsfs_lck_grp);
        kfree_type(struct xzsfs_mount, xmp);
    }
    return error;
}

int
xzsfs_start(__unused struct mount *mp, __unused int flags, __unused vfs_context_t context)
{
    return 0;
}

int
xzsfs_unmount(struct mount *mp, int mntflags, __unused vfs_context_t context)
{
    struct xzsfs_mount *xmp = (struct xzsfs_mount *)mp->mnt_data;
    int flags = 0;

    if (!xmp) {
        return EINVAL;
    }

    if (mntflags & MNT_FORCE) {
        flags |= FORCECLOSE;
    }

    int error = vflush(mp, NULL, flags);
    if (error != 0) {
        return error;
    }

    lck_mtx_destroy(&xmp->lock, xzsfs_lck_grp);
    kfree_type(struct xzsfs_mount, xmp);
    mp->mnt_data = (qaddr_t)NULL;
    return 0;
}

int
xzsfs_root(struct mount *mp, struct vnode **vpp, __unused vfs_context_t context)
{
    struct xzsfs_mount *xmp = (struct xzsfs_mount *)mp->mnt_data;
    if (!xmp || xmp->fs.node_count == 0) {
        return EINVAL;
    }
    return xzsfs_get_vnode(mp, &xmp->nodes[0], vpp);
}

int
xzsfs_vfs_getattr(struct mount *mp, struct vfs_attr *vfa, __unused vfs_context_t context)
{
    struct xzsfs_mount *xmp = (struct xzsfs_mount *)mp->mnt_data;
    if (!xmp) {
        return EINVAL;
    }

    VFSATTR_RETURN(vfa, f_bsize, XZSFS_SECTOR_SIZE);
    VFSATTR_RETURN(vfa, f_iosize, XZSFS_SECTOR_SIZE);
    VFSATTR_RETURN(vfa, f_blocks, xmp->fs.sb.image_size_bytes / XZSFS_SECTOR_SIZE);
    VFSATTR_RETURN(vfa, f_bfree, 0);
    VFSATTR_RETURN(vfa, f_bavail, 0);
    VFSATTR_RETURN(vfa, f_bused, xmp->fs.sb.image_size_bytes / XZSFS_SECTOR_SIZE);
    VFSATTR_RETURN(vfa, f_files, xmp->fs.node_count);
    VFSATTR_RETURN(vfa, f_ffree, 0);

    return 0;
}

int
xzsfs_sync(__unused struct mount *mp, __unused int waitfor, __unused vfs_context_t context)
{
    return 0;
}

int
xzsfs_vget(struct mount *mp, ino64_t ino, struct vnode **vpp, __unused vfs_context_t context)
{
    struct xzsfs_mount *xmp = (struct xzsfs_mount *)mp->mnt_data;
    if (!xmp || ino == 0 || ino > xmp->fs.node_count) {
        return ENOENT;
    }
    return xzsfs_get_vnode(mp, &xmp->nodes[ino - 1U], vpp);
}

int
xzsfs_init(__unused struct vfsconf *vfsc)
{
    return 0;
}

struct vfsops xzsfs_vfsops = {
    .vfs_mount   = xzsfs_mount,
    .vfs_start   = xzsfs_start,
    .vfs_unmount = xzsfs_unmount,
    .vfs_root    = xzsfs_root,
    .vfs_getattr = xzsfs_vfs_getattr,
    .vfs_sync    = xzsfs_sync,
    .vfs_vget    = xzsfs_vget,
    .vfs_init    = xzsfs_init,
};

extern void xzs_early_puts(const char *s);

int
xzsfs_vfs_register(void)
{
    if (xzsfs_registration_count > 0) {
        return 0; /* Already registered */
    }

    static struct vnodeopv_desc *s_opvdescs[2];
    s_opvdescs[0] = (struct vnodeopv_desc *)(uintptr_t)&xzsfs_vnodeop_opv_desc;
    s_opvdescs[1] = NULL;

    static struct vfs_fsentry s_entry;
    bzero(&s_entry, sizeof(s_entry));
    s_entry.vfe_vfsops = &xzsfs_vfsops;
    s_entry.vfe_vopcnt = 1;
    s_entry.vfe_opvdescs = s_opvdescs;
    s_entry.vfe_fstypenum = 0;
    strlcpy(s_entry.vfe_fsname, XZSFS_NAME, sizeof(s_entry.vfe_fsname));
    s_entry.vfe_flags = VFS_TBLTHREADSAFE | VFS_TBLFSNODELOCK | VFS_TBLNOTYPENUM |
                        VFS_TBL64BITREADY | VFS_TBLLOCALVOL;

    int error = vfs_fsadd(&s_entry, &xzsfs_vfstable_handle);

    if (error == 0) {
        xzsfs_registration_count++;
    }

    return error;
}

/*
 * Phase D44: Exact Text-Layout Perturbation Control.
 *
 * Deterministic inert text contribution: exactly 661 NOP instructions (2644 bytes)
 * in __TEXT_EXEC,__text to reproduce the exact downstream symbol displacement
 * observed in the failing M4-linked binary, without introducing any M4 filesystem
 * semantics, mutable globals, static constructors, or runtime references.
 */
__attribute__((naked, noinline, used))
void
xzs_inert_layout_pad(void)
{
    __asm__ volatile (
        ".rept 661\n"
        "nop\n"
        ".endr\n"
    );
}
