// SPDX-License-Identifier: GPL-2.0-only

#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/pm.h>
#include <linux/slab.h>

#include "gc555.h"

#define GC555_PCI_VENDOR_ID		0x1461
#define GC555_PCI_DEVICE_ID		0x0054
#define GC555_PCI_SUBVENDOR_ID		0x1461
#define GC555_PCI_SUBDEVICE_ID		0x5550
#define GC555_BRIDGE_BAR		0

static int gc555_probe(struct pci_dev *pdev,
		       const struct pci_device_id *id)
{
	struct gc555_dev *gc555;
	void __iomem *regs;
	int ret;

	ret = pcim_enable_device(pdev);
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "failed to enable PCI device\n");

	ret = dma_set_mask_and_coherent(&pdev->dev, DMA_BIT_MASK(32));
	if (ret)
		return dev_err_probe(&pdev->dev, ret,
				     "32-bit DMA is unavailable\n");

	regs = pcim_iomap_region(pdev, GC555_BRIDGE_BAR, "gc555");
	if (IS_ERR(regs))
		return dev_err_probe(&pdev->dev, PTR_ERR(regs),
				     "failed to map BAR0\n");

	gc555 = devm_kzalloc(&pdev->dev, sizeof(*gc555), GFP_KERNEL);
	if (!gc555)
		return -ENOMEM;

	gc555->dev = &pdev->dev;
	gc555->pdev = pdev;
	pci_set_drvdata(pdev, gc555);
	pci_set_master(pdev);

	ret = gc555_bridge_init(gc555, regs,
				pci_resource_len(pdev, GC555_BRIDGE_BAR));
	if (ret)
		goto clear_master;

	ret = gc555_fpga_init(gc555);
	if (ret)
		goto cleanup_bridge;

	ret = gc555_link_init(gc555);
	if (ret)
		goto cleanup_fpga;

	ret = gc555_i2c_init(gc555);
	if (ret)
		goto cleanup_fpga;

	ret = gc555_led_init(gc555);
	if (ret)
		dev_warn(gc555->dev,
			 "RGB lighting unavailable: %d\n", ret);

	ret = gc555_it6664_init(gc555);
	if (ret)
		goto cleanup_led;

	ret = gc555_it6805_init(gc555);
	if (ret)
		goto cleanup_it6664;

	ret = gc555_dma_init(gc555);
	if (ret)
		goto cleanup_it6805;

	ret = gc555_video_dma_init(gc555);
	if (ret)
		goto cleanup_dma;

	ret = gc555_audio_init(gc555);
	if (ret)
		goto cleanup_video_dma;

	ret = gc555_video_init(gc555);
	if (ret)
		goto cleanup_audio;

	dev_info(&pdev->dev,
		 "hardware transport, HDMI chips, and capture endpoints initialized\n");
	return 0;

cleanup_audio:
	gc555_audio_cleanup(gc555);
cleanup_video_dma:
	gc555_video_dma_cleanup(gc555);
cleanup_dma:
	gc555_dma_cleanup(gc555);
cleanup_it6805:
	gc555_it6805_cleanup(gc555);
cleanup_it6664:
	gc555_it6664_cleanup(gc555);
cleanup_led:
	gc555_led_cleanup(gc555);
	gc555_i2c_cleanup(gc555);
cleanup_fpga:
	gc555_fpga_cleanup(gc555);
cleanup_bridge:
	gc555_bridge_cleanup(gc555);
clear_master:
	pci_clear_master(pdev);
	return ret;
}

static void gc555_remove(struct pci_dev *pdev)
{
	struct gc555_dev *gc555 = pci_get_drvdata(pdev);

	gc555_it6805_suspend(gc555);
	gc555_it6664_suspend(gc555);
	gc555_led_suspend(gc555);

	/* Surprise removal has already made BAR0 unsafe to access. */
	if (!gc555_bridge_is_accessible(gc555)) {
		gc555_dma_mark_device_lost(gc555);
		gc555_bridge_mark_disconnected(gc555);
	}

	gc555_video_cleanup(gc555);
	gc555_audio_cleanup(gc555);
	gc555_video_dma_cleanup(gc555);
	gc555_dma_cleanup(gc555);
	gc555_it6805_cleanup(gc555);
	gc555_it6664_cleanup(gc555);
	gc555_led_cleanup(gc555);
	gc555_i2c_cleanup(gc555);
	gc555_fpga_cleanup(gc555);
	gc555_bridge_cleanup(gc555);
	pci_clear_master(pdev);
}

static int gc555_suspend(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct gc555_dev *gc555 = pci_get_drvdata(pdev);

	gc555_video_suspend(gc555);
	gc555_audio_suspend(gc555);
	gc555_it6805_suspend(gc555);
	gc555_it6664_suspend(gc555);
	gc555_led_suspend(gc555);
	gc555_bridge_suspend(gc555);

	return 0;
}

static int gc555_resume(struct device *dev)
{
	struct pci_dev *pdev = to_pci_dev(dev);
	struct gc555_dev *gc555 = pci_get_drvdata(pdev);
	int ret;

	pci_set_master(pdev);
	ret = gc555_bridge_resume(gc555);
	if (ret)
		return ret;
	ret = gc555_link_init(gc555);
	if (ret)
		goto suspend_bridge;
	ret = gc555_led_resume(gc555);
	if (ret)
		dev_warn(gc555->dev,
			 "RGB lighting resume failed: %d\n", ret);
	ret = gc555_it6664_resume(gc555);
	if (ret)
		goto suspend_led;
	ret = gc555_it6805_resume(gc555);
	if (ret)
		goto suspend_it6664;
	ret = gc555_bridge_resume_complete(gc555);
	if (ret)
		goto suspend_it6805;

	gc555_audio_resume(gc555);
	gc555_video_resume(gc555);
	return 0;

suspend_it6805:
	gc555_it6805_suspend(gc555);
suspend_it6664:
	gc555_it6664_suspend(gc555);
suspend_led:
	gc555_led_suspend(gc555);
suspend_bridge:
	gc555_bridge_suspend(gc555);
	dev_err(dev, "resume sequence failed: %d\n", ret);
	return ret;
}

static DEFINE_SIMPLE_DEV_PM_OPS(gc555_pm_ops, gc555_suspend, gc555_resume);

static const struct pci_device_id gc555_pci_ids[] = {
	{ PCI_DEVICE_SUB(GC555_PCI_VENDOR_ID, GC555_PCI_DEVICE_ID,
			 GC555_PCI_SUBVENDOR_ID, GC555_PCI_SUBDEVICE_ID) },
	{ }
};
MODULE_DEVICE_TABLE(pci, gc555_pci_ids);

static struct pci_driver gc555_pci_driver = {
	.name = "gc555",
	.id_table = gc555_pci_ids,
	.probe = gc555_probe,
	.remove = gc555_remove,
	.driver.pm = pm_sleep_ptr(&gc555_pm_ops),
};
module_pci_driver(gc555_pci_driver);

MODULE_DESCRIPTION("AVerMedia Live Gamer BOLT GC555 capture driver");
MODULE_LICENSE("GPL");
