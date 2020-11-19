/*
 * Copyright (C) 1999-2019 Parallels International GmbH. All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/proc_fs.h>
#include <linux/hash.h>
#include "prltg_common.h"
#include "prltg_compat.h"
#include "prltg_call.h"
#include "VidTg.h"

struct vtg_buffer {
	atomic_t			refcnt;
	struct draw_bdesc	bdesc;
};

struct vtg_hash_entry {
	unsigned int		id;
	unsigned int		used;
	struct list_head	hash_list;	// for vtg_hashtable[...]
	struct list_head	filp_list;	// for vtg_filp_private::hash_list
	struct file			*filp;
	struct vtg_buffer	*vtg_buffer;
};

struct vtg_glctx_entry {
	unsigned int		id;
	struct list_head	filp_list;	// for vtg_filp_private::glctx_list
};

DEFINE_SPINLOCK(vtg_hash_lock);
static struct list_head vtg_hashtable[VTG_HASH_SIZE];

static void vtg_glctx_created(struct vtg_filp_private *fp, unsigned id)
{
	struct vtg_glctx_entry *glctx = kmalloc(sizeof(*glctx), GFP_KERNEL);
	if (unlikely(!glctx)) {
		printk(KERN_ERR PFX "Failed allocating an glctx structure.");
		return;
	}

	glctx->id = id;
	INIT_LIST_HEAD(&glctx->filp_list);

	spin_lock(&fp->lock);
	list_add(&glctx->filp_list, &fp->glctx_list);
	spin_unlock(&fp->lock);

	DPRINTK("+++ GLCTX-%u fp=%p\n", id, fp);
}

static void vtg_glctx_destroyed(struct vtg_filp_private *fp, unsigned id)
{
	struct list_head *pos, *tmp;
	struct vtg_glctx_entry *glctx = NULL;

	spin_lock(&fp->lock);
	list_for_each_safe(pos, tmp, &fp->glctx_list) {
		glctx = list_entry(pos, struct vtg_glctx_entry, filp_list);
		if (glctx->id == id) {
			list_del(&glctx->filp_list);
			kfree(glctx);
			break;
		}
	}
	spin_unlock(&fp->lock);

	DPRINTK("--- GLCTX-%u fp=%p\n", id, fp);
}

static void vtg_glctx_destroy(struct vtg_filp_private *fp, unsigned id)
{
	struct {
		TG_REQUEST req;
		VID_TG_GL_DESTROY destroy;
	} src = {{
			.Request = TG_REQUEST_GL_DESTROY_CONTEXT,
			.Status	= TG_STATUS_PENDING,
			.InlineByteCount = sizeof(VID_TG_GL_DESTROY),
			.BufferCount = 0,
			.Reserved = 0
		},
		{
			.process = 0,
			.handle = id
		}
	};

	TG_REQ_DESC sdesc = {
		.src = &src.req,
		.idata = &src.destroy,
		.sbuf = 0,
		.flags = 0
	};

	call_tg_sync(fp->dev, &sdesc);
}

static inline void vtg_buffer_put(struct vtg_buffer *vb)
{
	if (vb && atomic_dec_and_test(&vb->refcnt)) {
		kfree(vb->bdesc.u.pbuf);
		kfree(vb);
	}
}

static inline void vtg_buffer_get(struct vtg_buffer *vb)
{
	if (vb)
		atomic_inc(&vb->refcnt);
}

static struct vtg_buffer *vtg_buffer_replace(TG_REQ_DESC *sdesc)
{
	TG_REQUEST *src;
	TG_BUFFER *sbuf;
	struct vtg_buffer *vb = NULL;
	unsigned id;
	struct list_head *head, *tmp;
	struct vtg_hash_entry *p;

	src = sdesc->src;
	if (src->BufferCount != 4)
		goto out;

	if (src->InlineByteCount < sizeof(unsigned)*3)
		goto out;

	id = *((unsigned *)sdesc->idata + 2);

	head = &vtg_hashtable[hash_ptr((void *)(unsigned long)id, VTG_HASH_BITS)];
	spin_lock(&vtg_hash_lock);
	list_for_each(tmp, head) {
		p = list_entry(tmp, struct vtg_hash_entry, hash_list);
		if (p->id == id) {
			vb = p->vtg_buffer;
			vtg_buffer_get(vb);
			p->used++;
			break;
		}
	}
	spin_unlock(&vtg_hash_lock);

	if (!vb)
		goto out;

	sbuf = sdesc->sbuf;
	sbuf += 3;
	sbuf->u.Va = vb->bdesc.u.va;
	sbuf->ByteCount = vb->bdesc.bsize;
	sbuf->Writable = 0;
	prltg_buf_set_kernelspace(sdesc, 3);

out:
	return vb;
}

void prl_vtg_release_common(struct vtg_filp_private *fp)
{
	while (!list_empty(&fp->hash_list)) {
		struct list_head *tmp;
		struct vtg_hash_entry *p;

		spin_lock(&fp->lock);
		if (unlikely(list_empty(&fp->hash_list))) {
			spin_unlock(&fp->lock);
			break;
		}
		tmp = fp->hash_list.next;
		list_del(tmp);
		spin_unlock(&fp->lock);

		p = list_entry(tmp, struct vtg_hash_entry, filp_list);
		spin_lock(&vtg_hash_lock);
		list_del(&p->hash_list);
		spin_unlock(&vtg_hash_lock);

		vtg_buffer_put(p->vtg_buffer);
		kfree(p);
	}

	while (!list_empty(&fp->glctx_list)) {
		struct list_head *tmp;
		struct vtg_glctx_entry *p;

		spin_lock(&fp->lock);
		if (unlikely(list_empty(&fp->glctx_list))) {
			spin_unlock(&fp->lock);
			break;
		}
		tmp = fp->glctx_list.next;
		list_del(tmp);
		spin_unlock(&fp->lock);

		p = list_entry(tmp, struct vtg_glctx_entry, filp_list);
		vtg_glctx_destroy(fp, p->id);
		kfree(p);
	}

	kfree(fp);
}

static int prl_vtg_release(struct inode *inode, struct file *filp)
{
	prl_vtg_release_common(filp->private_data);
	module_put(THIS_MODULE);
	return 0;
}

int prl_vtg_create_drawable(struct vtg_filp_private *fp, unsigned int id)
{
	struct vtg_hash_entry *vhe, *p;
	struct list_head *head, *tmp;

	vhe = kmalloc(sizeof(*vhe), GFP_KERNEL);
	if (vhe == NULL)
		return -ENOMEM;

	INIT_LIST_HEAD(&vhe->hash_list);
	INIT_LIST_HEAD(&vhe->filp_list);
	vhe->id = id;
	vhe->filp = fp->filp;
	vhe->vtg_buffer = NULL;
	vhe->used = 0;

	head = &vtg_hashtable[hash_ptr((void *)(unsigned long)vhe->id, VTG_HASH_BITS)];
	spin_lock(&vtg_hash_lock);
	list_for_each(tmp, head) {
		p = list_entry(tmp, struct vtg_hash_entry, hash_list);
		if (p->id == id) {
			spin_unlock(&vtg_hash_lock);
			kfree(vhe);
			return -EINVAL;
		}
	}
	list_add(&vhe->hash_list, head);
	spin_unlock(&vtg_hash_lock);

	spin_lock(&fp->lock);
	list_add(&vhe->filp_list, &fp->hash_list);
	spin_unlock(&fp->lock);
	return 0;
}

int prl_vtg_destroy_drawable(struct vtg_filp_private *fp, unsigned int id)
{
	struct vtg_hash_entry *p = NULL;
	struct list_head *head, *tmp, *n;
	struct vtg_buffer *old_vb = NULL;
	int ret = -EINVAL;

	head = &vtg_hashtable[hash_ptr((void *)(unsigned long)id, VTG_HASH_BITS)];
	spin_lock(&vtg_hash_lock);
	list_for_each_safe(tmp, n, head) {
		p = list_entry(tmp, struct vtg_hash_entry, hash_list);
		if (p->id == id) {
			if (p->filp == fp->filp) {
				old_vb = p->vtg_buffer;
				p->vtg_buffer = NULL;
				list_del(&p->hash_list);
				ret = 0;
			}
			break;
		}
	}
	spin_unlock(&vtg_hash_lock);
	if (!ret) {
		spin_lock(&fp->lock);
		list_del(&p->filp_list);
		spin_unlock(&fp->lock);
		vtg_buffer_put(old_vb);
		kfree(p);
	}
	return ret;
}

int prl_vtg_clip_drawable(struct vtg_filp_private *fp, struct draw_bdesc *hdr)
{
	struct vtg_hash_entry *p = NULL;
	struct list_head *head, *tmp;
	struct vtg_buffer *vb, *old_vb = NULL;
	int size = hdr->bsize;
	int ret = -ENOMEM;

	hdr->used = 0;
	vb = (struct vtg_buffer *)kmalloc(sizeof(*vb), GFP_KERNEL);
	if (!vb)
		return ret;
	memset(vb, 0, sizeof(*vb));

	vb->bdesc.u.pbuf = kmalloc(size, GFP_KERNEL);
	if (!vb->bdesc.u.pbuf)
		goto out_free;

	vb->bdesc.bsize = size;
	vb->bdesc.id = hdr->id;
	atomic_set(&vb->refcnt, 1);

	ret = -EFAULT;
	if (copy_from_user(vb->bdesc.u.pbuf, (void __user *)hdr->u.pbuf, size))
		goto out_vfree;

	head = &vtg_hashtable[hash_ptr((void *)(unsigned long)hdr->id, VTG_HASH_BITS)];
	ret = -EINVAL;
	spin_lock(&vtg_hash_lock);
	list_for_each(tmp, head) {
		p = list_entry(tmp, struct vtg_hash_entry, hash_list);
		if (p->id == hdr->id) {
			if (p->filp == fp->filp) {
				old_vb = p->vtg_buffer;
				p->vtg_buffer = vb;
				hdr->used = p->used;
				p->used = 0;
				ret = 0;
			}
			break;
		}
	}
	spin_unlock(&vtg_hash_lock);
	if (!ret) {
		vtg_buffer_put(old_vb);
		return 0;
	}

out_vfree:
	kfree(vb->bdesc.u.pbuf);
out_free:
	kfree(vb);
	return ret;
}

struct vtg_filp_private *prl_vtg_open_common(struct file *filp, struct tg_dev *dev)
{
	struct vtg_filp_private *fp;

	fp = kmalloc(sizeof(*fp), GFP_KERNEL);
	if (fp == NULL)
		return NULL;

	memset (fp, 0, sizeof(*fp));
	spin_lock_init(&fp->lock);
	INIT_LIST_HEAD(&fp->hash_list);
	INIT_LIST_HEAD(&fp->glctx_list);
	fp->filp = filp;
	fp->dev = dev;

	return fp;
}

int prl_vtg_user_to_host_request(struct tg_dev *dev, void *ureq, struct vtg_filp_private *fp)
{
	int ret;
	struct vtg_buffer *vb = NULL;
	TG_REQ_DESC sdesc;
	TG_REQUEST src;

	ret = prl_tg_user_to_host_request_prepare(ureq, &sdesc, &src);
	if (ret)
		return ret;

	if (src.Request == TG_REQUEST_GL_COMMAND)
		vb = vtg_buffer_replace(&sdesc);

	ret = call_tg_sync(dev, &sdesc);
	if (!ret && src.Request == TG_REQUEST_GL_CREATE_CONTEXT)
		vtg_glctx_created(fp, ((VID_TG_GL_CREATE*)sdesc.idata)->handle);
	else if (!ret && src.Request == TG_REQUEST_GL_DESTROY_CONTEXT)
		vtg_glctx_destroyed(fp, ((VID_TG_GL_DESTROY*)sdesc.idata)->handle);

	vtg_buffer_put(vb);

	return prl_tg_user_to_host_request_complete(ureq, &sdesc, ret);
}

static ssize_t prl_vtg_write(struct file *filp, const char __user *buf,
	size_t nbytes, loff_t *ppos)
{
	struct tg_dev *dev = PDE_DATA(FILE_DENTRY(filp)->d_inode);
	void *ureq = NULL;
	int ret = -EINVAL;

	DPRINTK("ENTER\n");
	if ((nbytes != sizeof(TG_REQUEST *)) && !PRL_32BIT_COMPAT_TEST)
		goto err;

	/* read userspace pointer */
	if (copy_from_user(&ureq, buf, nbytes)) {
		ret = -EFAULT;
		goto err;
	}

	ret = prl_vtg_user_to_host_request(dev, ureq, filp->private_data);

