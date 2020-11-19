/*
 * Copyright (C) 1999-2019 Parallels International GmbH. All Rights Reserved.
 */

#if PRL_DRM_ENABLED
#include <drm/drmP.h>
#include <drm/drm_crtc.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_fb_helper.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_gem.h>
#include <linux/screen_info.h>
#include "prltg_call.h"
#include "prlvid_compat.h"
#include "prlvid_common.h"

#define PFX_FMT "## [%d:%d] "
#define PFX_ARG task_tgid_nr(current), task_pid_nr(current)
#define MIN(a,b) ((a) < (b) ? (a) : (b))

// Print vma:
#define VMA_FMT "VMA(addr:%lx size:%ld flags:%lx)"
#define VMA_ARG(vma) ((vma) ? (vma)->vm_start : 0), \
	((vma) ? (vma)->vm_end - (vma)->vm_start : 0), \
	((vma) ? vma->vm_flags : 0)
// Print crtc:
#define CR_FMT "CRTC-%d:<%s>"
#define CR_ARG(cr) ((cr) ? (cr)->base.id : 0), ((cr) &&  (cr)->name ? (cr)->name : "???")
// Print plan:
#define PL_FMT "PLAN-%d:<%s>"
#define PL_ARG(pl) ((pl) ? (pl)->base.id : 0), ((pl) && (pl)->name ? (pl)->name : "???")
// Print prl gem object:
#define GO_FMT "GEM-%d:%d(si:%d sz:%d dma:%p ref:%u)"
#define GO_ARG(go) \
	((go) ? ((struct prl_gem_object*)go)->global_index : 0), \
	((go) ? ((struct prl_gem_object*)go)->handle : 0), \
	((go) ? ((struct prl_gem_object*)go)->shared_index : 0), \
	((go) ? (int)((struct prl_gem_object*)go)->base.size : 0), \
	((go) ? ((struct prl_gem_object*)go)->base.dma_buf : 0), \
	((go) ? kref_read(&((struct prl_gem_object*)go)->base.refcount) : 0)
// Print prl framebuffer:
#define FB_FMT "FB-%d:%p"
#define FB_ARG(fb) ((fb) ? ((struct prl_framebuffer*)fb)->base.base.id : 0), (fb)
// Print connector:
#define CON_FMT "CON:<%s>:%s"
#define CON_ARG(con) \
	((con && (con)->name) ? (con)->name : "???"), \
	((con) ? ((con)->status == connector_status_connected ? "YES" : "NO") : "???")
// Print property:
#define PROP_FMT "PROP:<%s>:%d"
#define PROP_ARG(prop) \
	((prop && (prop)->name) ? (prop)->name : "???"), ((prop) ? (prop)->num_values : 0 )
// Print prl framebuffer & gem object:
#define FB_GO_FMT "FB-%d:%p[GEM-%d:%d(si:%d sz:%d dma:%p ref:%u)]"
#define FB_GO_ARG(fb) \
	((fb) ? ((struct prl_framebuffer*)fb)->base.base.id : 0), \
	(fb), \
	((fb) && ((struct prl_framebuffer*)fb)->prl_go ? ((struct prl_framebuffer*)fb)->prl_go->global_index : 0), \
	((fb) && ((struct prl_framebuffer*)fb)->prl_go ? ((struct prl_framebuffer*)fb)->prl_go->handle : 0), \
	((fb) && ((struct prl_framebuffer*)fb)->prl_go ? ((struct prl_framebuffer*)fb)->prl_go->shared_index : 0), \
	((fb) && ((struct prl_framebuffer*)fb)->prl_go ? (int)((struct prl_framebuffer*)fb)->prl_go->base.size : 0), \
	((fb) && ((struct prl_framebuffer*)fb)->prl_go ? ((struct prl_framebuffer*)fb)->prl_go->base.dma_buf : 0), \
	((fb) && ((struct prl_framebuffer*)fb)->prl_go ?  kref_read(&((struct prl_framebuffer*)fb)->prl_go->base.refcount) : 0)
// Print plan:
#define PL_FB_FMT "PLAN-%d:<%s>(FB-%d:%p)"
#define PL_FB_ARG(pl) ((pl) ? (pl)->base.id : 0), \
	((pl) && (pl)->name ? (pl)->name : "???"), \
	((pl) && (pl)->fb ? ((struct prl_framebuffer*)((pl)->fb))->base.base.id : 0),\
	((pl) ? (pl)->fb : 0)
// Print image:
#define IMG_FMT "IMG-%u(hndl:%u w:%u h:%u)"
#define IMG_ARG(desc) (desc)->pbuffer, (desc)->handle, (desc)->width, (desc)->height

#define PRLDRMFB "prldrmfb"

#ifndef PRL_VIRTUAL_HEADS_MAX_COUNT
#define PRL_VIRTUAL_HEADS_MAX_COUNT 16
#endif

#define PRL_VIRTUAL_HEAD_BUFFERS_MAX	128
#define PRL_VIRTUAL_HEAD_BUFFER_SIZE	(16*1024*1024)

// If dumb buffer size <= 64k - it will be used for cursor and we
// will alloc it in system memory only (not video or aperture).
#define PRL_DRM_CURSOR_BUF_MAX_SIZE (64*1024)

// End-to-end numbering for all gem objects
static int prl_gem_object_global_index = 1;
// All shared video memory splitted into buffers of equal size
// (prl_shared_buf_size == 1440 * 900 * 4 + <width/height 64 byte + page alignment>)
// This mask show which one is available:
#define PRL_SHARED_BUF_AVAIL_MASK_NUM	((PRL_VIRTUAL_HEAD_BUFFERS_MAX + __BITS_PER_LONG - 1) / __BITS_PER_LONG)
static unsigned long prl_shared_buf_avail_mask[PRL_SHARED_BUF_AVAIL_MASK_NUM];
static uint64_t prl_shared_buf_size = 0;

#define PRLVID_APERTURE_BASE 0x80000000ull
struct prl_drm_aperture_entry {
	uint64_t base;
	uint64_t size;
	struct list_head list;
};
DEFINE_SPINLOCK(prl_drm_aperture_lock);
static struct list_head prl_drm_aperture_list;

// This data will be shared between host and guest.
// Must be aligned and allocated on paged boundary.
struct prl_drm_shared_data {
	VID_TG_UPDATE_BOUNDS		update_bounds[PRL_VIRTUAL_HEADS_MAX_COUNT];
	VID_TG_MOUSE_POSITION		mouse_position;
};

struct prl_drm_shared_state {
	// Tg request
	TG_REQUEST					req;
	TG_BUFFER					bounds;
	TG_BUFFER					mousepos;
	TG_REQ_DESC					sdesc;
	// Tg pending request pointer
	struct TG_PENDING_REQUEST	*pending;
	// Shared data
	struct prl_drm_shared_data	*data;
};

struct prl_gem_object {
	struct drm_gem_object		base;
	struct mutex				lock;
	//
	int							handle;
	int							global_index;
	int							shared_index; // index [1..64]
	//
	void						*uptr;	// uspace ptr
	void						*kptr;	// kspace ptr
	uint64_t					aperture_addr;
};

struct prl_framebuffer {
	struct drm_framebuffer		base;
	struct prl_gem_object		*prl_go;
};

struct prl_drm_head {
	struct drm_crtc				crtc;
	struct drm_encoder			encoder;
	struct drm_connector		connector;
	struct drm_plane			primary;
	struct drm_plane			cursor;
	// Prefered mode (aka dynamic resolution mode)
	VID_TG_MODE64				mode_pref;
	// Current mode (aka last set mode)
	VID_TG_MODE64				mode_cur;
	//
	int							index;
};

struct prl_drm_file {
	struct prl_drm_device		*prl_dev;
	struct vtg_filp_private		*vtg_file;
	bool						use_shared_state;
};

struct prl_drm_fbdev {
	struct drm_fb_helper		base;
	struct prl_framebuffer		prl_fb;
};

struct prl_drm_device {
	struct drm_device			*dev;
	struct tg_dev				*tg_dev;
	struct drm_property			*hotplug_mode_update_property;
	// Not yet binded images
	struct mutex				img_lock;
	struct list_head			img_list;
	//
	struct prl_drm_fbdev		*fbdev;
	//
	struct prl_drm_head			heads[PRL_VIRTUAL_HEADS_MAX_COUNT];
	unsigned int				heads_max;
	unsigned int				heads_connected;
	//
	unsigned long				alloc_size;
	// Tg shared bounds and mouse position
	struct prl_drm_shared_state shared_state;
};

#define PRL_DRM_IMAGE_MAX_OWNER	8
struct prl_drm_image
{
	struct image_desc			desc;
	struct prl_gem_object		*prl_go;
	struct list_head			img_list;
	// Number of owners is equial to refcount
	unsigned					num_owners;
	struct drm_file				*owners[PRL_DRM_IMAGE_MAX_OWNER];
};

static int cursormove = 1;
module_param(cursormove, int, 0);
MODULE_PARM_DESC(cursormove, "Support cursor move ioctl for DRM/KMS.");

#ifdef CONFIG_FRAMEBUFFER_CONSOLE
static int prldrmfb = 1;
#else
static int prldrmfb = 0;
#endif
module_param(prldrmfb, int, 0);
MODULE_PARM_DESC(prldrmfb, "Support framebuffer device for DRM/KMS.");

static uint32_t get_vga_state(void)
{
	outb(0xae, VGA_SEQ_I);
	return inl(VGA_SEQ_D);
}

static void set_vga_state(uint32_t val)
{
	outb(0xae, VGA_SEQ_I);
	outl(val, VGA_SEQ_D);
}

// KMS:
static int prl_drm_share_state_start(struct prl_drm_device *prl_dev, struct prl_drm_shared_state *shared_state)
{
	shared_state->req.Request = TG_REQUEST_MM_SHARE_STATE;
	shared_state->req.Status = TG_STATUS_PENDING;
	shared_state->req.BufferCount = 2;
	shared_state->req.Reserved = 0;

	shared_state->bounds.u.Buffer = shared_state->data->update_bounds;
	shared_state->bounds.ByteCount = sizeof(VID_TG_UPDATE_BOUNDS[PRL_VIRTUAL_HEADS_MAX_COUNT]);
	shared_state->bounds.Writable = 1;
	prltg_buf_set_kernelspace(&shared_state->sdesc, 0);

	shared_state->mousepos.u.Buffer = &shared_state->data->mouse_position;
	shared_state->mousepos.ByteCount = sizeof(VID_TG_MOUSE_POSITION);
	shared_state->mousepos.Writable = 1;
	prltg_buf_set_kernelspace(&shared_state->sdesc, 1);

	shared_state->sdesc.src = &shared_state->req;
	shared_state->sdesc.idata = 0;
	shared_state->sdesc.sbuf = &shared_state->bounds;
	shared_state->sdesc.flags = TG_REQ_RESTART_ON_SUCCESS;

	shared_state->pending = call_tg_async_start(prl_dev->tg_dev, &shared_state->sdesc);

	DRM_DEBUG_DRIVER(PFX_FMT "---- SHARE_STATE START(req:%p) ----", PFX_ARG, shared_state->pending);
	return 0;
}

static void prl_drm_share_state_stop(struct prl_drm_device *prl_dev, struct prl_drm_shared_state *shared_state)
{
	DRM_DEBUG_DRIVER(PFX_FMT "---- SHARE_STATE STOP(req:%p) ----", PFX_ARG, shared_state->pending);

	call_tg_async_cancel(shared_state->pending);
	shared_state->pending = NULL;
}

