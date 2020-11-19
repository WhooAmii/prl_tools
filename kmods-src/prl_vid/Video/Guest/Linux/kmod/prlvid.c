/*
 * Copyright (C) 1999-2019 Parallels International GmbH. All Rights Reserved.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/pci.h>
#include "prlvid_common.h"

#if PRL_DRM_ENABLED
// prl_drm available starting from kernel 4.7.0 and enabled by default
static int usedrm = 1;
module_param(usedrm, int, 0);
MODULE_PARM_DESC(usedrm, "Use DRM/KMS instead of VTG interface");

int prl_vid_usedrm(void)
{
	static int select = -1;
	if (select != -1)
		return select;

	if (usedrm) {
		unsigned int capabilities;
		// Read VESA regs for CAPABILITIES
		outb(0xae, VGA_SEQ_I);
		capabilities = inl(VGA_SEQ_D);

		if (capabilities & (PRLVID_CAPABILITY_APERTURE_ONLY << 16)) {
			printk("Usedrm flag is set and aperture only capability is present -> "
					"will start DRM/KMS interface and use aperture memory!");
			select = 1;
		} else if (usedrm == 2) {
			printk("Usedrm forced, but aperture only absent -> "
					"will start DRM/KMS interface and use video memory!");
			select = 1;
		} else {
			printk("Usedrm flag is set, but aperture only capability is absent -> "
					"will start VTG interface and use video memory!");
			select = 0;
		}
	} else {
		printk("Usedrm flag is not set -> will start VTG interface and use video memory!");
		select = 0;
	}

	return select;
}
#endif

static char version[] = KERN_INFO DRV_LOAD_MSG "\n";

static int prl_vid_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
/* when built into the kernel, we only print version if device is found */
#ifndef MODULE
	static int printed_version;
	if (!printed_version++)
		printk(version);
#endif
	assert(pdev != NULL);
	assert(ent != NULL);

#if PRL_DRM_ENABLED
	if (prl_vid_usedrm())
		return prl_drm_probe(pdev, ent);
	else
#endif
		return prl_vtg_probe(pdev, ent);
}

static void prl_vid_remove(struct pci_dev *pdev)
{
#if PRL_DRM_ENABLED
	if (prl_vid_usedrm())
		prl_drm_remove(pdev);
	else
#endif
		prl_vtg_remove(pdev);
}

#ifdef CONFIG_PM
static int prl_vid_suspend(struct pci_dev *pdev, pm_message_t state)
{
#if PRL_DRM_ENABLED
	if (prl_vid_usedrm())
		return prl_drm_suspend(pdev, state);
	else
#endif
		return prl_vtg_suspend(pdev, state);
}

static int prl_vid_resume(struct pci_dev *pdev)
{
#if PRL_DRM_ENABLED
	if (prl_vid_usedrm())
		return prl_drm_resume(pdev);
	else
#endif
		return prl_vtg_resume(pdev);
}
#endif

static struct pci_device_id prl_vid_pci_tbl[] = {
	{0x1ab8, 0x4005, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VIDEO_TOOLGATE },
	{0x1af4, 0x1050, PCI_ANY_ID, PCI_ANY_ID, 0, 0, VIDEO_TOOLGATE },
	{0,}
};
MODULE_DEVICE_TABLE (pci, prl_vid_pci_tbl);

static struct pci_driver prl_vid_pci_driver = {
	.name		= DRV_SHORT_NAME,
	.id_table	= prl_vid_pci_tbl,
	.probe		= prl_vid_probe,
	.remove		= prl_vid_remove,
#ifdef CONFIG_PM
	.suspend	= prl_vid_suspend,
	.resume		= prl_vid_resume,
#endif /* CONFIG_PM */
};

static int __init prl_vid_init_module(void)
{
#ifdef MODULE
	printk(version);
#endif

#if PRL_DRM_ENABLED
	prl_drm_init_module();
#endif
	prl_vtg_init_module();

	/* we don't return error when devices probing fails,
	 * it's required for proper supporting hot-pluggable device */
	return pci_register_driver(&prl_vid_pci_driver);
}

static void __exit prl_vid_cleanup_module(void)
{
	pci_unregister_driver(&prl_vid_pci_driver);
}

module_init(prl_vid_init_module);
module_exit(prl_vid_cleanup_module);

MODULE_AUTHOR("Parallels International GmbH");
MODULE_DESCRIPTION(DRV_LONG_NAME);
MODULE_LICENSE("Parallels");
MODULE_VERSION(DRV_VERSION);
MODULE_INFO(supported, "external");