err:
	DPRINTK("EXIT, returning %d\n", ret);
	return ret;
}

static int prl_vtg_open(struct inode *inode, struct file *filp)
{
	struct vtg_filp_private *fp;

	if (!try_module_get(THIS_MODULE))
		return -ENODEV;

	fp = prl_vtg_open_common(filp,  PDE_DATA(inode));
	if (fp == NULL)
		return -ENOMEM;

	filp->private_data = fp;
#ifdef FMODE_ATOMIC_POS
	filp->f_mode &= ~FMODE_ATOMIC_POS;
#endif

	return 0;
}

static long prl_vtg_ioctl(struct file *filp, unsigned int cmd,
                          unsigned long arg)
{
	struct vtg_filp_private *fp = filp->private_data;
	struct draw_bdesc hdr;
	int ret = -ENOTTY;

	switch (cmd) {
	case VIDTG_CREATE_DRAWABLE:
		ret = prl_vtg_create_drawable(fp, (unsigned int)arg);
		break;

	case VIDTG_CLIP_DRAWABLE:
		if (copy_from_user(&hdr, (void __user *)arg, sizeof(hdr))) {
			ret = -EFAULT;
			break;
		}

		ret = prl_vtg_clip_drawable(fp, &hdr);
		if (ret)
			break;

		if (copy_to_user((void __user *)arg, &hdr, sizeof(hdr)))
			ret = -EFAULT;
		break;

	case VIDTG_DESTROY_DRAWABLE:
		ret = prl_vtg_destroy_drawable(fp, (unsigned int)arg);
		break;

#ifdef PRLVTG_MMAP
	case VIDTG_GET_MEMSIZE: {
		unsigned int memsize = fp->dev->mem_size;
		ret = copy_to_user((void __user *)arg, &memsize, sizeof(memsize));
		break;
	}

	case VIDTG_ACTIVATE_SVGA:
		outb(0xae, VGA_SEQ_I);
		outb((arg == 0) ? 0 : 1, VGA_SEQ_D);
		ret = 0;
		break;
#endif
	}

	return ret;
}