static int prl_drm_query_heads(struct prl_drm_device *prl_dev, unsigned *heads_max, unsigned *heads_connected)
{
	struct {
		TG_REQUEST req;
		VID_TG_QUERY_HEADS query;
	} src = {{
			.Request = TG_REQUEST_MM_QUERY_HEADS,
			.Status	= TG_STATUS_PENDING,
			.InlineByteCount = sizeof(VID_TG_QUERY_HEADS),
			.BufferCount = 0,
			.Reserved = 0
		},
		{
			.heads = 0,
			.connected = 0
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.query,
		.sbuf = 0,
		.flags = 0
	};

	int ret;

	DRM_DEBUG_DRIVER(PFX_FMT "---- QUERY HEADS ----", PFX_ARG);
	ret = call_tg_sync(prl_dev->tg_dev, &sdesc);

	if (!ret) {
		if (heads_max)
			*heads_max = src.query.heads;

		if (heads_connected)
			*heads_connected = src.query.connected;
	}

	return ret;
}

static int prl_drm_enable_heads(struct prl_drm_device *prl_dev, struct prl_drm_head *prl_head, bool enable)
{
	struct {
		TG_REQUEST req;
		VID_TG_ENABLE_HEAD head;
	} src = {{
			.Request = enable ? TG_REQUEST_MM_ENABLE_HEAD : TG_REQUEST_MM_DISABLE_HEAD,
			.Status	= TG_STATUS_PENDING,
			.InlineByteCount = sizeof(VID_TG_ENABLE_HEAD),
			.BufferCount = 0,
			.Reserved = 0
		},
		{
			.head = prl_head->index,
			.reserved = 0
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.head,
		.sbuf = 0,
		.flags = 0
	};

	// Clear current mode if head disabled
	if (!enable)
		memset(&prl_head->mode_cur, 0, sizeof(prl_head->mode_cur));

	DRM_DEBUG_DRIVER(PFX_FMT "---- HEAD-%d: %sABLE ----", PFX_ARG, prl_head->index, enable ? "EN" : "DIS");
	return call_tg_sync(prl_dev->tg_dev, &sdesc);
}

static int prl_drm_display_set_mode(struct prl_drm_device *prl_dev,
	struct prl_drm_head *prl_head, unsigned short width, unsigned short height, unsigned short bpp,
	unsigned long stride, unsigned long long offset, bool reset)
{
	struct {
		TG_REQUEST req;
		VID_TG_MODE64 mode;
	} src = {{
			.Request = TG_REQUEST_MM_SET_MODE,
			.Status	= TG_STATUS_PENDING,
			.InlineByteCount = sizeof(VID_TG_MODE64),
			.BufferCount = 0,
			.Reserved = 0
		},
		{
			.head = prl_head->index,
			.bpp = bpp,
			.width = width,
			.height = height,
			.stride = stride,
			.refresh = 60, //VESA_FREQUENCY_DEFAULT,
			.flags = 1, // clear
			.dpi = 0,
			.offset32 = (unsigned)offset,
			.x = 0,
			.y = 0,
			.offset64 = offset
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.mode,
		.sbuf = 0,
		.flags = 0
	};

	int ret;

	if (!reset
			&& prl_head->mode_cur.width == width
			&& prl_head->mode_cur.height == height
			&& prl_head->mode_cur.stride == stride
			&& prl_head->mode_cur.offset64 == offset)
		return 0;

	DRM_DEBUG_DRIVER(PFX_FMT "---- HEAD-%d: SET_MODE(width:%d height:%d off:%llx) ---",
		PFX_ARG, prl_head->index, width, height, offset);

	ret = call_tg_sync(prl_dev->tg_dev, &sdesc);
	if (!ret) {
		prl_head->mode_cur.width = width;
		prl_head->mode_cur.height = height;
		prl_head->mode_cur.stride = stride;
		prl_head->mode_cur.offset64 = offset;
	}
	return ret;
}

static int prl_drm_display_set_offset(struct prl_drm_device *prl_dev, int head, unsigned long long offset)
{
	struct {
		TG_REQUEST req;
		VID_TG_OFFSET offset;
	} src = {{
			.Request = TG_REQUEST_MM_SET_OFFSET,
			.Status	= TG_STATUS_PENDING,
			.InlineByteCount = sizeof(VID_TG_OFFSET),
			.BufferCount = 0,
			.Reserved = 0
		},
		{
			.head = head,
			.reserved = 0,
			.offset = (unsigned)offset,
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.offset,
		.sbuf = 0,
		.flags = 0
	};

	DRM_DEBUG_DRIVER(PFX_FMT "---- HEAD-%d: SET_OFFSET(off:%llx) ----", PFX_ARG, head, offset);
	return call_tg_sync(prl_dev->tg_dev, &sdesc);
}

static int prl_drm_mouse_set_pointer(struct prl_drm_device *prl_dev, int width, int height,
	int stride, int hotx, int hoty, void *data)
{
	struct {
		TG_REQUEST req;
		VID_TG_SET_MOUSE_POINTER mptr;
		TG_BUFFER bits;
	} src = {{
			.Request = TG_REQUEST_MOUSE_SET_POINTER,
			.Status	= TG_STATUS_SUCCESS,
			.InlineByteCount = sizeof(VID_TG_SET_MOUSE_POINTER),
			.BufferCount = 1,
			.Reserved = 0
		},
		{
			.x = 0,
			.y = 0,
			.hotx = hotx,
			.hoty = hoty,
			.width = width,
			.height = height,
			.stride = stride
		},
		{
			.u.Buffer = data,
			.ByteCount = height*stride,
			.Writable = 0,
			.Reserved = 0
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.mptr,
		.sbuf = &src.bits,
		.flags = 0,
		.kernel_bufs = (1 << 0)
	};

	DRM_DEBUG_DRIVER(PFX_FMT "---- MOUSE: SET_POINTER(wh:%d height:%d hotx:%d hoty:%d ptr:%p) ----",
		PFX_ARG, width, height, hotx, hoty, data);

	return call_tg_sync(prl_dev->tg_dev, &sdesc);
}

static int prl_drm_mouse_hide_pointer(struct prl_drm_device *prl_dev)
{
	struct {
		TG_REQUEST req;
	} src = {{
			.Request = TG_REQUEST_MOUSE_HIDE_POINTER,
			.Status	= TG_STATUS_SUCCESS,
			.InlineByteCount = 0,
			.BufferCount = 0,
			.Reserved = 0
		},
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = 0,
		.sbuf = 0,
		.flags = 0
	};

	DRM_DEBUG_DRIVER(PFX_FMT "---- MOUSE: HIDE_POINTER ----", PFX_ARG);
	return call_tg_sync(prl_dev->tg_dev, &sdesc);
}

static unsigned prl_drm_create_pbuffer(struct prl_drm_device *prl_dev, unsigned short format[IMAGE_DESC_FORMAT_MAX],
	bool recreate)
{
	struct {
		TG_REQUEST req;
		VID_TG_GL_CREATE create;
	} src = {{
			.Request = recreate ? TG_REQUEST_GL_RECREATE_BUFFER : TG_REQUEST_GL_CREATE_BUFFER,
			.Status	= TG_STATUS_PENDING,
			.InlineByteCount = sizeof(VID_TG_GL_CREATE),
			.BufferCount = 0,
			.Reserved = 0
		},
		{
			.process = 0,
			.handle = 0
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.create,
		.sbuf = 0,
		.flags = 0
	};

	memcpy(src.create.format, format, sizeof(format[IMAGE_DESC_FORMAT_MAX]));

	DRM_DEBUG_DRIVER(PFX_FMT "---- %sCREATE PBUFFER ----", PFX_ARG, recreate ? "RE-" : "");
	if (call_tg_sync(prl_dev->tg_dev, &sdesc))
		return 0;

	return src.create.handle;
}

static unsigned prl_drm_image_find_owner(struct drm_file *file, struct prl_drm_image *prl_img)
{
	unsigned i = 0;
	while (i < PRL_DRM_IMAGE_MAX_OWNER && prl_img->owners[i] != file) i++;
	return i;
}

static void prl_drm_destroy_pbuffer(struct prl_drm_device *prl_dev, unsigned pbuffer)
{
	struct {
		TG_REQUEST req;
		VID_TG_GL_DESTROY destroy;
	} src = {{
			.Request = TG_REQUEST_GL_DESTROY_BUFFER,
			.Status	= TG_STATUS_PENDING,
			.InlineByteCount = sizeof(VID_TG_GL_DESTROY),
			.BufferCount = 0,
			.Reserved = 0
		},
		{
			.process = 0,
			.handle = pbuffer
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.destroy,
		.sbuf = 0,
		.flags = 0
	};

	DRM_DEBUG_DRIVER(PFX_FMT "---- DESTROY PBUFFER ----", PFX_ARG);
	call_tg_sync(prl_dev->tg_dev, &sdesc);
}

static struct prl_drm_image *prl_drm_create_image(struct drm_file *file, struct image_desc *desc)
{
	struct prl_drm_file *prl_file = (struct prl_drm_file*)file->driver_priv;
	struct prl_drm_device *prl_dev = prl_file->prl_dev;
	struct prl_drm_image *prl_img = kzalloc(sizeof(*prl_img), GFP_KERNEL);
	if (unlikely(!prl_img)) {
		DRM_ERROR(PFX_FMT "Failed allocating an image structure.", PFX_ARG);
		return NULL;
	}

	INIT_LIST_HEAD(&prl_img->img_list);
	prl_img->num_owners = 1;
	prl_img->owners[0] = file;

	prl_img->desc = *desc;

	prl_img->desc.pbuffer = prl_drm_create_pbuffer(prl_dev, desc->pformat, false);
	if (!prl_img->desc.pbuffer) {
		DRM_ERROR(PFX_FMT "pbuffer create failed!", PFX_ARG);
		goto init_failed;
	}

	if (desc->handle) {
		prl_img->prl_go = (struct prl_gem_object *)drm_gem_object_lookup(file, desc->handle);
		if (!prl_img->prl_go) {
			DRM_ERROR(PFX_FMT "object loockup for handle %d failed!", PFX_ARG, desc->handle);
			goto lookup_failed;
		}
	}

	list_add(&prl_img->img_list, &prl_dev->img_list);

	return prl_img;

lookup_failed:
	prl_drm_destroy_pbuffer(prl_dev, prl_img->desc.pbuffer);
init_failed:
	kfree(prl_img);
	return NULL;
}

static void prl_drm_destroy_image(struct drm_file *file, struct prl_drm_image *prl_img)
{
	struct prl_drm_device *prl_dev = ((struct prl_drm_file*)file->driver_priv)->prl_dev;

	list_del(&prl_img->img_list);

	if (prl_img->prl_go)
		drm_gem_object_put_unlocked_X(&prl_img->prl_go->base);

	prl_drm_destroy_pbuffer(prl_dev, prl_img->desc.pbuffer);

	kfree(prl_img);
}

static PRL_VM_FAULT_T prl_drm_mmap(struct prl_drm_device *prl_dev, struct vm_area_struct *vma, resource_size_t mem_off)
{
	unsigned long size = vma->vm_end - vma->vm_start;
	resource_size_t mem_size = prl_dev->tg_dev->mem_size;
	resource_size_t mem_phys = prl_dev->tg_dev->mem_phys;
	int ret;

	vma->vm_page_prot = pgprot_noncached(vma->vm_page_prot);
	vma->vm_pgoff = 0;

	DRM_DEBUG_DRIVER(PFX_FMT "%lx .. %lx/%lu poff:%lx flags:%lx prot:%lx - phys:(%llx + %llu)/%llu",
		PFX_ARG, vma->vm_start, vma->vm_end, size,
		vma->vm_pgoff, vma->vm_flags, (long)vma->vm_page_prot.pgprot,
		mem_phys, mem_off, mem_size);

	if (size > (mem_size - mem_off))
		return VM_FAULT_OOM;

	ret = vm_iomap_memory(vma, mem_phys + mem_off, size);

	if (ret == 0 || ret == -EAGAIN || ret == -ERESTARTSYS || ret == -EINTR || ret == -EBUSY)
		return VM_FAULT_NOPAGE;

	if (ret == -ENOMEM)
		return VM_FAULT_OOM;

	return VM_FAULT_SIGBUS;
}

static uint64_t prl_drm_aperture_alloc(uint64_t size)
{
	uint64_t base = PRLVID_APERTURE_BASE;
	uint64_t best_base = ~0ull;
	uint64_t best_free = ~0ull;
	struct list_head *head, *best_head = &prl_drm_aperture_list;
	struct prl_drm_aperture_entry *entry = kmalloc(sizeof(struct prl_drm_aperture_entry), GFP_KERNEL);
	if (entry == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&entry->list);
	entry->size = size;

	// find best fit
	spin_lock(&prl_drm_aperture_lock);
	list_for_each(head, &prl_drm_aperture_list) {
		struct prl_drm_aperture_entry *p = list_entry(head, struct prl_drm_aperture_entry, list);

		uint64_t free = p->base - base;
		if (size <= free && free < best_free) {
			best_base = base;
			best_free = free;
			best_head = head;
		}
		base = roundup(p->base + p->size, PAGE_SIZE);
	}

	if (best_base < base) {
		base = best_base;
		head = best_head;
	}

	entry->base = base;
	list_add(&entry->list, head->prev);
	spin_unlock(&prl_drm_aperture_lock);

	DRM_DEBUG_DRIVER(PFX_FMT "---- APERTURE ALLOC: va:%llx size:%lld----", PFX_ARG, base, size);
	return base;
}

static void prl_drm_aperture_free(uint64_t base)
{
	struct list_head *head;

	spin_lock(&prl_drm_aperture_lock);
	list_for_each(head, &prl_drm_aperture_list) {
		struct prl_drm_aperture_entry *p = list_entry(head, struct prl_drm_aperture_entry, list);
		if (p->base == base) {
			list_del(&p->list);
			kfree(p);
			break;
		}
	}
	spin_unlock(&prl_drm_aperture_lock);

	DRM_DEBUG_DRIVER(PFX_FMT "---- APERTURE FREE: va:%llx ----", PFX_ARG, base);
}

static int prl_drm_aperture_map(struct prl_drm_device *prl_dev, uint64_t aperture, uint64_t size, void *ptr)
{
	struct {
		TG_REQUEST req;
		VID_TG_MAP_APERTURE map;
		TG_BUFFER buf;
	} src = {{
			.Request = TG_REQUEST_VID_MAP_APERTURE,
			.Status	= TG_STATUS_PENDING,
			.InlineByteCount = sizeof(VID_TG_MAP_APERTURE),
			.BufferCount = 1,
			.Reserved = 0
		},
		{
			.VidMemAddress = aperture
		},
		{
			.u.Buffer = ptr,
			.ByteCount = size,
			.Writable = 1
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.map,
		.sbuf = &src.buf,
		.flags = 0
	};
	prltg_buf_set_kernelspace(&sdesc, 0);

	DRM_DEBUG_DRIVER(PFX_FMT "---- APERTURE MAP: va:%llx size:%lld ----", PFX_ARG, aperture, size);
	return call_tg_sync(prl_dev->tg_dev, &sdesc);
}

static int prl_drm_aperture_unmap(struct prl_drm_device *prl_dev, uint64_t aperture)
{
	struct {
		TG_REQUEST req;
		VID_TG_UNMAP_APERTURE unmap;
		TG_UINT64 align;
	} src = {{
			.Request = TG_REQUEST_VID_UNMAP_APERTURE,
			.Status	= TG_STATUS_PENDING,
			.InlineByteCount = sizeof(VID_TG_UNMAP_APERTURE),
			.BufferCount = 0,
			.Reserved = 0
		},
		{
			.VidMemAddress = aperture
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.unmap,
		.sbuf = 0,
		.flags = 0
	};

	DRM_DEBUG_DRIVER(PFX_FMT "---- APERTURE UNMAP: va:%llx ----", PFX_ARG, aperture);
	return call_tg_sync(prl_dev->tg_dev, &sdesc);
}

// FB
static void prl_fb_destroy(struct drm_framebuffer *fb)
{
	struct prl_framebuffer *prl_fb = (struct prl_framebuffer*)fb;
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)fb->dev->dev_private;
	DRM_DEBUG_DRIVER(PFX_FMT " " FB_GO_FMT, PFX_ARG, FB_GO_ARG(fb));
	drm_framebuffer_cleanup(&prl_fb->base);
	drm_gem_object_put_unlocked_X(&prl_fb->prl_go->base);
	prl_fb->prl_go = NULL;
	if (prl_dev->fbdev && &prl_dev->fbdev->prl_fb == prl_fb) {
		DRM_ERROR(PFX_FMT "Destory prldrmfb?", PFX_ARG);
		return;
	}
	kfree(prl_fb);
}

static int prl_fb_create_handle(struct drm_framebuffer *fb,
	struct drm_file *file,
	unsigned int *handle)
{
	struct prl_framebuffer* prl_fb = (struct prl_framebuffer*)fb;
	int ret = drm_gem_handle_create(file, &prl_fb->prl_go->base, handle);
	DRM_DEBUG_DRIVER(PFX_FMT FB_GO_FMT "(handle:%d) -> ret:%d", PFX_ARG, FB_GO_ARG(fb), *handle, ret);
	return ret;
}

static int prl_fb_dirty(struct drm_framebuffer *fb,
	struct drm_file *file,
	unsigned flags,
	unsigned color,
	struct drm_clip_rect *clips,
	unsigned num_clips)
{
	struct prl_framebuffer* prl_fb = (struct prl_framebuffer*)fb;
	struct prl_gem_object *prl_go = prl_fb->prl_go;
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)fb->dev->dev_private;
	unsigned long long offset = fb->offsets[0] + prl_go->aperture_addr;
	int i, index = 0;

	DRM_DEBUG_DRIVER(PFX_FMT FB_GO_FMT " file:%p flags:%08x color:%08x num:%d head:%d",
		PFX_ARG, FB_GO_ARG(prl_fb), file, flags, color, num_clips, index);

	if (!prl_dev->shared_state.pending)
		prl_drm_share_state_start(prl_dev, &prl_dev->shared_state);

	if (!prl_dev->shared_state.pending)
		return 0;

	// Update bounds
	prl_drm_display_set_offset(prl_dev, index, offset);
	for(;;) {
		VID_TG_UPDATE_BOUNDS *bounds = &prl_dev->shared_state.data->update_bounds[index];
		VID_TG_UPDATE_BOUNDS cmp = *bounds;
		VID_TG_UPDATE_BOUNDS exe = cmp;

		// Merge rects
		if (!num_clips) {
			exe.s.left = 0;
			exe.s.top = 0;
			exe.s.right = fb->width;
			exe.s.bottom = fb->height;
		} else {
			for (i = 0; i < num_clips; i++) {
				if (exe.s.left > clips[i].x1)
					exe.s.left = clips[i].x1;
				if (exe.s.top > clips[i].y1)
					exe.s.top = clips[i].y1;
				if (exe.s.right < clips[i].x2)
					exe.s.right = clips[i].x2;
				if (exe.s.bottom < clips[i].y2)
					exe.s.bottom = clips[i].y2;
			}
		}
		// Update bounds
		if (exe.u == cmp.u || atomic64_cmpxchg((atomic64_t *)&bounds->u, cmp.u, exe.u) == cmp.u)
			break;
	}

	return 0;
}

static const struct drm_framebuffer_funcs prl_fb_funcs = {
	.destroy = prl_fb_destroy,
	.create_handle = prl_fb_create_handle,
	.dirty = prl_fb_dirty,
};

static struct drm_framebuffer *prl_kms_fb_create_modeset(
	struct drm_device *dev,
	struct drm_file *file,
	const struct drm_mode_fb_cmd2 *mode_cmd)
{
	int ret = -ENOMEM;
	struct prl_framebuffer *prl_fb = NULL;

	struct prl_gem_object *prl_go = (struct prl_gem_object *)drm_gem_object_lookup(file, mode_cmd->handles[0]);
	if (!prl_go) {
		DRM_INFO(PFX_FMT "object loockup for handle %d failed!", PFX_ARG, mode_cmd->handles[0]);
		return ERR_PTR(-ENOENT);
	}

	prl_fb = kzalloc(sizeof(*prl_fb), GFP_KERNEL);
	if (!prl_fb) {
		DRM_ERROR(PFX_FMT "framebuffer alloc failed!", PFX_ARG);
		goto fb_alloc_failed;
	}
	prl_fb->prl_go = prl_go;

	drm_helper_mode_fill_fb_struct_X(dev, &prl_fb->base, mode_cmd);

	ret = drm_framebuffer_init(dev, &prl_fb->base, &prl_fb_funcs);
	if (ret) {
		DRM_ERROR(PFX_FMT "framebuffer init failed %d", PFX_ARG, ret);
		goto fb_init_failed;
	}

	DRM_DEBUG_DRIVER(PFX_FMT FB_GO_FMT "(cmd->fb_id:%d) (%d x %d) fmt:%08x fl:%08x pitch:%d off:%d modi:%llx",
		PFX_ARG, FB_GO_ARG(prl_fb), mode_cmd->fb_id,
		mode_cmd->width, mode_cmd->height,
		mode_cmd->pixel_format, mode_cmd->flags,
		mode_cmd->pitches[0], mode_cmd->offsets[0], mode_cmd->modifier[0]);
	return &prl_fb->base;

fb_init_failed:
	kfree(prl_fb);
fb_alloc_failed:
	drm_gem_object_put_unlocked_X(&prl_go->base);
	return ERR_PTR(ret);
}

static const struct drm_mode_config_funcs prl_kms_funcs = {
	.fb_create = prl_kms_fb_create_modeset,
	.atomic_check = drm_atomic_helper_check,
	.atomic_commit = drm_atomic_helper_commit,
};

// KMS: Plane
static int prl_kms_atomic_check_plane_helper(struct drm_plane *plane, struct drm_plane_state *new_state)
{
	DRM_DEBUG_DRIVER(PFX_FMT PL_FB_FMT, PFX_ARG, PL_FB_ARG(plane));
	return 0;
}

static void prl_kms_atomic_update_primary_plane_helper(struct drm_plane *plane, struct drm_plane_state *old_state)
{
	struct drm_device *dev = (struct drm_device*)plane->dev;
	struct prl_framebuffer *prl_fb = (struct prl_framebuffer *)plane->state->fb;
	struct drm_crtc *crtc = plane->state->crtc ? plane->state->crtc : old_state->crtc;
	struct prl_drm_head *prl_head = (struct prl_drm_head*)crtc;

	DRM_DEBUG_DRIVER(PFX_FMT " " CR_FMT " " FB_GO_FMT " display(%d x %d) total(%d x %d)",
		PFX_ARG, CR_ARG(crtc), FB_GO_ARG(prl_fb),
		(crtc ? crtc->mode.hdisplay : 0), (crtc ? crtc->mode.vdisplay : 0),
		(crtc ? crtc->mode.htotal : 0), (crtc ? crtc->mode.vtotal : 0));

	if (crtc && prl_fb) {
		struct prl_gem_object *prl_go = prl_fb->prl_go;
		int stride = prl_fb->base.pitches[0];
		unsigned long long offset = prl_fb->base.offsets[0];
		unsigned vesa_state = get_vga_state();

		if (prl_go && prl_go->shared_index)
			offset += (unsigned long long)(prl_go->shared_index - 1) * prl_shared_buf_size;
		else if (prl_go && prl_go->aperture_addr)
			offset += prl_go->aperture_addr;

		DRM_DEBUG_DRIVER(PFX_FMT " " FB_GO_FMT " -> width:%d height:%d stride:%d offset:%llx svga:%08x",
			PFX_ARG, FB_GO_ARG(prl_fb), crtc->mode.hdisplay, crtc->mode.vdisplay, stride, offset, vesa_state);

		prl_drm_display_set_mode(dev->dev_private, prl_head,
			crtc->mode.hdisplay, crtc->mode.vdisplay, 32, stride, offset, !(vesa_state & 1));
	}
}

static void prl_kms_atomic_update_cursor_plane_helper(struct drm_plane *plane, struct drm_plane_state *old_state)
{
	struct drm_plane_state *state = plane->state;
	struct drm_device *dev = plane->dev;
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;

	DRM_DEBUG_DRIVER(PFX_FMT " " CR_FMT " CURSOR: (%d, %d, %u, %u, %p) -> (%d, %d, %u, %u, %p)",
		PFX_ARG, CR_ARG(plane->crtc),
		old_state ? old_state->crtc_x : 0, old_state ? old_state->crtc_y : 0,
		old_state ? old_state->crtc_w : 0, old_state ? old_state->crtc_h : 0, old_state ? old_state->fb : 0,
		state ? state->crtc_x : 0, state ? state->crtc_y : 0,
		state ? state->crtc_w : 0, state ? state->crtc_h : 0, state ? state->fb : 0);

	if (state->fb != old_state->fb) {
		struct prl_framebuffer *fb = (struct prl_framebuffer *)state->fb;

		if (fb && fb->prl_go && fb->prl_go->kptr)
			prl_drm_mouse_set_pointer(prl_dev,
				state->crtc_w, state->crtc_h, state->crtc_w*4,
				prl_drm_fb_hot_x(state->fb), prl_drm_fb_hot_y(state->fb),
				fb->prl_go->kptr);
		else
			prl_drm_mouse_hide_pointer(prl_dev);
	} else {
		VID_TG_MOUSE_POSITION *xy = &prl_dev->shared_state.data->mouse_position;

		// Start state sharing if not yet started
		if (!prl_dev->shared_state.pending)
			prl_drm_share_state_start(prl_dev, &prl_dev->shared_state);

		DRM_DEBUG_DRIVER(PFX_FMT " cursor move (%d, %d) -> (%d, %d)",
			PFX_ARG, xy->s.x, xy->s.y, state->crtc_x, state->crtc_y);

		xy->s.x = state->crtc_x;
		xy->s.y = state->crtc_y;
	}
}

static void prl_kms_destroy_plane(struct drm_plane *plane)
{
	DRM_DEBUG_DRIVER(PFX_FMT " " PL_FB_FMT, PFX_ARG, PL_FB_ARG(plane));
	drm_plane_cleanup(plane);
}

static const struct drm_plane_helper_funcs prl_kms_primary_plane_helper_funcs = {
	.atomic_check = prl_kms_atomic_check_plane_helper,
	.atomic_update = prl_kms_atomic_update_primary_plane_helper,
};

static const struct drm_plane_helper_funcs prl_kms_cursor_plane_helper_funcs = {
	.atomic_check = prl_kms_atomic_check_plane_helper,
	.atomic_update = prl_kms_atomic_update_cursor_plane_helper,
};

static const struct drm_plane_funcs prl_kms_plane_funcs = {
	.update_plane = drm_atomic_helper_update_plane,
	.disable_plane = drm_atomic_helper_disable_plane,
	.destroy = prl_kms_destroy_plane,
	.reset = drm_atomic_helper_plane_reset,
	.atomic_duplicate_state = drm_atomic_helper_plane_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_plane_destroy_state,
};

static const uint32_t prl_kms_formats[] = {
	DRM_FORMAT_ARGB8888,
	DRM_FORMAT_XRGB8888
};

// KMS: Connector
static int prl_kms_connector_dpms(struct drm_connector *connector, int mode)
{
	DRM_DEBUG_DRIVER(PFX_FMT CON_FMT " mode:%d", PFX_ARG, CON_ARG(connector), mode);
	return 0;
}

static enum drm_connector_status prl_kms_connector_detect(struct drm_connector *connector, bool force)
{
	DRM_DEBUG_DRIVER(PFX_FMT CON_FMT " force:%s", PFX_ARG, CON_ARG(connector), force ? "yes" : "no");
	return connector->status;
}

// fake timings for a 60Hz vrefresh mode.
static void prl_kms_connector_mode_add(struct drm_connector *connector,
	unsigned width, unsigned height, bool preferred)
{
	struct drm_display_mode _mode =
		{ DRM_MODE("", DRM_MODE_TYPE_DRIVER|(preferred ? DRM_MODE_TYPE_PREFERRED : 0),
			height*width/100*6,
			width,	width+50,	width+100,	width+150,	0,
			height, height+50,	height+100,	height+150,	0,
			DRM_MODE_FLAG_NHSYNC|DRM_MODE_FLAG_PVSYNC) };

	struct prl_drm_head *head = container_of(connector, struct prl_drm_head, connector);
	struct drm_display_mode *dup = NULL;

	snprintf(_mode.name, sizeof(_mode.name), "%dx%d", width, height);
	_mode.name[sizeof(_mode.name)-1] = 0;

	_mode.vrefresh = drm_mode_vrefresh(&_mode);

	dup = drm_mode_duplicate(connector->dev, &_mode);
	if (dup == NULL) {
		DRM_ERROR(PFX_FMT "Failed to duplicate mode! (%dx%d)", PFX_ARG, width, height);
		return;
	}

	drm_mode_probed_add(connector, dup);

	DRM_DEBUG_DRIVER(PFX_FMT "HEAD-%d mode (%dx%d) %s", PFX_ARG, head->index, width, height,
		preferred ? "PREFERRED!" : "");
}

static int prl_kms_connector_fill_modes(struct drm_connector *connector, uint32_t max_width, uint32_t max_height)
{
	// Add default modes
	static const unsigned _modes[][2] = {
		{ 640, 480 }, { 800, 600 }, { 1024, 768 }
	};

	struct prl_drm_head *head = container_of(connector, struct prl_drm_head, connector);
	struct drm_display_mode *mode, *t;
	int i, count = 0;

	// Remove all dyn-res modes (aka preferred) that is not current
	list_for_each_entry_safe(mode, t, &connector->modes, head) {
		if ((mode->type & DRM_MODE_TYPE_PREFERRED) &&
			(mode->hdisplay != head->mode_cur.width || mode->vdisplay != head->mode_cur.height)) {
				list_del(&mode->head);
				drm_mode_destroy(connector->dev, mode);
		}
	}
	// Add new preferred modes first
	if (head->mode_pref.width > 0 && head->mode_pref.width < max_width &&
		head->mode_pref.height && head->mode_pref.height <= max_height) {
		prl_kms_connector_mode_add(connector, head->mode_pref.width, head->mode_pref.height, true);
		count++;
	}
	// Add video mode that was used on boot
	if (screen_info.orig_video_isVGA == VIDEO_TYPE_VLFB && screen_info.lfb_depth == 32) {
		prl_kms_connector_mode_add(connector, screen_info.lfb_width, screen_info.lfb_height, count == 0);
		count++;
	}
	// Add all default modes
	for (i = 0; i < ARRAY_SIZE(_modes); i++) {
		if (_modes[i][0] <= max_width && _modes[i][1] <= max_height) {
			prl_kms_connector_mode_add(connector, _modes[i][0], _modes[i][1], false);
			count++;
		}
	}


	drm_connector_list_update_X(connector);
	// Move the prefered mode first, help apps pick the right mode.
	drm_mode_sort(&connector->modes);

	return count;
}

static int prl_kms_connector_set_property(struct drm_connector *connector, struct drm_property *property, uint64_t val)
{
	DRM_DEBUG_DRIVER(PFX_FMT CON_FMT " " PROP_FMT " <- %llx",
		PFX_ARG, CON_ARG(connector), PROP_ARG(property), val);
	return 0;
}

static void prl_kms_connector_destroy(struct drm_connector *connector)
{
	DRM_DEBUG_DRIVER(PFX_FMT "  " CON_FMT, PFX_ARG, CON_ARG(connector));
	drm_connector_unregister(connector);
	drm_connector_cleanup(connector);
}

static int prl_kms_connector_atomic_set_property(struct drm_connector *connector,
	struct drm_connector_state *state, struct drm_property *property, uint64_t val)
{
	DRM_DEBUG_DRIVER(PFX_FMT CON_FMT " c_st:%p " PROP_FMT " <- %llx",
		PFX_ARG, CON_ARG(connector), state, PROP_ARG(property), val);
	return 0;
}

static int prl_kms_connector_atomic_get_property(struct drm_connector *connector,
	const struct drm_connector_state *state, struct drm_property *property, uint64_t *val)
{
	DRM_DEBUG_DRIVER(PFX_FMT CON_FMT " c_st:%p " PROP_FMT " ptr:%p",
		PFX_ARG, CON_ARG(connector), state, PROP_ARG(property), val);
	*val = 0;
	return 0;
}

static const struct drm_connector_helper_funcs prl_kms_connector_helper_funcs = {
	.best_encoder = drm_atomic_helper_best_encoder_X
};

static const struct drm_connector_funcs prl_kms_connector_funcs = {
	.dpms = prl_kms_connector_dpms,
	.detect = prl_kms_connector_detect,
	.fill_modes = prl_kms_connector_fill_modes,
	.set_property = prl_kms_connector_set_property,
	.destroy = prl_kms_connector_destroy,
	.reset = drm_atomic_helper_connector_reset,
	.atomic_duplicate_state = drm_atomic_helper_connector_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_connector_destroy_state,
	.atomic_set_property = prl_kms_connector_atomic_set_property,
	.atomic_get_property = prl_kms_connector_atomic_get_property,
};

// KMS: Encoder
static void prl_kms_encoder_destroy(struct drm_encoder *encoder)
{
	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
	drm_encoder_cleanup(encoder);
}

static const struct drm_encoder_funcs prl_kms_encoder_funcs = {
	.destroy = prl_kms_encoder_destroy,
};

// KMS: CRTC
#if (PRL_KMS_CRTC_GAMMA_SET_X == 1)
static int prl_kms_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b, uint32_t size,
	struct drm_modeset_acquire_ctx *ctx)
#elif (PRL_KMS_CRTC_GAMMA_SET_X == 2)
static int prl_kms_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b, uint32_t size)
#elif (PRL_KMS_CRTC_GAMMA_SET_X == 3)
static void prl_kms_crtc_gamma_set(struct drm_crtc *crtc, u16 *r, u16 *g, u16 *b, uint32_t start,
	uint32_t size)
#endif
{
	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
#if (PRL_KMS_CRTC_GAMMA_SET_X == 1) || (PRL_KMS_CRTC_GAMMA_SET_X == 2)
	return 0;
#endif
}

static void prl_kms_crtc_destroy(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER(PFX_FMT " " CR_FMT, PFX_ARG, CR_ARG(crtc));
	drm_crtc_cleanup(crtc);
}

static void prl_kms_crtc_reset(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER(PFX_FMT " " CR_FMT, PFX_ARG, CR_ARG(crtc));
	if (crtc->state) {
		__drm_atomic_helper_crtc_destroy_state_X(crtc, crtc->state);
		kfree(crtc->state);
	}

	crtc->state = kzalloc(sizeof(struct drm_crtc_state), GFP_KERNEL);
	crtc->state->crtc = crtc;
}

static void prl_kms_crtc_helper_prepare(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER(PFX_FMT CR_FMT, PFX_ARG, CR_ARG(crtc));
}

static void prl_kms_crtc_helper_disable(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER(PFX_FMT  CR_FMT, PFX_ARG, CR_ARG(crtc));
}

static void prl_kms_crtc_helper_mode_set_nofb(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER(PFX_FMT CR_FMT, PFX_ARG, CR_ARG(crtc));
}

static int prl_kms_crtc_helper_atomic_check(struct drm_crtc *crtc, struct drm_crtc_state *new_state)
{
	DRM_DEBUG_DRIVER(PFX_FMT CR_FMT " cr_st:%p", PFX_ARG, CR_ARG(crtc), new_state);
	return 0;
}

static void prl_kms_crtc_helper_atomic_begin(struct drm_crtc *crtc, struct drm_crtc_state *old_crtc_state)
{
	DRM_DEBUG_DRIVER(PFX_FMT CR_FMT " cr_st:%p", PFX_ARG, CR_ARG(crtc), old_crtc_state);
}

static void prl_kms_crtc_helper_commit(struct drm_crtc *crtc)
{
	DRM_DEBUG_DRIVER(PFX_FMT " " CR_FMT, PFX_ARG, CR_ARG(crtc));
}

static void prl_kms_crtc_helper_atomic_flush(struct drm_crtc *crtc, struct drm_crtc_state *old_crtc_state)
{
	struct drm_plane *primary = crtc->primary;
	struct drm_pending_vblank_event *event = crtc->state->event;

	DRM_DEBUG_DRIVER(PFX_FMT " " CR_FMT "PRIM:" PL_FB_FMT " CURSOR:" PL_FB_FMT,
		PFX_ARG, CR_ARG(crtc), PL_FB_ARG(primary), PL_FB_ARG(crtc->cursor));

	if (event) {
		crtc->state->event = NULL;

		spin_lock_irq(&crtc->dev->event_lock);
		if (drm_crtc_vblank_get(crtc) == 0) {
			DRM_DEBUG_DRIVER(PFX_FMT " arm_vblank_event", PFX_ARG);
			drm_crtc_arm_vblank_event(crtc, event);
		} else {
			DRM_DEBUG_DRIVER(PFX_FMT " send_vblank_event", PFX_ARG);
			drm_crtc_send_vblank_event(crtc, event);
		}
		 spin_unlock_irq(&crtc->dev->event_lock);
	}

	DRM_DEBUG_DRIVER(PFX_FMT " " CR_FMT " cr_st:%p evt:%p",
		PFX_ARG, CR_ARG(crtc), old_crtc_state, event);
}

static const struct drm_crtc_helper_funcs prl_kms_crtc_helper_funcs = {
	.prepare = prl_kms_crtc_helper_prepare,
	.disable = prl_kms_crtc_helper_disable,
	.commit = prl_kms_crtc_helper_commit,
	.mode_set_nofb = prl_kms_crtc_helper_mode_set_nofb,
	.atomic_check = prl_kms_crtc_helper_atomic_check,
	.atomic_begin = prl_kms_crtc_helper_atomic_begin,
	.atomic_flush = prl_kms_crtc_helper_atomic_flush,
};

static const struct drm_crtc_funcs prl_kms_crtc_funcs = {
	.gamma_set = prl_kms_crtc_gamma_set,
	.destroy = prl_kms_crtc_destroy,
	.reset = prl_kms_crtc_reset,
	.atomic_duplicate_state = drm_atomic_helper_crtc_duplicate_state,
	.atomic_destroy_state = drm_atomic_helper_crtc_destroy_state,
	.set_config = drm_atomic_helper_set_config,
	.page_flip = drm_atomic_helper_page_flip,
};

// KMS: Dispaly (CRTCs + Planes + Encoders + Connectors) == "PRL HEAD"
static void prl_drm_head_init(struct prl_drm_device *prl_dev, int index)
{
	struct drm_device *dev = prl_dev->dev;
	struct prl_drm_head *head = &prl_dev->heads[index];
	struct drm_plane *primary = &head->primary;
	struct drm_plane *cursor = &head->cursor;
	struct drm_connector *connector = &head->connector;
	struct drm_encoder *encoder = &head->encoder;
	struct drm_crtc *crtc = &head->crtc;

	head->index = index;

	// Create planes
	drm_atomic_helper_plane_reset(primary);
	drm_universal_plane_init_X(dev, primary, (1 << index), &prl_kms_plane_funcs,
		prl_kms_formats, ARRAY_SIZE(prl_kms_formats), DRM_PLANE_TYPE_PRIMARY);
	drm_plane_helper_add(primary, &prl_kms_primary_plane_helper_funcs);

	drm_atomic_helper_plane_reset(cursor);
	drm_universal_plane_init_X(dev, cursor, (1 << index), &prl_kms_plane_funcs,
		prl_kms_formats, ARRAY_SIZE(prl_kms_formats), DRM_PLANE_TYPE_CURSOR);
	drm_plane_helper_add(cursor, &prl_kms_cursor_plane_helper_funcs);

	// Create CRTC
	prl_kms_crtc_reset(crtc);
	drm_crtc_init_with_planes(dev, crtc, primary, cursor, &prl_kms_crtc_funcs, NULL);
	drm_crtc_helper_add(crtc, &prl_kms_crtc_helper_funcs);
	drm_mode_crtc_set_gamma_size(crtc, 256);

	// Create encoder
	drm_encoder_init(dev, encoder, &prl_kms_encoder_funcs, DRM_MODE_ENCODER_VIRTUAL, NULL);
	encoder->possible_crtcs = (1 << index);
	encoder->possible_clones = 0;

	// Create connector
	drm_atomic_helper_connector_reset(connector);
	drm_connector_init(dev, connector, &prl_kms_connector_funcs, DRM_MODE_CONNECTOR_VIRTUAL);
	drm_connector_helper_add(connector, &prl_kms_connector_helper_funcs);
	drm_object_attach_property(&connector->base, prl_dev->hotplug_mode_update_property, 1);
	drm_object_attach_property(&connector->base, dev->mode_config.suggested_x_property, 0);
	drm_object_attach_property(&connector->base, dev->mode_config.suggested_y_property, 0);
	connector->status = (index == 0) ? connector_status_connected : connector_status_disconnected;
	connector->state->best_encoder = encoder;
	drm_connector_attach_encoder_X(connector, encoder);
	drm_connector_register(connector);

	DRM_DEBUG_DRIVER(PFX_FMT "[HEAD-%d]: " CR_FMT ":  " PL_FMT ", " PL_FMT,
		PFX_ARG, head->index, CR_ARG(crtc), PL_ARG(primary), PL_ARG(cursor));
}

// FBDEV:
static struct fb_ops prl_fbdev_ops = {
	.owner = THIS_MODULE,
	//DRM_FB_HELPER_DEFAULT_OPS
	.fb_check_var = drm_fb_helper_check_var,
	.fb_set_par = drm_fb_helper_set_par,
	.fb_setcmap = drm_fb_helper_setcmap,
	.fb_blank = drm_fb_helper_blank,
	.fb_pan_display = drm_fb_helper_pan_display,
	.fb_fillrect = drm_fb_helper_cfb_fillrect,
	.fb_copyarea = drm_fb_helper_cfb_copyarea,
	.fb_imageblit = drm_fb_helper_cfb_imageblit
};

static struct prl_gem_object *prl_drm_gem_dumb_create_helper(struct drm_device *dev,
	uint32_t width, uint32_t height, uint32_t bpp, uint32_t *pitch, uint64_t *size);
static void prl_drm_gem_free_object(struct drm_gem_object *obj);

static int prl_fbdev_probe(struct drm_fb_helper *helper, struct drm_fb_helper_surface_size *sizes)
{
	struct prl_drm_fbdev *prl_fbdev = (struct prl_drm_fbdev*)helper;
	struct prl_drm_device *prl_dev = (struct prl_drm_device *)helper->dev->dev_private;
	struct prl_framebuffer *prl_fb = &prl_fbdev->prl_fb;
	struct fb_info *info;
	struct drm_mode_fb_cmd2 mode_cmd;
	uint64_t size;
	int ret;
	assert (prl_fb->prl_go == NULL);

	if (sizes->surface_bpp != 24 && sizes->surface_bpp != 32)
		return -EINVAL;

	memset(&mode_cmd, 0, sizeof(mode_cmd));
	mode_cmd.pixel_format = drm_mode_legacy_fb_format(sizes->surface_bpp, sizes->surface_depth);
	mode_cmd.width = sizes->surface_width;
	mode_cmd.height = sizes->surface_height;
	prl_fb->prl_go = prl_drm_gem_dumb_create_helper(prl_dev->dev,
		mode_cmd.width, mode_cmd.height, sizes->surface_bpp, &mode_cmd.pitches[0], &size);
	if (IS_ERR(prl_fb->prl_go))
		return PTR_ERR(prl_fb->prl_go);

	info = drm_fb_helper_alloc_fbi(helper);
	if (IS_ERR(info)) {
		ret = PTR_ERR(info);
		goto fb_helper_alloc_fbi_failed;
	}

	drm_helper_mode_fill_fb_struct_X(helper->dev, &prl_fb->base, &mode_cmd);
	ret = drm_framebuffer_init(helper->dev, &prl_fb->base, &prl_fb_funcs);
	if (ret) {
		DRM_ERROR(PFX_FMT "framebuffer init failed %d", PFX_ARG, ret);
		goto fb_helper_alloc_fbi_failed;
	}

	helper->fb = &prl_fb->base;
	info->par = helper;
	info->fbops = &prl_fbdev_ops;
	info->screen_buffer = prl_fb->prl_go->kptr;
	info->screen_size = size;
	prl_drm_fb_helper_fill_info_X(info, helper, sizes);

	strcpy(info->fix.id, PRLDRMFB);
	info->pixmap.flags = FB_PIXMAP_SYSTEM;
	info->fix.mmio_start = 0;
	info->fix.mmio_len = 0;

	DRM_DEBUG_DRIVER(PFX_FMT "Prldrmfb initialized", PFX_ARG);
	return 0;

fb_helper_alloc_fbi_failed:
	prl_drm_gem_free_object(&prl_fb->prl_go->base);
	prl_fb->prl_go = NULL;
	return ret;
}

static const struct drm_fb_helper_funcs prl_fbdev_funcs = {
	.fb_probe = prl_fbdev_probe,
};

// KMS:
static int prl_drm_driver_load(struct drm_device *dev, unsigned long chipset)
{
	int i, n, ret = 0;
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;
	struct prl_drm_shared_data *shared_data = prl_dev->shared_state.data;
	unsigned long mem_size = prl_dev->tg_dev->mem_size;
	DRM_DEBUG_DRIVER(PFX_FMT "cs:%lx", PFX_ARG, chipset);

	//prl_shared_buf_size = roundup(roundup(1440, 64)*roundup(900, 64)*4, PAGE_SIZE);
	prl_shared_buf_size = PRL_VIRTUAL_HEAD_BUFFER_SIZE;
	n = MIN(mem_size / prl_shared_buf_size, PRL_VIRTUAL_HEAD_BUFFERS_MAX);
	memset(prl_shared_buf_avail_mask, 0, PRL_SHARED_BUF_AVAIL_MASK_NUM*sizeof(prl_shared_buf_avail_mask[0]));
	for (i = 0; i < n; i++)
		set_bit(i, prl_shared_buf_avail_mask);

	// Init shared state
	for (i = 0; i < PRL_VIRTUAL_HEADS_MAX_COUNT; i++) {
		shared_data->update_bounds[i].s.left = 16383;
		shared_data->update_bounds[i].s.top = 16383;
		shared_data->update_bounds[i].s.right= -16384;
		shared_data->update_bounds[i].s.bottom = -16384;
	}
	shared_data->mouse_position.s.x = 0;
	shared_data->mouse_position.s.y = 0;

	if (prl_drm_query_heads(prl_dev, &prl_dev->heads_max, &prl_dev->heads_connected)) {
		DRM_ERROR(PFX_FMT "Failed to query heads info.", PFX_ARG);
		return -ENODEV;
	}

	DRM_DEBUG_DRIVER(PFX_FMT "heads:%u/%u, video memory %dMb for %d buffers of %llu bytes, aperture is %s",
		PFX_ARG, prl_dev->heads_max, prl_dev->heads_connected,
		(int)(mem_size/1024/1024), n, prl_shared_buf_size,
		(prl_dev->tg_dev->capabilities & (PRLVID_CAPABILITY_APERTURE_ONLY << 16)) ? "enabled" : "disabled");

	drm_mode_config_init(dev);
	dev->mode_config.funcs = &prl_kms_funcs;
	dev->mode_config.min_width = 16;
	dev->mode_config.min_height = 16;
	dev->mode_config.max_width = 8192;
	dev->mode_config.max_height = 8192;

	drm_mode_create_suggested_offset_properties(dev);
	prl_dev->hotplug_mode_update_property = drm_property_create_range(dev,
		DRM_MODE_PROP_IMMUTABLE, "hotplug_mode_update", 0, 1);

	for (i = 0; i < prl_dev->heads_max; i++)
		prl_drm_head_init(prl_dev, i);

	if (prldrmfb) {
		struct prl_drm_fbdev *fbdev = kzalloc(sizeof(struct prl_drm_fbdev), GFP_KERNEL);
		if (!fbdev) {
			DRM_ERROR(PFX_FMT "Prldrmfb alloc failed!", PFX_ARG);
			return -ENOMEM;
		}

		drm_fb_helper_prepare(dev, &fbdev->base, &prl_fbdev_funcs);
		ret = prl_drm_fb_helper_init_X(dev, &fbdev->base, 1 /*prl_dev->heads_max*/);
		if (ret) {
			DRM_ERROR(PFX_FMT "Prldrmfb init failed!", PFX_ARG);
			kfree(fbdev);
			return ret;
		}

		drm_fb_helper_single_add_all_connectors(&fbdev->base);
		drm_fb_helper_initial_config(&fbdev->base, 32);
		prl_dev->fbdev = fbdev;

		DRM_DEBUG_DRIVER(PFX_FMT "Prldrmfb was initialized", PFX_ARG);
	} else
		 DRM_DEBUG_DRIVER(PFX_FMT "Prldrmfb was disabled", PFX_ARG);

	return 0;
}

#if (PRL_DRM_DRIVER_UNLOAD_X == 1)
static int prl_drm_driver_unload(struct drm_device *dev)
#else
static void prl_drm_driver_unload(struct drm_device *dev)
#endif
{
	struct prl_drm_device *prl_dev = (struct prl_drm_device *)dev->dev_private;
	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);

	if (prl_dev->fbdev) {
		struct prl_drm_fbdev *fbdev = prl_dev->fbdev;
		drm_fb_helper_unregister_fbi(&fbdev->base);
		drm_fb_helper_fini(&fbdev->base);
		drm_framebuffer_cleanup(&fbdev->prl_fb.base);
		kfree(fbdev);
		prl_dev->fbdev = NULL;
	}

	prl_drm_atomic_helper_shutdown_X(prl_dev->dev);

	drm_mode_config_cleanup(prl_dev->dev);
#if (PRL_DRM_DRIVER_UNLOAD_X == 1)
	return 0;
#endif
}

static irqreturn_t prl_drm_irq_handler(int irq, void *arg)
{
	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
	return IRQ_NONE;
}

irqreturn_t prl_drm_thread_fn(int irq, void *arg)
{
	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
	return IRQ_NONE;
}

static u32 prl_drm_get_vblank_counter(struct drm_device *dev, unsigned int pipe)
{
	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
	return 0;
}

static int prl_drm_enable_vblank(struct drm_device *dev, unsigned int pipe)
{
	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
	return -ENOSYS;
}

static void prl_drm_disable_vblank(struct drm_device *dev, unsigned int pipe)
{
	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
}

#if (PRL_DRM_SET_BUSID_X == 1)
static int prl_drm_set_busid(struct drm_device *dev, struct drm_master *master)
{
	DRM_DEBUG_DRIVER(PFX_FMT "pci:0000:%02x:%02x.%d",
		PFX_ARG,
		dev->pdev->bus->number,
		PCI_SLOT(dev->pdev->devfn),
		PCI_FUNC(dev->pdev->devfn));
	return drm_pci_set_busid(dev, master);
}
#endif

static int prl_drm_master_set(struct drm_device *dev, struct drm_file *file, bool from_open)
{
	struct prl_drm_file *prl_file = file->driver_priv;
	struct prl_drm_device *prl_dev = prl_file->prl_dev;
	DRM_DEBUG_DRIVER(PFX_FMT "SS:%d FB:%d", PFX_ARG, prl_file->use_shared_state, num_registered_fb);

	// Start shared state on master set if it was used by userspace
	if (!prl_dev->shared_state.pending && prl_file->use_shared_state)
		prl_drm_share_state_start(prl_dev, &prl_dev->shared_state);

	return 0;
}

#if (PRL_DRM_MASTER_DROP_X == 1)
static void prl_drm_master_drop(struct drm_device *dev, struct drm_file *file)
#else
static void prl_drm_master_drop(struct drm_device *dev, struct drm_file *file, bool from_release)
#endif
{
	struct prl_drm_file *prl_file = file->driver_priv;
	struct prl_drm_device *prl_dev = prl_file->prl_dev;
	struct fb_info *fb0 = NULL;
	DRM_DEBUG_DRIVER(PFX_FMT "SS:%d FB:%d", PFX_ARG, prl_file->use_shared_state, num_registered_fb);

	prl_drm_mouse_hide_pointer(prl_dev);

	// Our own fbdev is active
	if (prl_dev->fbdev)
		return;

	if (prl_dev->shared_state.pending)
		prl_drm_share_state_stop(prl_dev, &prl_dev->shared_state);

	if (num_registered_fb > 0 && num_registered_fb <= FB_MAX)
		fb0 = registered_fb[0];

	if (fb0 && fb0->var.bits_per_pixel == 32
			&& prl_dev->tg_dev->mem_phys == fb0->fix.smem_start) {
		// setup svga mode to /dev/fb0 memory (local video memory, with zero offset)
		prl_drm_display_set_mode(prl_dev, &prl_dev->heads[0],
			fb0->var.xres, fb0->var.yres, 32, fb0->fix.line_length, 0, true);
		return;
	}

	set_vga_state(0);
}

static int prl_drm_open(struct drm_device *dev, struct drm_file *file)
{
	struct prl_drm_file *prl_file = kzalloc(sizeof(struct prl_drm_file), GFP_KERNEL);
	if (!prl_file) {
		DRM_ERROR(PFX_FMT "file private failed!", PFX_ARG);
		return -ENOMEM;
	}

	prl_file->prl_dev = dev->dev_private;
	prl_file->vtg_file = prl_vtg_open_common(file->filp, prl_file->prl_dev->tg_dev);
	if (!prl_file->vtg_file) {
		kfree(prl_file);
		return -ENOMEM;
	}

	file->driver_priv = prl_file;

	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
	return 0;
}

static void prl_drm_postclose(struct drm_device *dev, struct drm_file *file)
{
	struct prl_drm_image *img, *tmp;
	struct prl_drm_file *prl_file = file->driver_priv;
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;

	mutex_lock(&prl_dev->img_lock);
	list_for_each_entry_safe(img, tmp, &prl_dev->img_list, img_list) {
		unsigned idx = prl_drm_image_find_owner(file, img);
		if (idx != PRL_DRM_IMAGE_MAX_OWNER) {
			if (img->num_owners == 1) {
				DRM_DEBUG_DRIVER(PFX_FMT " IMG-%u - RELEASED-1", PFX_ARG, img->desc.pbuffer);

				prl_drm_destroy_image(file, img);
			} else {
				DRM_DEBUG_DRIVER(PFX_FMT " IMG-%u - RELEASED-2", PFX_ARG, img->desc.pbuffer);

				img->owners[idx] = NULL;
				img->num_owners--;
			}
		}
	}
	mutex_unlock(&prl_dev->img_lock);

	prl_vtg_release_common(prl_file->vtg_file);
	kfree(prl_file);

	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
}

static void prl_drm_gem_close_object(struct drm_gem_object *obj,
	struct drm_file *file)
{
	struct prl_gem_object *prl_go = (struct prl_gem_object*)obj;
	DRM_DEBUG_DRIVER(PFX_FMT " " GO_FMT, PFX_ARG, GO_ARG(prl_go));
}

static void prl_drm_gem_free_object(struct drm_gem_object *obj)
{
	struct prl_gem_object *prl_go = (struct prl_gem_object*)obj;
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)obj->dev->dev_private;
	DRM_DEBUG_DRIVER(PFX_FMT " " GO_FMT, PFX_ARG, GO_ARG(prl_go));

	if (prl_go->kptr) {
		if (prl_go->aperture_addr) {
			prl_drm_aperture_unmap(prl_dev, prl_go->aperture_addr);
			prl_drm_aperture_free(prl_go->aperture_addr);
		}

		if (is_vmalloc_addr(prl_go->kptr))
			vfree(prl_go->kptr);
		else
			kfree(prl_go->kptr);

		prl_dev->alloc_size -= prl_go->base.size;
	} else if (prl_go->shared_index) {
		set_bit(prl_go->shared_index - 1, prl_shared_buf_avail_mask);
	}

	drm_gem_object_release(&prl_go->base);
	kfree(prl_go);
}

static struct prl_gem_object *prl_drm_gem_dumb_create_helper(struct drm_device *dev,
	uint32_t width, uint32_t height, uint32_t bpp,
	uint32_t *pitch, uint64_t *size)
{
	int ret = -ENOMEM;
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;
	struct prl_gem_object *prl_go = NULL;
	uint32_t w = roundup(width, 64);
	uint32_t h = roundup(height, 64);

	*pitch = w*(bpp >> 3);
	*size = roundup(*pitch * h, PAGE_SIZE);
	if (!*size) {
		DRM_INFO(PFX_FMT "incorrect size %d x %d x %d !", PFX_ARG, width, height, bpp);
		return ERR_PTR(-EINVAL);
	}

	prl_go = kzalloc(sizeof(*prl_go), GFP_KERNEL);
	if (!prl_go) {
		DRM_ERROR(PFX_FMT "cache alloc failed !", PFX_ARG);
		return ERR_PTR(-ENOMEM);
	}
	mutex_init(&prl_go->lock);

	ret = drm_gem_object_init(dev, &prl_go->base, *size);
	if (ret) {
		DRM_ERROR(PFX_FMT "gem object init failed!", PFX_ARG);
		goto gem_object_failed;
	}

	prl_go->global_index = prl_gem_object_global_index++;

	// Buffer for cursor - use system memory
	if (prl_go->base.size <= PRL_DRM_CURSOR_BUF_MAX_SIZE) {
		prl_go->kptr = kmalloc(prl_go->base.size, GFP_KERNEL /*|GFP_DMA*/);
		if (IS_ERR(prl_go->kptr)) {
			DRM_ERROR(PFX_FMT "failed to alloc memory!", PFX_ARG);
			ret = -ENOMEM;
			goto gem_object_failed;
		}
		prl_dev->alloc_size += prl_go->base.size;

	// Large buffer - try to use aperture,
	} else if (prl_dev->tg_dev->capabilities & (PRLVID_CAPABILITY_APERTURE_ONLY << 16)) {
		prl_go->kptr = vmalloc_user(prl_go->base.size);
		if (IS_ERR(prl_go->kptr)) {
			DRM_ERROR(PFX_FMT "failed to alloc memory for aperture!", PFX_ARG);
			ret = -ENOMEM;
			goto gem_object_failed;
		}
		prl_dev->alloc_size += prl_go->base.size;

		prl_go->aperture_addr = prl_drm_aperture_alloc(prl_go->base.size);
		if (prl_go->aperture_addr == (uint64_t)-ENOMEM) {
			DRM_ERROR(PFX_FMT "failed to alloc memory for aperture entry!", PFX_ARG);
			ret = -ENOMEM;
			goto gem_object_failed;
		}

		if (prl_drm_aperture_map(prl_dev, prl_go->aperture_addr, prl_go->base.size, prl_go->kptr)) {
			DRM_ERROR(PFX_FMT "Failed to map apertue.", PFX_ARG);
			ret = -EINVAL;
			goto gem_aperture_failed;
		}

	// Large buffer - try to use video memory.
	} else if (prl_go->base.size <= prl_shared_buf_size) {
		int i;
		for (i = 0; i < PRL_VIRTUAL_HEAD_BUFFERS_MAX; i++)
			if (test_and_clear_bit(i, prl_shared_buf_avail_mask)) {
				prl_go->shared_index = i + 1;
				break;
			}

		if (!prl_go->shared_index) {
			DRM_ERROR(PFX_FMT "no more shared video memory for gem objects!!!", PFX_ARG);
			goto gem_object_failed;
		}
	// Buffer is too large
	} else {
		DRM_ERROR(PFX_FMT "object is too large (%zu > %lld)!!!",
			PFX_ARG, prl_go->base.size, prl_shared_buf_size);
		goto gem_object_failed;
	}

	DRM_DEBUG_DRIVER(PFX_FMT "%u x %u x %u = %u x %u pitch:%u size:%llu " GO_FMT " ALLOCATED(%lu)",
		PFX_ARG, width, height, bpp, w, h, *pitch, *size, GO_ARG(prl_go),
		prl_dev->alloc_size);
	return prl_go;

gem_aperture_failed:
	if (prl_go->aperture_addr)
		prl_drm_aperture_free(prl_go->aperture_addr);

gem_object_failed:
	if (prl_go->kptr) {
		prl_dev->alloc_size -= prl_go->base.size;

		if (is_vmalloc_addr(prl_go->kptr))
			vfree(prl_go->kptr);
		else
			kfree(prl_go->kptr);
	}
	kfree(prl_go);
	return ERR_PTR(ret);
}

static int prl_drm_gem_dumb_create(struct drm_file *file,
	struct drm_device *dev,
	struct drm_mode_create_dumb *args)
{
	int ret;
	struct prl_gem_object *prl_go = prl_drm_gem_dumb_create_helper(dev,
		args->width, args->height, args->bpp, &args->pitch, &args->size);
	if (IS_ERR(prl_go))
		return PTR_ERR(prl_go);

	ret = drm_gem_handle_create(file, &prl_go->base, &args->handle);
	drm_gem_object_put_unlocked_X(&prl_go->base);
	if (ret) {
		DRM_ERROR(PFX_FMT "create handle for gem object failed!!", PFX_ARG);
		prl_drm_gem_free_object(&prl_go->base);
		return ret;
	}

	prl_go->handle = args->handle;
	return 0;
}

static int prl_drm_gem_dumb_map_offset(struct drm_file *file,
	struct drm_device *dev,
	uint32_t handle,
	uint64_t *offset)
{
	int ret = -EINVAL;
	struct prl_gem_object *prl_go = (struct prl_gem_object*)drm_gem_object_lookup(file, handle);
	if (!prl_go) {
		DRM_INFO(PFX_FMT "object loockup for GEM-0:%d failed!", PFX_ARG, handle);
		return -ENOENT;
	}

	ret = drm_gem_create_mmap_offset(&prl_go->base);
	if (ret) {
		DRM_ERROR(PFX_FMT "create mmap offest for " GO_FMT " failed!", PFX_ARG, GO_ARG(prl_go));
		goto range_check_failed;
	}

	*offset = drm_vma_node_offset_addr(&prl_go->base.vma_node);

	DRM_DEBUG_DRIVER(PFX_FMT GO_FMT " offeset:%llx", PFX_ARG, GO_ARG(prl_go), *offset);

range_check_failed:
	drm_gem_object_put_unlocked_X(&prl_go->base);
	return ret;
}

static int prl_drm_gem_dumb_destroy(struct drm_file *file,
	struct drm_device *dev,
	uint32_t handle)
{
	DRM_DEBUG_DRIVER(PFX_FMT "handle:%d", PFX_ARG, handle);
	return drm_gem_dumb_destroy(file, dev, handle);
}

static int prl_drm_prime_fd_to_handle(struct drm_device *dev,
	struct drm_file *file, int fd, u32 *handle)
{
	int ret;
	ret = drm_gem_prime_fd_to_handle(dev, file, fd, handle);
	DRM_DEBUG_DRIVER(PFX_FMT " fd:%08x -> h:%08x (%08x)", PFX_ARG, fd, *handle, ret);
	return ret;
}

static int prl_drm_prime_handle_to_fd(struct drm_device *dev,
	struct drm_file *file, uint32_t handle, uint32_t flags, int *prime_fd)
{
	int ret;
	ret = drm_gem_prime_handle_to_fd(dev, file, handle, flags, prime_fd);
	DRM_DEBUG_DRIVER(PFX_FMT " h:%08x flg:%08x -> fd:%08x (%08x)",
		PFX_ARG, handle, flags, *prime_fd, ret);
	return ret;
}

#if (PRL_DRM_PRIME_EXPORT_DEV == 1)
static struct dma_buf *prl_drm_gem_prime_export(struct drm_device *dev,
	struct drm_gem_object *obj, int flags)
#else
static struct dma_buf *prl_drm_gem_prime_export(struct drm_gem_object *obj, int flags)
#endif
{
	struct dma_buf *dma_buf;
#if (PRL_DRM_PRIME_EXPORT_DEV == 1)
	dma_buf = drm_gem_prime_export(dev, obj, flags);
#else
	dma_buf = drm_gem_prime_export(obj, flags);
#endif
	DRM_DEBUG_DRIVER(PFX_FMT " " GO_FMT " fl:%08x -> dma:%p", PFX_ARG, GO_ARG(obj), flags, dma_buf);
	return dma_buf;
}

static struct drm_gem_object *prl_drm_gem_prime_import(struct drm_device *dev,
	struct dma_buf *dma_buf)
{
	struct drm_gem_object *obj;
	obj = drm_gem_prime_import(dev, dma_buf);
	DRM_DEBUG_DRIVER(PFX_FMT " dma:%p -> " GO_FMT, PFX_ARG, dma_buf, GO_ARG(obj));
	return obj;
}

static PRL_VM_FAULT_T prl_vm_insert_mixed(struct vm_area_struct *vma, unsigned long addr, pfn_t pfn)
{
#if (PRL_DRM_VM_OPERATIONS_FAULT_X == 1)
	return vmf_insert_mixed(vma, addr, pfn);
#else
	int ret = vm_insert_mixed(vma, addr, pfn);
	if (ret == 0 || ret == -EAGAIN || ret == -ERESTARTSYS || ret == -EINTR || ret == -EBUSY)
		return VM_FAULT_NOPAGE;

	if (ret == -ENOMEM)
		return VM_FAULT_OOM;

	return VM_FAULT_SIGBUS;
#endif
}

// Shared state vm operations
#if (PRL_DRM_VM_OPERATIONS_FAULT_X == 0)
static PRL_VM_FAULT_T prl_drm_ss_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
#else
static PRL_VM_FAULT_T prl_drm_ss_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#endif
	struct prl_drm_file *prl_file = vma->vm_private_data;
	struct prl_drm_device *prl_dev = prl_file->prl_dev;
	unsigned long pfn = page_to_pfn(virt_to_page(prl_dev->shared_state.data));
	PRL_VM_FAULT_T ret = prl_vm_insert_mixed(vma, vma->vm_start, __pfn_to_pfn_t(pfn, PFN_DEV));

	// Start shared state if not yet started
	if (!prl_dev->shared_state.pending)
		prl_drm_share_state_start(prl_dev, &prl_dev->shared_state);

	prl_file->use_shared_state = true;

	return ret;
}

static void prl_drm_ss_vm_open(struct vm_area_struct *vma)
{
	struct prl_drm_file *prl_file = vma->vm_private_data;
	struct prl_drm_device *prl_dev = prl_file->prl_dev;
	DRM_DEBUG_DRIVER(PFX_FMT "+++ SS:%d", PFX_ARG, prl_file->use_shared_state);

	// Start shared state if not yet started
	if (!prl_dev->shared_state.pending)
		prl_drm_share_state_start(prl_dev, &prl_dev->shared_state);

	prl_file->use_shared_state = true;
}

static void prl_drm_ss_vm_close(struct vm_area_struct *vma)
{
	struct prl_drm_file *prl_file = vma->vm_private_data;
	struct prl_drm_device *prl_dev = prl_file->prl_dev;
	DRM_DEBUG_DRIVER(PFX_FMT "--- SS:%d", PFX_ARG, prl_file->use_shared_state);

	// Stop shared state if it is pending
	if (prl_dev->shared_state.pending)
		prl_drm_share_state_stop(prl_dev, &prl_dev->shared_state);

	prl_file->use_shared_state = false;
}

static const struct vm_operations_struct prl_drm_ss_vm_ops = {
	.fault = prl_drm_ss_vm_fault,
	.open = prl_drm_ss_vm_open,
	.close = prl_drm_ss_vm_close,
};

// GEM vm operations
#if (PRL_DRM_VM_OPERATIONS_FAULT_X == 0)
static PRL_VM_FAULT_T prl_drm_gem_vm_fault(struct vm_area_struct *vma, struct vm_fault *vmf)
{
#else
static PRL_VM_FAULT_T prl_drm_gem_vm_fault(struct vm_fault *vmf)
{
	struct vm_area_struct *vma = vmf->vma;
#endif

#if (PRL_DRM_VM_FAULT_ADDRESS_X == 1)
	unsigned long address = vmf->address;
#else
	unsigned long address = (unsigned long)vmf->virtual_address;
#endif

	struct prl_gem_object *prl_go = vma->vm_private_data;
	unsigned long pfn = 0;
	PRL_VM_FAULT_T ret = VM_FAULT_NOPAGE;

	if (address > vma->vm_end) {
		DRM_INFO(PFX_FMT " " VMA_FMT " " GO_FMT " fault:%lx -> out of [start, end]!!",
			PFX_ARG, VMA_ARG(vma), GO_ARG(prl_go), address);
		return VM_FAULT_OOM;
	}

	if (mutex_lock_interruptible(&prl_go->lock)) {
		DRM_ERROR(PFX_FMT " failed to lock!", PFX_ARG);
		return VM_FAULT_SIGBUS;
	}

	if (prl_go->shared_index) {
		resource_size_t phys_off = (prl_go->shared_index - 1)*prl_shared_buf_size;

		if (prl_go->uptr) {
			DRM_DEBUG_DRIVER(PFX_FMT " already mapped", PFX_ARG);
			goto out_unlock;
		}

		DRM_DEBUG_DRIVER(PFX_FMT " " VMA_FMT " " GO_FMT " fault:%lx phys_off:%lld",
			PFX_ARG, VMA_ARG(vma), GO_ARG(prl_go), address, phys_off);

		ret = prl_drm_mmap(prl_go->base.dev->dev_private, vma, phys_off);
		if (!(ret & VM_FAULT_ERROR))
			prl_go->uptr = (void*)vma->vm_start;

		goto out_unlock;
	}

	if (is_vmalloc_addr(prl_go->kptr))
		pfn = vmalloc_to_pfn(prl_go->kptr + (address - vma->vm_start));
	else
		pfn = page_to_pfn(virt_to_page(prl_go->kptr + (address - vma->vm_start)));

	ret = prl_vm_insert_mixed(vma, address, __pfn_to_pfn_t(pfn, PFN_DEV));
	if (!(ret & VM_FAULT_ERROR))
		prl_go->uptr = (void*)vma->vm_start;

out_unlock:
	mutex_unlock(&prl_go->lock);
	return ret;
}

static void prl_drm_gem_vm_open(struct vm_area_struct *vma)
{
	struct prl_gem_object *prl_go = vma->vm_private_data;
	drm_gem_vm_open(vma);
	DRM_DEBUG_DRIVER(PFX_FMT " " VMA_FMT " " GO_FMT, PFX_ARG, VMA_ARG(vma), GO_ARG(prl_go));
}

static void prl_drm_gem_vm_close(struct vm_area_struct *vma)
{
	struct prl_gem_object *prl_go = vma->vm_private_data;
	prl_go->uptr = 0;
	drm_gem_vm_close(vma);
	DRM_DEBUG_DRIVER(PFX_FMT " " VMA_FMT " " GO_FMT, PFX_ARG, VMA_ARG(vma), GO_ARG(prl_go));
}

static const struct vm_operations_struct prl_drm_gem_vm_ops = {
	.fault = prl_drm_gem_vm_fault,
	.open = prl_drm_gem_vm_open,
	.close = prl_drm_gem_vm_close,
};

// fops:
static int prl_drm_fops_open(struct inode *inode, struct file *filp)
{
	DRM_DEBUG_DRIVER(PFX_FMT "inod:%lu file:%p task:%s",
		PFX_ARG, (inode) ?  inode->i_ino : 0, filp, current->comm);
	return drm_open(inode, filp);
}

static int prl_drm_fops_release(struct inode *inode, struct file *filp)
{
	DRM_DEBUG_DRIVER(PFX_FMT "inod:%lu file:%p", PFX_ARG, (inode) ?  inode->i_ino : 0, filp);
	return drm_release(inode, filp);
}

static unsigned int prl_drm_fops_poll(struct file *filp, struct poll_table_struct *wait)
{
	return drm_poll(filp, wait);
}

static ssize_t prl_drm_fops_read(struct file *filp, char __user *buffer,
		      size_t count, loff_t *offset)
{
	DRM_DEBUG_DRIVER(PFX_FMT "inod:%lu file:%p s:%zu",
		PFX_ARG, (filp && filp->f_inode) ? filp->f_inode->i_ino : 0, filp, count);
	return drm_read(filp, buffer, count, offset);
}

static long prl_drm_fops_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
	struct drm_file *file_priv = filp->private_data;
	DRM_DEBUG_DRIVER(PFX_FMT " inod:%lu file:%p cmd:%x arg:%lx - master:%d render:%d",
		PFX_ARG, (filp && filp->f_inode) ? filp->f_inode->i_ino : 0, filp, cmd, arg,
		drm_is_current_master_X(file_priv) ? 1 : 0,
		drm_is_render_client(file_priv) ? 1 : 0);

	if (cursormove == 0 && (cmd == DRM_IOCTL_MODE_CURSOR || cmd == DRM_IOCTL_MODE_CURSOR2)) {
		struct drm_mode_cursor d;
		if (copy_from_user(&d, (void __user *)arg, sizeof(d)))
			return -EFAULT;

		DRM_DEBUG_DRIVER(PFX_FMT "CURSOR(2) fl:%08x crtc:%u x:%d y:%d w:%u h:%u hndl:%u",
			PFX_ARG, d.flags, d.crtc_id, d.x, d.y, d.width, d.height, d.handle);
		return -EINVAL;
	}

	return drm_ioctl(filp, cmd, arg);
}

static int prl_drm_fops_mmap(struct file *filp, struct vm_area_struct *vma)
{
	int ret = 0;
	unsigned long size = vma->vm_end - vma->vm_start;

	vma->vm_flags |= VM_MIXEDMAP;

	// Map shared state to host and to user space
	if (vma->vm_pgoff == 0x000FFFFF && size == PAGE_SIZE) {
		vma->vm_flags |= VM_PFNMAP | VM_DONTEXPAND | VM_DONTDUMP | VM_DONTCOPY;
		vma->vm_ops = &prl_drm_ss_vm_ops;
		vma->vm_private_data = ((struct drm_file*)filp->private_data)->driver_priv;
		//vma->vm_page_prot = pgprot_writecombine(vm_get_page_prot(vma->vm_flags));
	} else
		ret = drm_gem_mmap(filp, vma);

	DRM_DEBUG_DRIVER(PFX_FMT "inod:%lu file:%p " VMA_FMT " poff:%lx prot:%lx ops:%p ret:%d",
		PFX_ARG, (filp && filp->f_inode) ? filp->f_inode->i_ino : 0, filp,
		VMA_ARG(vma), vma->vm_pgoff, (long)vma->vm_page_prot.pgprot, vma->vm_ops, ret);
	return ret;
}

static int prl_drm_create_drawable_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct prl_drm_file *prl_file = file->driver_priv;
	unsigned int id = *(unsigned int*)data;
	DRM_DEBUG_DRIVER(PFX_FMT " data:%p id:%x", PFX_ARG, data, id);
	return prl_vtg_create_drawable(prl_file->vtg_file, id);
}

static int prl_drm_destroy_drawable_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct prl_drm_file *prl_file = file->driver_priv;
	unsigned int id = *(unsigned int*)data;

	DRM_DEBUG_DRIVER(PFX_FMT " data:%p id:%x", PFX_ARG, data, id);
	return prl_vtg_destroy_drawable(prl_file->vtg_file, id);
}

