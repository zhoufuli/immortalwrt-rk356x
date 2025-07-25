/*
 * Copyright (c) [2020], MediaTek Inc. All rights reserved.
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws.
 * The information contained herein is confidential and proprietary to
 * MediaTek Inc. and/or its licensors.
 * Except as otherwise provided in the applicable licensing terms with
 * MediaTek Inc. and/or its licensors, any reproduction, modification, use or
 * disclosure of MediaTek Software, and information contained herein, in whole
 * or in part, shall be strictly prohibited.
*/
/*
 ***************************************************************************
 ***************************************************************************

    Module Name:
    rbus_prop_dev.c

    Abstract:
    Create and register network interface for RBUS based chipsets in linux platform.

    Revision History:
    Who         When            What
    --------    ----------      ----------------------------------------------
*/
#ifdef RTMP_RBUS_SUPPORT
#define RTMP_MODULE_OS

#include "rt_config.h"

#if defined(CONFIG_RA_CLASSIFIER) && (!defined(CONFIG_RA_CLASSIFIER_MODULE))
extern int (*ra_classifier_init_func)(void);
extern void (*ra_classifier_release_func)(void);
extern struct proc_dir_entry *proc_ptr, *proc_ralink_wl_video;
#endif

#ifdef MULTI_INTR_SUPPORT
extern unsigned int multi_intr_2nd;
extern unsigned int multi_intr_3rd;
extern unsigned int multi_intr_4th;
#endif

static struct pci_device_id mt_rbus_tbl[] DEVINITDATA = {

#ifdef MT7986
	{PCI_DEVICE(0x14c3, 0x7986)},
#endif /* MT7986 */
#ifdef MT7981
	{PCI_DEVICE(0x14c3, 0x7981)},
#endif /* MT7981 */

	{} /* terminate list */
};

MODULE_DEVICE_TABLE(pci, mt_rbus_tbl);


#define RBUS_TSSI_CTRL_OFFSET 0x34
#define RBUS_PA_LNA_CTRL_OFFSET 0x38

int rbus_tssi_set(struct _RTMP_ADAPTER *ad, UCHAR mode)
{
	struct pci_dev *dev = ((POS_COOKIE)ad->OS_Cookie)->pci_dev;

	pci_write_config_byte(dev, RBUS_TSSI_CTRL_OFFSET, mode);
	return 0;
}

int rbus_pa_lna_set(struct _RTMP_ADAPTER *ad, UCHAR mode)
{
	struct pci_dev *dev = ((POS_COOKIE)ad->OS_Cookie)->pci_dev;

	pci_write_config_byte(dev, RBUS_PA_LNA_CTRL_OFFSET, mode);
	return 0;
}

