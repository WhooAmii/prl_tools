/*
 * Copyright (C) 1999-2019 Parallels International GmbH. All Rights Reserved.
 */

#ifndef __PRLVID_COMMON_H__
#define __PRLVID_COMMON_H__

#include "prltg_common.h"
#include "Tg.h"
#include "VidTg.h"

#if PRL_DRM_ENABLED
#define DRV_LONG_NAME "Parallels Video (VTG+DRM/KMS)"
#else
#define DRV_LONG_NAME "Parallels Video (VTG)"
#endif

struct vtg_filp_private *prl_vtg_open_common(struct file *filp, struct tg_dev *dev);
void prl_vtg_release_common(struct vtg_filp_private *fp);
int prl_vtg_create_drawable(struct vtg_filp_private *fp, unsigned int id);
int prl_vtg_destroy_drawable(struct vtg_filp_private *fp, unsigned int id);
int prl_vtg_clip_drawable(struct vtg_filp_private *fp, struct draw_bdesc *hdr);
int prl_vtg_user_to_host_request(struct tg_dev *dev, void *ureq, struct vtg_filp_private *fp);
int prl_vtg_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
void prl_vtg_remove(struct pci_dev *pdev);
void prl_vtg_init_module(void);
#ifdef CONFIG_PM
int prl_vtg_suspend(struct pci_dev *pdev, pm_message_t state);
int prl_vtg_resume(struct pci_dev *pdev);
#endif

#if PRL_DRM_ENABLED
int prl_drm_probe(struct pci_dev *pdev, const struct pci_device_id *ent);
void prl_drm_remove(struct pci_dev *pdev);
void prl_drm_init_module(void);
#ifdef CONFIG_PM
int prl_drm_suspend(struct pci_dev *pdev, pm_message_t state);
int prl_drm_resume(struct pci_dev *pdev);
#endif
#endif
#endif