static int prl_drm_clip_drawable_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct prl_drm_file *prl_file = file->driver_priv;
	struct draw_bdesc *pbdesc = *(struct draw_bdesc **)data;
	struct draw_bdesc hdr;
	int ret;

	DRM_DEBUG_DRIVER(PFX_FMT " data:%p pbdesc=%p", PFX_ARG, data, pbdesc);
	do {
		if (copy_from_user(&hdr, (void __user *)pbdesc, sizeof(hdr))) {
			ret = -EFAULT;
			break;
		}

		ret = prl_vtg_clip_drawable(prl_file->vtg_file, &hdr);
		if (ret)
			break;

		if (copy_to_user((void __user *)pbdesc, &hdr, sizeof(hdr)))
			ret = -EFAULT;
	}
	while(0);

	return ret;
}

static int prl_drm_get_memsize_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	unsigned int mem_size = ((struct prl_drm_device*)dev->dev_private)->tg_dev->mem_size;
	DRM_DEBUG_DRIVER(PFX_FMT " size:%u", PFX_ARG, mem_size);
	*(unsigned int*)data = mem_size;
	return 0;
}

static int prl_drm_activate_svga_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	DRM_DEBUG_DRIVER(PFX_FMT " flag:%u", PFX_ARG, *(unsigned int*)data);
	set_vga_state((*(unsigned int*)data == 0) ? 0 : 1);
	return 0;
}

