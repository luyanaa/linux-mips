// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM driver
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/platform_device.h>

#include <drm/clients/drm_client_setup.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_drv.h>
#include <drm/drm_fbdev_shmem.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_print.h>
#include <drm/drm_probe_helper.h>
#include <drm/drm_vblank.h>

#include <asm/mach-ps2/dmac.h>
#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs-registers.h>
#include <asm/mach-ps2/irq.h>

#include "gs_connector.h"
#include "gs_drm.h"
#include "gs_cmd.h"
#include "gs_gem.h"
#include "gs_ioctl.h"
#include "gs_mode.h"
#include "gs_pipe.h"

/* 512 KiB staging buffer, enough for a maximum-size GIF image packet. */
#define GS_PACKAGE_ORDER 7

#define DEVICE_NAME "gs-drm"

DEFINE_DRM_GEM_FOPS(gs_driver_fops);

static struct drm_driver gs_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,

	.fops			 = &gs_driver_fops,
	.ioctls			 = gs_ioctls,
	.num_ioctls		 = GS_IOCTL_COUNT,
	DRM_GEM_SHMEM_DRIVER_OPS,
	DRM_FBDEV_SHMEM_DRIVER_OPS,
	.gem_create_object	 = gs_gem_create_object,

	.name		 = "gs",
	.desc		 = "PlayStation 2 Graphics Synthesizer",
	.major		 = 1,
	.minor		 = 0,
};

static irqreturn_t gs_vsync_irq(int irq, void *arg)
{
	struct gs_device *gs = arg;

	drm_crtc_handle_vblank(&gs->crtc);
	return IRQ_HANDLED;
}

static int gs_probe(struct platform_device *pdev)
{
	struct drm_device *dev;
	struct gs_device *gs;
	int err;

	gs = devm_drm_dev_alloc(&pdev->dev, &gs_driver, struct gs_device, dev);
	if (IS_ERR(gs)) {
		dev_err(&pdev->dev, "gs device allocation failed\n");
		return PTR_ERR(gs);
	}

	err = gs_vram_init(&gs->vram);
	if (err)
		return err;

	err = gs_submit_init(gs);
	if (err)
		goto err_vram;

	/*
	 * A GIF image tag can carry up to 32767 quadwords, so size the
	 * staging buffer for the largest packet and fall back to smaller
	 * orders if high-order DMA memory is unavailable.
	 */
	for (gs->package.order = GS_PACKAGE_ORDER;
	     gs->package.order > 0; gs->package.order--) {
		gs->package.buffer = (union package *)
			__get_free_pages(GFP_DMA | __GFP_NOWARN,
							 gs->package.order);
		if (gs->package.buffer)
			break;
	}
	if (!gs->package.buffer)
		gs->package.buffer = (union package *)__get_free_page(GFP_DMA);
	if (!gs->package.buffer) {
		dev_err(&pdev->dev, "Failed to allocate package buffer\n");
		err = -ENOMEM;
		goto err_vram;
	}
	gs->package.capacity = (PAGE_SIZE << gs->package.order) /
			       sizeof(union package);

	dev = &gs->dev;
	dev->dev_private = gs;

	gs_mode_config_init(dev);

	err = drm_vblank_init(dev, 1);
	if (err)
		goto err_mode_config;

	err = gs_connector_init(dev, &gs->connector);
	if (err < 0)
		goto err_mode_config;

	err = gs_pipe_init(gs);
	if (err < 0)
		goto err_mode_config;

	/*
	 * The GIF asserts DMA completion interrupts that are uninteresting
	 * because DMA operations are fast enough to busy-wait for.
	 */
	disable_irq(IRQ_DMAC_GIF);

	err = request_irq(IRQ_GS_VSYNC, gs_vsync_irq, 0, "gs-vsync", gs);
	if (err)
		goto err_enable_gif_irq;

	drm_mode_config_reset(dev);

	err = drm_dev_register(dev, 0);
	if (err)
		goto err_free_vsync;

	drm_client_setup(dev, NULL);

	platform_set_drvdata(pdev, gs);

	return 0;

err_free_vsync:
	free_irq(IRQ_GS_VSYNC, gs);
err_enable_gif_irq:
	enable_irq(IRQ_DMAC_GIF);
err_mode_config:
	drm_mode_config_cleanup(dev);
	free_pages((unsigned long)gs->package.buffer, gs->package.order);
err_vram:
	gs_vram_fini(&gs->vram);
	return err;
}

static void gs_remove(struct platform_device *pdev)
{
	struct gs_device *gs = platform_get_drvdata(pdev);
	struct drm_device *dev = &gs->dev;

	free_irq(IRQ_GS_VSYNC, gs);
	drm_dev_unregister(dev);
	drm_mode_config_cleanup(dev);

	if (!gif_wait())
		dev_err(&pdev->dev, "Failed to complete GIF DMA transfer\n");

	enable_irq(IRQ_DMAC_GIF);

	free_pages((unsigned long)gs->package.buffer, gs->package.order);
	gs_vram_fini(&gs->vram);
}

static struct platform_driver gs_platform_driver = {
	.probe		= gs_probe,
	.remove		= gs_remove,
	.driver = {
		.name	= DEVICE_NAME,
	},
};

module_platform_driver(gs_platform_driver);

MODULE_DESCRIPTION("PlayStation 2 Graphics Synthesizer");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL v2");