#ifdef PRLVTG_MMAP
static int prl_vtg_mmap(struct file *filp, struct vm_area_struct *vma)
{
	struct vtg_filp_private *fp = filp->private_data;
	unsigned long len = vma->vm_end - vma->vm_start;

	if (len > fp->dev->mem_size)
		return -EINVAL;
	return vm_iomap_memory(vma, (phys_addr_t)fp->dev->mem_phys, len);
}
#else
#define prl_vtg_mmap NULL
#endif

static struct proc_ops prl_vtg_ops = PRLTG_PROC_OPS_INIT(
		prl_vtg_open,
		prl_vtg_write,
		prl_vtg_ioctl,
		prl_vtg_mmap,
		prl_vtg_release);

int prl_vtg_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	return prl_tg_probe_common(pdev, VIDEO_TOOLGATE, &prl_vtg_ops);
}

void prl_vtg_remove(struct pci_dev *pdev)
{
	prl_tg_remove_common(pci_get_drvdata(pdev));
	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM
int prl_vtg_suspend(struct pci_dev *pdev, pm_message_t state)
{
	return prl_tg_suspend_common(pci_get_drvdata(pdev), state);
}

int prl_vtg_resume(struct pci_dev *pdev)
{
	return prl_tg_resume_common(pci_get_drvdata(pdev));
}
#endif

void prl_vtg_init_module(void)
{
	int i;
	for (i = 0; i < VTG_HASH_SIZE; i++)
		INIT_LIST_HEAD(&vtg_hashtable[i]);
}