static int prl_drm_host_request_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;
	struct prl_drm_file *prl_file = (struct prl_drm_file*)file->driver_priv;
	DRM_DEBUG_DRIVER(PFX_FMT " data:%p tg_req:%p", PFX_ARG, data, *(void**)data);
	return prl_vtg_user_to_host_request(prl_dev->tg_dev, *(void**)data, prl_file->vtg_file);
}

static int prl_drm_enable_heads_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct drm_connector *connector;
	unsigned index = *(unsigned*)data;
	unsigned i = 0;
	bool enable = (index & 0x80);
	index &= 0xf;

	DRM_DEBUG_DRIVER(PFX_FMT " HEAD-%u will be %sabled", PFX_ARG, index, enable ? "en" : "dis");

	mutex_lock(&dev->mode_config.mutex);
	list_for_each_entry(connector, &dev->mode_config.connector_list, head) {
		if (i == index /*&& connector->status != status*/) {
			connector->status = enable ?  connector_status_connected : connector_status_disconnected;
			prl_drm_enable_heads(dev->dev_private,
				container_of(connector, struct prl_drm_head, connector), enable);
		}

		i++;
	}
	mutex_unlock(&dev->mode_config.mutex);

	drm_sysfs_hotplug_event(dev);

	return 0;
}

