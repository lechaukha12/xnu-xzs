/*
 * XZSFS Node Lifecycle Management
 *
 * Prepared for D5-M4 VFS mount/vnode operations.
 * Instrumenting call counters to guarantee zero execution during D5-M3.
 */

#include "xzsfs.h"
#include <sys/vnode_internal.h>

uint32_t xzsfs_get_vnode_call_count = 0;
uint32_t vnode_create_call_count_from_xzsfs = 0;
uint32_t xzsfs_root_vnode_create_count = 0;
uint32_t xzsfs_root_vnode_reclaim_count = 0;

int
xzsfs_get_vnode(mount_t mp, struct xzsfs_node *node, vnode_t *vpp)
{
    int error = 0;
    struct vnode_fsparam vnfs_param;

    xzsfs_get_vnode_call_count++;

    if (!mp || !node || !vpp) {
        return EINVAL;
    }

    lck_mtx_lock(&node->xmp->lock);

    if (node->vnode != NULL) {
        error = vnode_get(node->vnode);
        if (error == 0) {
            *vpp = node->vnode;
            lck_mtx_unlock(&node->xmp->lock);
            return 0;
        }
        node->vnode = NULL;
    }

    /* Set up vnode creation parameters */
    bzero(&vnfs_param, sizeof(vnfs_param));
    vnfs_param.vnfs_mp = mp;
    vnfs_param.vnfs_vtype = (node->core.type == XZSFS_TYPE_DIR) ? VDIR : VREG;
    vnfs_param.vnfs_str = XZSFS_NAME;
    vnfs_param.vnfs_dvp = NULL;
    vnfs_param.vnfs_fsnode = node;
    vnfs_param.vnfs_vops = xzsfs_vnodeop_p;
    vnfs_param.vnfs_markroot = (node->core.object_id == 1U);
    vnfs_param.vnfs_marksystem = 0;
    vnfs_param.vnfs_rdev = 0;
    vnfs_param.vnfs_filesize = (off_t)node->core.data_length;
    vnfs_param.vnfs_cnp = NULL;
    vnfs_param.vnfs_flags = VNFS_CANTCACHE | VNFS_NOCACHE;

    vnode_create_call_count_from_xzsfs++;
    error = vnode_create(VNCREATE_FLAVOR, VCREATESIZE, &vnfs_param, &node->vnode);
    if (error == 0) {
        if (node->core.object_id == 1U) {
            xzsfs_root_vnode_create_count++;
        }
        *vpp = node->vnode;
    }

    lck_mtx_unlock(&node->xmp->lock);
    return error;
}

int
xzsfs_node_reclaim(struct xzsfs_node *node, vnode_t vp)
{
    if (!node || !vp) {
        return EINVAL;
    }

    lck_mtx_lock(&node->xmp->lock);
    if (node->core.object_id == 1U) {
        xzsfs_root_vnode_reclaim_count++;
    }
    node->vnode = NULL;
    vnode_clearfsnode(vp);
    lck_mtx_unlock(&node->xmp->lock);

    return 0;
}