static int DEVINIT mt_rbus_probe(struct pci_dev *pdev, const struct pci_device_id *pci_id)
{
	struct net_device *net_dev;
	ULONG csr_addr;
	INT rv = 0;
	void *handle = NULL;
	RTMP_ADAPTER *pAd = NULL;
	RTMP_OS_NETDEV_OP_HOOK netDevHook;
	struct _PCI_HIF_T *pci_hif;
	struct pci_hif_chip *hif_chip = NULL;
	struct pci_hif_chip_cfg cfg;

	MTWF_DBG(pAd, DBG_CAT_HIF, CATHIF_PCI, DBG_LVL_INFO, "===> rt2880_probe\n");

	if (!dma_set_mask(&pdev->dev, DMA_BIT_MASK(32))) {
		/*
		 * pci_set_consistent_dma_mask() will always be able to set the same
		 * or a smaller mask as pci_set_dma_mask()
		 */
		dma_set_coherent_mask(&pdev->dev, DMA_BIT_MASK(32));
	} else {
		MTWF_DBG(pAd, DBG_CAT_HIF, CATHIF_PCI, DBG_LVL_ERROR,
				 "set DMA mask failed!errno=%d\n", rv);
		goto err_out;
	}

	/* map physical address to virtual address for accessing register */
	csr_addr = (unsigned long)ioremap(pci_resource_start(pdev, 0), pci_resource_len(pdev, 0));
	/* Allocate RTMP_ADAPTER adapter structure */
	os_alloc_mem(NULL, (UCHAR **)&handle, sizeof(struct os_cookie));

	if (!handle) {
		MTWF_DBG(pAd, DBG_CAT_HIF, CATHIF_PCI, DBG_LVL_ERROR, "Allocate memory for os_cookie failed!\n");
		goto err_out;
	}

	os_zero_mem(handle, sizeof(struct os_cookie));
#ifdef OS_ABL_FUNC_SUPPORT
	/* get DRIVER operations */
	RTMP_DRV_OPS_FUNCTION(pRtmpDrvOps, NULL, NULL, NULL);
#endif /* OS_ABL_FUNC_SUPPORT */
	rv = RTMPAllocAdapterBlock(handle, (VOID **)&pAd, RTMP_DEV_INF_RBUS);

	if (rv != NDIS_STATUS_SUCCESS) {
		MTWF_DBG(pAd, DBG_CAT_HIF, CATHIF_PCI, DBG_LVL_ERROR, " RTMPAllocAdapterBlock !=  NDIS_STATUS_SUCCESS\n");
		os_free_mem(handle);
		goto err_out;
	}
	pci_hif = hc_get_hif_ctrl(pAd->hdev_ctrl);
	/* Here are the RTMP_ADAPTER structure with rbus-bus specific parameters. */
	pci_hif->CSRBaseAddress = (PUCHAR)csr_addr;
	pAd->ChipID = pci_id->device;
	/*is not a regular method*/
	((POS_COOKIE)handle)->pci_dev = (VOID *)pdev;
	((POS_COOKIE)handle)->pDev = &pdev->dev;
	RtmpRaDevCtrlInit(pAd, RTMP_DEV_INF_RBUS);
	net_dev = RtmpPhyNetDevInit(pAd, &netDevHook);

	if (net_dev == NULL)
		goto err_out_free_radev;

	/* Here are the net_device structure with pci-bus specific parameters. */
	net_dev->irq = pdev->irq;			/* Interrupt IRQ number */
	net_dev->base_addr = csr_addr;		/* Save CSR virtual address and irq to device structure */

	os_zero_mem(&cfg, sizeof(struct pci_hif_chip_cfg));
	cfg.csr_addr = csr_addr;
	cfg.msi_en = FALSE;
	cfg.device = &pdev->dev;
	cfg.device_id = pci_id->device;
	cfg.irq = pdev->irq;

#ifdef MULTI_INTR_SUPPORT
	if (multi_intr_2nd > 0 && multi_intr_3rd > 0 && multi_intr_4th > 0) {
		cfg.multi_intr_2nd = multi_intr_2nd;
		cfg.multi_intr_3rd = multi_intr_3rd;
		cfg.multi_intr_4th = multi_intr_4th;
	}
#endif
	pci_hif_chip_init((VOID **)&hif_chip, &cfg);
	pci_set_drvdata(pdev, hif_chip);
	pci_hif->main_hif_chip = hif_chip;
	pci_hif->net_dev = net_dev;
	hif_chip->hif = pci_hif;

#ifdef CONFIG_STA_SUPPORT
	pAd->StaCfg[0].OriDevType = net_dev->type;
#endif /* CONFIG_STA_SUPPORT */
	RTMP_DRIVER_CHIP_PREPARE(pAd);
	/*All done, it's time to register the net device to kernel. */
	/* Register this device */
	rv = RtmpOSNetDevAttach(pAd->OpMode, net_dev, &netDevHook);

	if (rv) {
		MTWF_DBG(pAd, DBG_CAT_HIF, CATHIF_PCI, DBG_LVL_ERROR, "failed to call RtmpOSNetDevAttach(), rv=%d!\n", rv);
		goto err_out_free_netdev;
	}
	MTWF_DBG(pAd, DBG_CAT_HIF, CATHIF_PCI, DBG_LVL_INFO, "%s: at CSR addr 0x%lx, IRQ %ld.\n", net_dev->name, (ULONG)csr_addr, (long int)net_dev->irq);
	MTWF_DBG(pAd, DBG_CAT_HIF, CATHIF_PCI, DBG_LVL_INFO, "<=== %s()\n", __func__);
#if defined(CONFIG_RA_CLASSIFIER) && (!defined(CONFIG_RA_CLASSIFIER_MODULE))
	proc_ptr = proc_ralink_wl_video;

	if (ra_classifier_init_func != NULL)
		ra_classifier_init_func();

#endif
#ifdef MTK_FE_RESET_RECOVER
	mtk_fe_reset_notifier_init(pAd);
#endif
#ifdef WF_RESET_SUPPORT
        rv = wf_reset_init(pAd);
        if (!rv)
                goto err_out_free_netdev;
#endif

	return 0;
err_out_free_netdev:
	RtmpOSNetDevFree(net_dev);
err_out_free_radev:
	/* free RTMP_ADAPTER strcuture and os_cookie*/
	RTMPFreeAdapter(pAd);
err_out:
	return -ENODEV;
}