static int prl_drm_add_mode_for_head_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;
	struct mode_desc *desc = (struct mode_desc*)data;
	struct prl_drm_head *head = &prl_dev->heads[desc->index];

	DRM_DEBUG_DRIVER(PFX_FMT "HEAD-%u new mode (%dx%d)", PFX_ARG, desc->index, desc->width, desc->height);

	mutex_lock(&dev->mode_config.mutex);
	head->mode_pref.width = desc->width;
	head->mode_pref.height = desc->height;
	mutex_unlock(&dev->mode_config.mutex);

	drm_sysfs_hotplug_event(dev);

	return 0;
}

static int prl_drm_image_create_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	int ret = 0;
	struct prl_drm_image *img, *prl_img = NULL;
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;
	struct image_desc *desc = (struct image_desc *)data;

	DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - IN", PFX_ARG, IMG_ARG(desc));

	// Illigal param combinations:
	if (desc->width == 0 || desc->height == 0) {
		ret = -EINVAL;
		goto exit;
	}

	mutex_lock(&prl_dev->img_lock);
	list_for_each_entry(img, &prl_dev->img_list, img_list) {
		//
		// This is always true: img->desc.pbuffer != 0 && img->desc.width != 0 && img->desc.height != 0
		//
		if (desc->pbuffer == img->desc.pbuffer) {
			unsigned idx = prl_drm_image_find_owner(file, img);

			if (idx != PRL_DRM_IMAGE_MAX_OWNER) {
				// We are exclusive owner of this image and can do resize and rebind freely
				if (img->num_owners == 1) {
					if (desc->handle != img->desc.handle) {
						// !!!BIND!!!
						if (img->prl_go == NULL) {
							img->prl_go = (struct prl_gem_object *)drm_gem_object_lookup(file, desc->handle);
							if (img->prl_go != NULL) {

								DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - BINDED", PFX_ARG, IMG_ARG(&img->desc));
							} else {
								ret = -ENOENT;

								DRM_ERROR(PFX_FMT " " IMG_FMT " - gem object loockup failed!",
									PFX_ARG, IMG_ARG(desc));
								break;
							}

						// !!!UNBIND!!!
						} else if (desc->handle == 0) {
							drm_gem_object_put_unlocked_X(&img->prl_go->base);
							img->prl_go = NULL;

							DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - UNBINDED", PFX_ARG, IMG_ARG(desc));
						}

					// !!!RESIZE!!!
					} else if (desc->width != img->desc.width || desc->height != img->desc.height ||
						desc->offset != img->desc.offset) {
						if (img->prl_go) {
							drm_gem_object_put_unlocked_X(&img->prl_go->base);
							img->prl_go = NULL;
							DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - RESIZED (and UNBINDED)", PFX_ARG, IMG_ARG(desc));
						} else
							DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - RESIZED", PFX_ARG, IMG_ARG(desc));

						desc->handle = 0;

					// !!!FORMAT!!! (RE-CREATE)
					} else if (memcmp(desc->pformat, img->desc.pformat, sizeof(unsigned short[IMAGE_DESC_FORMAT_MAX])) != 0) {
						unsigned pbuffer = prl_drm_create_pbuffer(prl_dev, desc->pformat, true);
						if (pbuffer != img->desc.pbuffer) {
							ret = -ENOENT;

							DRM_ERROR(PFX_FMT " " IMG_FMT " - image recreate failed!",
								PFX_ARG, IMG_ARG(desc));
							break;
						}
					} else
						DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - nothing to change.", PFX_ARG, IMG_ARG(desc));

					img->desc = *desc;
					prl_img = img;
					break;
				}

				// We are not exclusive owner of this image and can only drop ownership and create new image
				if (desc->handle != img->desc.handle ||
					desc->width != img->desc.width || desc->height != img->desc.height ||
					desc->offset != img->desc.offset ||
					memcmp(desc->pformat, img->desc.pformat, sizeof(unsigned short[IMAGE_DESC_FORMAT_MAX])) != 0) {
					drm_gem_object_put_unlocked_X(&img->prl_go->base);
					img->owners[idx] = NULL;
					img->num_owners--;

					DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - image released due to multiply ownership.",
						PFX_ARG, IMG_ARG(&img->desc));
				} else {
					prl_img = img;

					DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - nothig to change here.", PFX_ARG, IMG_ARG(desc));
				}

				break;
			}

			// We are not an owner of this image -> error
			ret = -ENOENT;
			DRM_ERROR(PFX_FMT " " IMG_FMT " - image ownership error!",
				PFX_ARG, IMG_ARG(desc));

		} else if (desc->pbuffer == 0 && desc->handle == img->desc.handle &&
				desc->width == img->desc.width && desc->height == img->desc.height &&
				desc->offset == img->desc.offset) {
			unsigned idx = prl_drm_image_find_owner(file, img);

			if (idx != PRL_DRM_IMAGE_MAX_OWNER) {

				break;
			}

			// !!!EXPORT!!!
			idx = prl_drm_image_find_owner(NULL, img);

			if (idx != PRL_DRM_IMAGE_MAX_OWNER) {
				img->owners[idx] = file;
				img->num_owners++;
				prl_img = img;

				DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - EXPORTED",
					PFX_ARG, IMG_ARG(&img->desc));
			} else {
				ret = -ENOENT;

				DRM_ERROR(PFX_FMT " " IMG_FMT " - image owners list is full!",
					PFX_ARG, IMG_ARG(&img->desc));
			}

			break;
		}
	}

	if (ret == 0 && prl_img == NULL) {
		prl_img = prl_drm_create_image(file, desc);
		if (prl_img == NULL)
			ret = -ENOENT;
	}

	if (prl_img != NULL)
		*desc = prl_img->desc;

	mutex_unlock(&prl_dev->img_lock);

