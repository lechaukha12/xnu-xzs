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

extern void xzs_breadcrumb(uint32_t cp, uint32_t err);
extern void xzs_early_puts(const char *s);

int
xzsfs_mount(struct mount *mp, vnode_t devvp, __unused user_addr_t data, __unused vfs_context_t context)
{
    struct xzsfs_mount *xmp = NULL;
    struct xzsfs_disk_object disk_objects[XZSFS_MAX_OBJECTS];
    char disk_strtab[XZSFS_MAX_STRTAB_SIZE];
    vnode_t root_vp = NULL;
    boolean_t devvp_ref_held = FALSE;
    boolean_t root_ref_held = FALSE;
    int error = 0;

    /* Checkpoint 0x20: xzsfs mount dispatch enter */
    xzs_breadcrumb(0xD530, 0x20);
    xzs_early_puts("[XZSFS] xzsfs_mount entered\n");

    if (!mp || !devvp) {
        return EINVAL;
    }

    xmp = kalloc_type(struct xzsfs_mount, Z_WAITOK | Z_ZERO);
    if (!xmp) {
        return ENOMEM;
    }

    /* Checkpoint 0x21: mount-private allocation pass */
    xzs_breadcrumb(0xD530, 0x21);
    xzs_early_puts("[XZSFS] mount-private allocated\n");

    xmp->mp = mp;
    xmp->devvp = devvp;
    lck_mtx_init(&xmp->lock, xzsfs_lck_grp, LCK_ATTR_NULL);

    /* Retain devvp reference */
    error = vnode_ref(devvp);
    if (error != 0) {
        goto fail;
    }
    devvp_ref_held = TRUE;

    /* Checkpoint 0x22: devvp reference acquired */
    xzs_breadcrumb(0xD530, 0x22);
    xzs_early_puts("[XZSFS] devvp reference acquired\n");

    /* Read and validate superblock */
    error = xzsfs_core_read_superblock(xzsfs_kernel_block_read, devvp, &xmp->fs.sb);
    if (error != 0) {
        goto fail;
    }

    error = xzsfs_core_validate_superblock(&xmp->fs.sb);
    if (error != 0) {
        goto fail;
    }

    /* Checkpoint 0x30: superblock validation pass */
    xzs_breadcrumb(0xD530, 0x30);
    xzs_early_puts("[XZSFS] superblock validated\n");

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

    /* Checkpoint 0x31: metadata CRC pass */
    xzs_breadcrumb(0xD530, 0x31);
    xzs_early_puts("[XZSFS] metadata CRC pass\n");

    /* Initialize core filesystem */
    error = xzsfs_core_init_fs(&xmp->fs, &xmp->fs.sb, disk_objects, disk_strtab);
    if (error != 0) {
        goto fail;
    }

    /* Checkpoint 0x32: object graph validation pass */
    xzs_breadcrumb(0xD530, 0x32);
    xzs_early_puts("[XZSFS] object graph validation pass\n");

    /* Initialize in-memory nodes */
    for (uint32_t i = 0; i < xmp->fs.node_count; i++) {
        xmp->nodes[i].core = xmp->fs.nodes[i];
        xmp->nodes[i].vnode = NULL;
        xmp->nodes[i].xmp = xmp;
    }

    /* Checkpoint 0x40: root xzsfs_node resolved */
    xzs_breadcrumb(0xD530, 0x40);
    xzs_early_puts("[XZSFS] root xzsfs_node resolved\n");

    /* Checkpoint 0x50: root vnode create enter */
    xzs_breadcrumb(0xD530, 0x50);
    xzs_early_puts("[XZSFS] root vnode create enter\n");

    error = xzsfs_get_vnode(mp, &xmp->nodes[0], &root_vp);
    if (error != 0 || root_vp == NULL) {
        goto fail;
    }

    /* Checkpoint 0x51: root vnode create pass */
    xzs_breadcrumb(0xD530, 0x51);
    xzs_early_puts("[XZSFS] root vnode created\n");

    /* Hold long-term usecount on root vnode and release creation iocount */
    vnode_ref(root_vp);
    root_ref_held = TRUE;
    vnode_put(root_vp);

    vfs_setfsprivate(mp, xmp);
    vfs_getnewfsid(mp);
    mp->mnt_flag |= MNT_LOCAL | MNT_RDONLY;
    strlcpy(mp->mnt_vfsstat.f_mntfromname, "md0", sizeof(mp->mnt_vfsstat.f_mntfromname));

    return 0;

fail:
    if (root_ref_held && root_vp) {
        vnode_rele(root_vp);
    }
    if (xmp && xmp->nodes[0].vnode) {
        if (root_vp && !root_ref_held) {
            vnode_put(root_vp);
        }
    }
    if (devvp_ref_held) {
        vnode_rele(devvp);
    }
    if (xmp) {
        lck_mtx_destroy(&xmp->lock, xzsfs_lck_grp);
        kfree_type(struct xzsfs_mount, xmp);
    }
    vfs_setfsprivate(mp, NULL);
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
    struct xzsfs_mount *xmp = (struct xzsfs_mount *)vfs_fsprivate(mp);
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

    if (xmp->nodes[0].vnode) {
        vnode_rele(xmp->nodes[0].vnode);
    }
    if (xmp->devvp) {
        vnode_rele(xmp->devvp);
    }

    lck_mtx_destroy(&xmp->lock, xzsfs_lck_grp);
    kfree_type(struct xzsfs_mount, xmp);
    vfs_setfsprivate(mp, NULL);
    return 0;
}

int
xzsfs_root(struct mount *mp, struct vnode **vpp, __unused vfs_context_t context)
{
    struct xzsfs_mount *xmp = (struct xzsfs_mount *)vfs_fsprivate(mp);
    if (!xmp || xmp->fs.node_count == 0) {
        return EINVAL;
    }
    return xzsfs_get_vnode(mp, &xmp->nodes[0], vpp);
}

int
xzsfs_vfs_getattr(struct mount *mp, struct vfs_attr *vfa, __unused vfs_context_t context)
{
    struct xzsfs_mount *xmp = (struct xzsfs_mount *)vfs_fsprivate(mp);
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
    struct xzsfs_mount *xmp = (struct xzsfs_mount *)vfs_fsprivate(mp);
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
                        VFS_TBL64BITREADY | VFS_TBLLOCALVOL | VFS_TBLCANMOUNTROOT;

    int error = vfs_fsadd(&s_entry, &xzsfs_vfstable_handle);

    if (error == 0) {
        xzsfs_registration_count++;
    }

    return error;
}