static VOID DEVEXIT mt_rbus_remove(struct pci_dev *pci_dev)
{
	struct pci_hif_chip *hif_chip = pci_get_drvdata(pci_dev);
	struct net_device *net_dev = hif_chip->hif->net_dev;
	RTMP_ADAPTER *pAd;
	ULONG csr_addr = 0;
#ifdef DBDC_ONE_BAND1_SUPPORT
	struct _RTMP_CHIP_CAP *cap;
#endif

	if (net_dev == NULL)
		return;
	csr_addr = net_dev->base_addr;

	/* pAd = net_dev->priv; */
	GET_PAD_FROM_NET_DEV(pAd, net_dev);
#ifdef DBDC_ONE_BAND1_SUPPORT
	if (pAd != NULL)
		cap = hc_get_chip_cap(pAd->hdev_ctrl);
#endif
	if (pAd != NULL) {
#ifdef WF_RESET_SUPPORT
		wf_reset_exit(pAd);
#endif
#ifdef MTK_FE_RESET_RECOVER
		mtk_fe_reset_notifier_exit(pAd);
#endif
		RtmpPhyNetDevExit(pAd, net_dev);
		RtmpRaDevCtrlExit(pAd);
	} else {
		RtmpOSNetDevDetach(net_dev);
	}
	/* Unmap CSR base address */
	iounmap((char *)(csr_addr));
	/* release memory region */
	pci_release_regions(pci_dev);
#ifdef DBDC_ONE_BAND1_SUPPORT
	/* main netdev freed in MBSS_REMOVE */
	if (!cap->DbdcOneBand1Support)
#endif
		/* Free the root net_device. */
		RtmpOSNetDevFree(net_dev);
	/*free hif_chip*/
	pci_hif_chip_exit(hif_chip);
#if defined(CONFIG_RA_CLASSIFIER) && (!defined(CONFIG_RA_CLASSIFIER_MODULE))
	proc_ptr = proc_ralink_wl_video;

	if (ra_classifier_release_func != NULL)
		ra_classifier_release_func();

#endif
}

/*
 *	Our PCI driver structure
 */
static struct pci_driver mt_rbus_driver = {
name:
	"mt_rbus",
id_table :
	mt_rbus_tbl,
probe :
	mt_rbus_probe,
remove :
	DEVEXIT_P(mt_rbus_remove),
};


/*
 *	Driver module load/unload function
 */
int __init wbsys_module_init(void)
{
	MTWF_DBG(NULL, DBG_CAT_HIF, CATHIF_PCI, DBG_LVL_INFO, "register %s\n", RTMP_DRV_NAME);
#ifndef MULTI_INF_SUPPORT
	os_module_init();
#endif
	return pci_register_driver(&mt_rbus_driver);
}


void __exit wbsys_module_exit(void)
{
	pci_unregister_driver(&mt_rbus_driver);
 #ifndef MULTI_INF_SUPPORT
	os_module_exit();
#endif
}

/** @} */
/** @} */

#ifndef MULTI_INF_SUPPORT
module_init(wbsys_module_init);
module_exit(wbsys_module_exit);
#endif /* MULTI_INF_SUPPORT */

#endif /* RTMP_RBUS_SUPPORT */