exit:
	DRM_DEBUG_DRIVER(PFX_FMT " " IMG_FMT " - OUT:%d", PFX_ARG, IMG_ARG(desc), ret);
	return ret;
}

static int prl_drm_image_release_ioctl(struct drm_device *dev, void *data, struct drm_file *file)
{
	int ret = -ENOENT;
	struct prl_drm_image *img;
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;
	unsigned pbuffer = *(unsigned *)data;

	DRM_DEBUG_DRIVER(PFX_FMT " IMG-%u - IN", PFX_ARG, pbuffer);

	// illigal param combinations:
	if (pbuffer == 0) {
		ret = -EINVAL;
		goto exit;
	}

	mutex_lock(&prl_dev->img_lock);
	list_for_each_entry(img, &prl_dev->img_list, img_list) {
		if (img->desc.pbuffer == pbuffer) {
			unsigned idx = prl_drm_image_find_owner(file, img);

			if (idx == PRL_DRM_IMAGE_MAX_OWNER) {
				DRM_ERROR(PFX_FMT " IMG-%u - not an owner!", PFX_ARG, pbuffer);
			} else if (img->num_owners == 1) {
				prl_drm_destroy_image(file, img);
				ret = 0;
				DRM_DEBUG_DRIVER(PFX_FMT " IMG-%u - RELEASED-1", PFX_ARG, pbuffer);
			} else {
				img->owners[idx] = NULL;
				img->num_owners--;
				ret = 0;
				DRM_DEBUG_DRIVER(PFX_FMT " IMG-%u - RELEASED-2", PFX_ARG, pbuffer);
			}

			break;
		}
	}
	mutex_unlock(&prl_dev->img_lock);

exit:
	DRM_DEBUG_DRIVER(PFX_FMT " IMG-%u - OUT:%d", PFX_ARG, pbuffer, ret);
	return ret;
}

