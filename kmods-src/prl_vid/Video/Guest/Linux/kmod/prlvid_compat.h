/*
 * Copyright (C) 1999-2018 Parallels International GmbH. All Rights Reserved.
 */

#ifndef __PRLTG_DRM_COMPAT_H__
#define __PRLTG_DRM_COMPAT_H__

// LINUX_VERSION_CODE >= KERNEL_VERSION(4, 5, 0)
#include <linux/pfn_t.h>

#if (PRL_DRM_UNIVERSAL_PLANE_INIT_X == 1)
	#define drm_universal_plane_init_X(a0, a1, a2, a3, a4, a5, a6) \
		drm_universal_plane_init((a0), (a1), (a2), (a3), (a4), (a5), NULL, (a6), NULL)
#elif (PRL_DRM_UNIVERSAL_PLANE_INIT_X == 2)
	#define drm_universal_plane_init_X(a0, a1, a2, a3, a4, a5, a6) \
		drm_universal_plane_init((a0), (a1), (a2), (a3), (a4), (a5), (a6), NULL)
#else
	#define drm_universal_plane_init_X(a0, a1, a2, a3, a4, a5, a6) \
		drm_universal_plane_init((a0), (a1), (a2), (a3), (a4), (a5) /*(a6),*/)
#endif

#if (PRL_DRM_CONNECTOR_ATTACH_ENCODER_X == 1)
	#define drm_connector_attach_encoder_X(a0, a1) \
		drm_connector_attach_encoder((a0), (a1))
	#define drm_connector_list_update_X(a0) \
		drm_connector_list_update((a0))
#else
	#define drm_connector_attach_encoder_X(a0, a1) \
		drm_mode_connector_attach_encoder((a0), (a1))
	#define drm_connector_list_update_X(a0) \
		drm_mode_connector_list_update((a0))
#endif

#if (PRL_DRM_ATOMIC_HELPER_CRTC_DESTROY_STATE_X == 1)
	#define __drm_atomic_helper_crtc_destroy_state_X(a0, a1) \
		__drm_atomic_helper_crtc_destroy_state(/*(a0),*/ (a1))
#else
	#define __drm_atomic_helper_crtc_destroy_state_X(a0, a1) \
		__drm_atomic_helper_crtc_destroy_state((a0), (a1))
#endif

#if (PRL_DRM_HELPER_MODE_FILL_FB_STRUCT_X == 1)
	#define drm_helper_mode_fill_fb_struct_X(a0, a1, a2) \
		drm_helper_mode_fill_fb_struct((a0), (a1), (a2))
#elif (PRL_DRM_HELPER_MODE_FILL_FB_STRUCT_X == 2)
	#include <drm/drm_modeset_helper.h>
	#define drm_helper_mode_fill_fb_struct_X(a0, a1, a2) \
		drm_helper_mode_fill_fb_struct(/*(a0),*/ (a1), (a2))
#else
	#include <drm/drm_crtc_helper.h>
	#define drm_helper_mode_fill_fb_struct_X(a0, a1, a2) \
		drm_helper_mode_fill_fb_struct(/*(a0),*/ (a1), (a2))
#endif

#if (PRL_DRM_GEM_OBJECT_PUT_UNLOCKED_X == 1)
	#define drm_gem_object_put_unlocked_X(a0) \
		drm_gem_object_put_unlocked((a0))
#else
	#define drm_gem_object_put_unlocked_X(a0) \
		drm_gem_object_unreference_unlocked((a0))
#endif

#if (PRL_DRM_IS_CURRENT_MASTER_X == 1)
	#include <drm/drm_auth.h>
	#define drm_is_current_master_X(a0) \
		drm_is_current_master((a0))
#else
	#define drm_is_current_master_X(a0) \
		drm_is_primary_client((a0))
#endif

#if (PRL_DRM_MINOR_PRIMARY_X == 1)
	#define DRM_MINOR_PRIMARY_X DRM_MINOR_PRIMARY
#else
	#define DRM_MINOR_PRIMARY_X DRM_MINOR_LEGACY
#endif

#if (PRL_DRM_KREF_READ_X == 1)
	#include <linux/kref.h>
#else
	#include <linux/atomic.h>
	#define kref_read(kr) \
		atomic_read(&(kr)->refcount)
#endif

#if (PRL_DRM_ATOMIC_HELPER_BEST_ENCODER_X == 1)
	#define drm_atomic_helper_best_encoder_X \
		drm_atomic_helper_best_encoder
#else
	#define drm_atomic_helper_best_encoder_X \
		(NULL)
#endif

#if (PRL_DRM_DEV_PUT_X == 1)
	#define drm_dev_put_X \
		drm_dev_unref
#else
	#define drm_dev_put_X \
		drm_dev_put
#endif

#if (PRL_DRM_FB_HOT_XY == 1)
	#define prl_drm_fb_hot_x(fb) (fb)->hot_x
	#define prl_drm_fb_hot_y(fb) (fb)->hot_y
#else
	#define prl_drm_fb_hot_x(fb) 0
	#define prl_drm_fb_hot_y(fb) 0
#endif

#if (PRL_DRM_FB_HELPER_INIT_X == 1)
	#define prl_drm_fb_helper_init_X(dev, helper, heads) \
		drm_fb_helper_init(dev, helper, heads)
#else
	#define prl_drm_fb_helper_init_X(dev, helper, heads) \
		drm_fb_helper_init(dev, helper, (dev)->mode_config.num_crtc, heads)
#endif

#if (PRL_DRM_FB_HELPER_FILL_INFO_X == 1)
	#define prl_drm_fb_helper_fill_info_X drm_fb_helper_fill_info
#elif (PRL_DRM_FB_HELPER_FILL_INFO_X == 2)
	#define prl_drm_fb_helper_fill_info_X(info, helper, sizes) \
		drm_fb_helper_fill_fix(info, helper->fb->pitches[0], helper->fb->format->depth); \
		drm_fb_helper_fill_var(info, helper, sizes->fb_width, sizes->fb_height)
#else
	#define prl_drm_fb_helper_fill_info_X(info, helper, sizes) \
		drm_fb_helper_fill_fix(info, helper->fb->pitches[0], helper->fb->depth); \
		drm_fb_helper_fill_var(info, helper, sizes->fb_width, sizes->fb_height)
#endif

#if (PRL_DRM_VM_OPERATIONS_FAULT_X == 1)
	#define PRL_VM_FAULT_T	vm_fault_t
#else
	#define PRL_VM_FAULT_T	int
#endif

#if (PRL_DRM_ATOMIC_HELPER_SHUTDOWN_X == 1)
	#define prl_drm_atomic_helper_shutdown_X(dev)	drm_atomic_helper_shutdown(dev)
#elif (PRL_DRM_ATOMIC_HELPER_SHUTDOWN_X == 2)
	#define prl_drm_atomic_helper_shutdown_X(dev)	drm_crtc_force_disable_all(dev)
#else
	#define prl_drm_atomic_helper_shutdown_X(dev)	((void)(dev))
#endif

#endif