#define PRL_DRM_IOCTL_DEF(ioctl, func, flags) \
	[DRM_IOCTL_NR(ioctl##_IOCTL) - DRM_COMMAND_BASE] = { ioctl##_IOCTL, flags, func }

#define PRL_DRM_CREATE_DRAWABLE_IOCTL \
	DRM_IOW(DRM_COMMAND_BASE + PRL_DRM_CREATE_DRAWABLE, \
	unsigned int)
#define PRL_DRM_CLIP_DRAWABLE_IOCTL \
	DRM_IOWR(DRM_COMMAND_BASE + PRL_DRM_CLIP_DRAWABLE, \
	void*)
#define PRL_DRM_DESTROY_DRAWABLE_IOCTL \
	DRM_IOW(DRM_COMMAND_BASE + PRL_DRM_DESTROY_DRAWABLE, \
	unsigned int)
#define PRL_DRM_GET_MEMSIZE_IOCTL \
	DRM_IOR(DRM_COMMAND_BASE + PRL_DRM_GET_MEMSIZE, \
	unsigned int)
#define PRL_DRM_ACTIVATE_SVGA_IOCTL \
	DRM_IOW(DRM_COMMAND_BASE + PRL_DRM_ACTIVATE_SVGA, \
	unsigned int)
#define PRL_DRM_HOST_REQUEST_IOCTL \
	DRM_IOWR(DRM_COMMAND_BASE + PRL_DRM_HOST_REQUEST, \
	void*)
#define PRL_DRM_ENABLE_HEADS_IOCTL \
	DRM_IOW(DRM_COMMAND_BASE + PRL_DRM_ENABLE_HEADS, \
	unsigned int)
#define PRL_DRM_ENABLE_HEADS_IOCTL \
	DRM_IOW(DRM_COMMAND_BASE + PRL_DRM_ENABLE_HEADS, \
	unsigned int)
#define PRL_DRM_ADD_MODE_FOR_HEAD_IOCTL \
	DRM_IOW(DRM_COMMAND_BASE + PRL_DRM_ADD_MODE_FOR_HEAD, \
	struct mode_desc)
#define PRL_DRM_IMAGE_CREATE_IOCTL \
	DRM_IOWR(DRM_COMMAND_BASE + PRL_DRM_IMAGE_CREATE, \
	struct image_desc)
#define PRL_DRM_IMAGE_RELEASE_IOCTL \
	DRM_IOW(DRM_COMMAND_BASE + PRL_DRM_IMAGE_RELEASE, \
	unsigned int)

static const struct drm_ioctl_desc prl_drm_ioctls[] = {
	PRL_DRM_IOCTL_DEF(PRL_DRM_CREATE_DRAWABLE, prl_drm_create_drawable_ioctl,
		DRM_RENDER_ALLOW),
	PRL_DRM_IOCTL_DEF(PRL_DRM_CLIP_DRAWABLE, prl_drm_clip_drawable_ioctl,
		DRM_RENDER_ALLOW),
	PRL_DRM_IOCTL_DEF(PRL_DRM_DESTROY_DRAWABLE, prl_drm_destroy_drawable_ioctl,
		DRM_RENDER_ALLOW),
	PRL_DRM_IOCTL_DEF(PRL_DRM_GET_MEMSIZE, prl_drm_get_memsize_ioctl,
		DRM_RENDER_ALLOW),
	PRL_DRM_IOCTL_DEF(PRL_DRM_ACTIVATE_SVGA, prl_drm_activate_svga_ioctl,
		DRM_MASTER),
	PRL_DRM_IOCTL_DEF(PRL_DRM_HOST_REQUEST, prl_drm_host_request_ioctl,
		DRM_RENDER_ALLOW),
	PRL_DRM_IOCTL_DEF(PRL_DRM_ENABLE_HEADS, prl_drm_enable_heads_ioctl,
		DRM_RENDER_ALLOW),
	PRL_DRM_IOCTL_DEF(PRL_DRM_ADD_MODE_FOR_HEAD, prl_drm_add_mode_for_head_ioctl,
		DRM_RENDER_ALLOW),
	PRL_DRM_IOCTL_DEF(PRL_DRM_IMAGE_CREATE, prl_drm_image_create_ioctl,
		DRM_RENDER_ALLOW),
	PRL_DRM_IOCTL_DEF(PRL_DRM_IMAGE_RELEASE, prl_drm_image_release_ioctl,
		DRM_RENDER_ALLOW)
};

static const struct file_operations prl_drm_driver_fops = {
	.owner = THIS_MODULE,
	.open = prl_drm_fops_open,
	.release = prl_drm_fops_release,
	.unlocked_ioctl = prl_drm_fops_ioctl,
	.mmap = prl_drm_fops_mmap,
	.poll = prl_drm_fops_poll,
	.read = prl_drm_fops_read,
	.compat_ioctl = prl_drm_fops_ioctl,
	.llseek = noop_llseek,
};

static struct drm_driver prl_drm_driver = {
#if (PRL_DRM_DRIVER_PRIME_DEFINED == 1)
	.driver_features = DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM | DRIVER_RENDER | DRIVER_PRIME,
#else
	.driver_features = DRIVER_MODESET | DRIVER_ATOMIC | DRIVER_GEM | DRIVER_RENDER,
#endif
	.load = prl_drm_driver_load,
	.unload = prl_drm_driver_unload,
	.irq_handler = prl_drm_irq_handler,
	.get_vblank_counter = prl_drm_get_vblank_counter,
	.enable_vblank = prl_drm_enable_vblank,
	.disable_vblank = prl_drm_disable_vblank,
	.master_set = prl_drm_master_set,
	.master_drop = prl_drm_master_drop,
#if (PRL_DRM_SET_BUSID_X == 1)
	.set_busid = prl_drm_set_busid,
#endif
	.open = prl_drm_open,
	.postclose = prl_drm_postclose,

	.dumb_create = prl_drm_gem_dumb_create,
	.dumb_map_offset = prl_drm_gem_dumb_map_offset,
	.dumb_destroy = prl_drm_gem_dumb_destroy,

	.prime_fd_to_handle = prl_drm_prime_fd_to_handle,
	.prime_handle_to_fd = prl_drm_prime_handle_to_fd,
	.gem_prime_export = prl_drm_gem_prime_export,
	.gem_prime_import = prl_drm_gem_prime_import,
	.gem_close_object = prl_drm_gem_close_object,
	.gem_free_object_unlocked = prl_drm_gem_free_object,
	.gem_vm_ops = &prl_drm_gem_vm_ops,
	.ioctls = prl_drm_ioctls,
	.num_ioctls = ARRAY_SIZE(prl_drm_ioctls),

	.fops = &prl_drm_driver_fops,

	.name = DRV_SHORT_NAME,
	.desc = DRV_LONG_NAME,
	.date = DRV_DATE,
	.major = DRV_MAJOR,
	.minor = DRV_MINOR,
	.patchlevel = 0
};

static int prl_drm_dev_fops_open(struct inode *inode, struct file *filp)
{
	DRM_DEBUG_DRIVER(PFX_FMT, PFX_ARG);
	return 0;
}

static const struct file_operations prl_drm_dev_fops = {
	.owner = THIS_MODULE,
	.open = prl_drm_dev_fops_open,
	.llseek = noop_llseek,
};

int prl_drm_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	int ret = 0;
	struct drm_device *dev = NULL;
	struct prl_drm_device *prl_dev = NULL;

	ret = prl_tg_probe_common(pdev, VIDEO_DRM_TOOLGATE, NULL);
	if (unlikely(ret != 0))
		return ret;

	dev = drm_dev_alloc(&prl_drm_driver, &pdev->dev);
	if (IS_ERR(dev))
	{
		DRM_ERROR(PFX_FMT "Failed allocating a drm device.", PFX_ARG);
		return PTR_ERR(dev);
	}
	dev->pdev = pdev;

	prl_dev = kzalloc(sizeof(*prl_dev), GFP_KERNEL);
	if (unlikely(!prl_dev)) {
		DRM_ERROR(PFX_FMT "Failed allocating a drm device private struct.", PFX_ARG);
		goto priv_alloc_failed;
	}

	prl_dev->shared_state.data = (struct prl_drm_shared_data*)get_zeroed_page(GFP_KERNEL);
	if (unlikely(!prl_dev)) {
		DRM_ERROR(PFX_FMT "Failed allocating page for shared state.", PFX_ARG);
		goto priv_alloc_failed2;
	}

	mutex_init(&prl_dev->img_lock);
	INIT_LIST_HEAD(&prl_dev->img_list);

	prl_dev->dev = dev;
	prl_dev->tg_dev = pci_get_drvdata(pdev);
	dev->dev_private = prl_dev;
	pci_set_drvdata(pdev, dev);

	ret = pci_enable_device(pdev);
	if (ret) {
		DRM_ERROR(PFX_FMT "Failed enable pci for drm device.", PFX_ARG);
		goto pdev_enable_failed;
	}

	if (prldrmfb) {
		// Kick out conflicting framebuffer device
		struct apertures_struct *ap = alloc_apertures(1);
		if (!ap) {
			DRM_ERROR(PFX_FMT "Failed to alloc apertures struct.", PFX_ARG);
			ret = -ENOMEM;
			goto drm_dev_register_failed;
		}
		ap->ranges[0].base = prl_dev->tg_dev->mem_phys;
		ap->ranges[0].size = prl_dev->tg_dev->mem_size;
		remove_conflicting_framebuffers(ap, PRLDRMFB, true);
		kfree(ap);
	}

	ret = drm_dev_register(dev, ent->driver_data);
	if (ret) {
		DRM_DEBUG_DRIVER(PFX_FMT "Failed to register drm device.", PFX_ARG);
		goto drm_dev_register_failed;
	}

	return 0;

drm_dev_register_failed:
	pci_disable_device(pdev);
pdev_enable_failed:
	free_page((unsigned long)prl_dev->shared_state.data);
priv_alloc_failed2:
	kfree(prl_dev);
priv_alloc_failed:
	drm_dev_put_X(dev);
	return ret;
}

void prl_drm_remove(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct prl_drm_device *prl_dev;
	struct tg_dev *prl_tg;

	if (!dev) {
		DRM_DEBUG_DRIVER(PFX_FMT "remove called with no dev", PFX_ARG);
		return;
	}

	pci_disable_device(pdev);
	prl_dev = (struct prl_drm_device *)dev->dev_private;
	prl_tg = prl_dev->tg_dev;

	drm_dev_unregister(dev);
	drm_dev_put_X(dev);

	if (prl_dev->shared_state.pending)
		prl_drm_share_state_stop(prl_dev, &prl_dev->shared_state);
	free_page((unsigned long)prl_dev->shared_state.data);
	kfree(prl_dev);

	prl_tg_remove_common(prl_tg);
	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM
int prl_drm_suspend(struct pci_dev *pdev, pm_message_t state)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;
	return prl_tg_suspend_common(prl_dev->tg_dev, state);
}

int prl_drm_resume(struct pci_dev *pdev)
{
	struct drm_device *dev = pci_get_drvdata(pdev);
	struct prl_drm_device *prl_dev = (struct prl_drm_device*)dev->dev_private;
	return prl_tg_resume_common(prl_dev->tg_dev);
}
#endif

void prl_drm_init_module(void)
{
	INIT_LIST_HEAD(&prl_drm_aperture_list);
}
#endif
