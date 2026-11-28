// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 frame buffer driver
 *
 * Copyright (C) 2019 Fredrik Noring
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/mm.h>
#include <linux/module.h>
#include <linux/of_device.h>
#include <linux/platform_device.h>
#include <linux/string.h>
#include <linux/uaccess.h>

#include <drm/drm_atomic_helper.h>
#include <drm/drm_debugfs.h>
#include <drm/drm_drv.h>
#include <drm/drm_file.h>
#include <drm/drm_gem.h>
#include <drm/drm_vblank.h>
#include <drm/drm_gem_framebuffer_helper.h>
#include <drm/drm_gem_shmem_helper.h>
#include <drm/drm_ioctl.h>
#include <drm/drm_of.h>
#include <drm/drm_simple_kms_helper.h>
#include <drm/drm_probe_helper.h>
#include <linux/fb.h>
#include <drm/drm_print.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_crtc_helper.h>

#include <asm/io.h>

#include <asm/mach-ps2/dmac.h>
#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/irq.h>
#include <asm/mach-ps2/gs.h>
#include <asm/mach-ps2/gs-registers.h>

#include <uapi/asm/gif.h>
#include <uapi/asm/gs.h>

#include "gs_connector.h"
#include "gs_drm.h"
#include "gs_environment.h"
#include "gs_gem.h"
#include "gs_mode.h"
#include "gs_pan.h"
#include "gs_pipe.h"
#include "gs_tile.h"

#define DEVICE_NAME "gs-drm"

DEFINE_DRM_GEM_FOPS(gs_driver_fops);

/* Module parameters */
static __maybe_unused char *video_option;
#if 0
static char *mode_margin = "";
#endif

struct ps2fb_par *info_to_ps2par(const struct fb_info *info)
{
	return &((struct gs_device *)info->par)->ps2par;
}

/**
 * gs_var_to_fbw - frame buffer width for a given virtual x resolution
 * @var: screen info object to compute FBW for
 *
 * Return: frame buffer width (FBW) in 64-pixel unit
 */
u32 gs_var_to_fbw(const struct fb_var_screeninfo *var)
{
	/*
	 * Round up to nearest GS_FB_PAGE_WIDTH (64 px) since there are
	 * valid resolutions such as 720 px that do not divide 64 properly.
	 */
	return (var->xres_virtual + GS_FB_PAGE_WIDTH - 1) / GS_FB_PAGE_WIDTH;
}

/**
 * gs_var_to_psm - frame buffer pixel storage mode for a given bits per pixel
 * @var: screen info object to compute PSM for
 *
 * Return: frame buffer pixel storage mode
 */
enum gs_psm gs_var_to_psm(const struct fb_var_screeninfo *var)
{
	if (var->bits_per_pixel == 1)
		return gs_psm_ct16;
	if (var->bits_per_pixel == 16)
		return gs_psm_ct16;
	if (var->bits_per_pixel == 32)
		return gs_psm_ct32;

	DRM_WARN_ONCE("%s: Unsupported bits per pixel %u\n",
		__func__, var->bits_per_pixel);

	return gs_psm_ct32;
}

/**
 * var_to_block_count - number of frame buffer blocks for a given video mode
 * @var: screen info object to compute the number of blocks for
 *
 * The Graphics Synthesizer frame buffer is subdivided into rectangular pages,
 * from left to right, top to bottom. Pages are further subdivided into blocks,
 * with different arrangements for PSMCT16 and PSMCT32. Blocks are further
 * subdivided into columns, which are finally subdivided into pixels.
 *
 * The video display buffer, textures and palettes share the same frame buffer.
 * This function can be used to compute the first free block after the video
 * display buffer.
 *
 * Return: number of blocks, or zero for unsupported pixel storage modes
 */
static u32 var_to_block_count(const struct fb_info *info)
{
	const struct fb_var_screeninfo *var = &info->var;
	const enum gs_psm psm = gs_var_to_psm(var);
	const u32 fbw = gs_var_to_fbw(var);

	if (psm == gs_psm_ct16)
		return gs_psm_ct16_block_count(fbw, var->yres_virtual);
	if (psm == gs_psm_ct32)
		return gs_psm_ct32_block_count(fbw, var->yres_virtual);

	DRM_WARN_ONCE("%s: Unsupported pixel storage mode %u\n",
		__func__, psm);

	return 0;
}

/**
 * gs_color_base_pointer - colour base pointer
 * @info: frame buffer info object
 *
 * Return: block index of the colour palette, that follows the display buffer
 */
u32 gs_color_base_pointer(const struct fb_info *info)
{
	const struct ps2fb_par *par = info_to_ps2par(info);

	return par->cb.block_count;
}

/**
 * invalidate_palette - set invalid palette indices to force update
 * @par: driver specific object
 *
 * The background and foreground palette indices will mismatch in the next
 * tile file, and thereby force a palette update.
 */
static void invalidate_palette(struct ps2fb_par *par)
{
	par->cb.bg = ~0;
	par->cb.fg = ~0;
}

static int gs_fb_setcolreg(unsigned regno, unsigned red, unsigned green,
	unsigned blue, unsigned transp, struct fb_info *info)
{
	const struct gs_rgba32 color = {
		.r = red    >> 8,
		.g = green  >> 8,
		.b = blue   >> 8,
		.a = transp >> 8
	};
	struct ps2fb_par *par = info_to_ps2par(info);
	unsigned long flags;

	if (regno >= PALETTE_SIZE)
		return -EINVAL;

	spin_lock_irqsave(&par->lock, flags);

	par->pseudo_palette[regno] = color;
	invalidate_palette(par);

	spin_unlock_irqrestore(&par->lock, flags);

	return 0;
}

/**
 * clear_screen - clear the displayed buffer screen
 * @info: frame buffer info object
 */
static void clear_screen(struct fb_info *info)
{
	struct ps2fb_par *par = info_to_ps2par(info);
	union package * const base_package = par->package.buffer;
	union package *package = base_package;

	if (!gif_wait()) {
		DRM_ERROR("Failed to clear the screen, GIF is busy\n");
		return;
	}

	GIF_PACKAGE_TAG(package) {
		.flg = gif_reglist_mode,
		.reg0 = gif_reg_prim,
		.reg1 = gif_reg_rgbaq,
		.reg2 = gif_reg_xyz2,
		.reg3 = gif_reg_xyz2,
		.nreg = 4,
		.nloop = 1,
		.eop = 1
	};
	GIF_PACKAGE_REG(package) {
		.lo.prim = { .prim = gs_sprite },
		.hi.rgbaq = { .a = GS_ALPHA_ONE }
	};
	GIF_PACKAGE_REG(package) {
		.lo.xyz2 = {
			.x = gs_fbcs_to_pcs(0),
			.y = gs_fbcs_to_pcs(0)
		},
		.hi.xyz2 = {
			.x = gs_fbcs_to_pcs(info->var.xres_virtual),
			.y = gs_fbcs_to_pcs(info->var.yres_virtual)
		}
	};

	gif_write(&base_package->gif, package - base_package);
}

static int gs_set_par(struct fb_info *info)
{
	printk("drm: %s %u %u %u %u %u %u %u %u %u\n",
		__func__,
		info->var.xres,
		info->var.yres,
		info->var.pixclock,
		info->var.left_margin,
		info->var.right_margin,
		info->var.upper_margin,
		info->var.lower_margin,
		info->var.hsync_len,
		info->var.vsync_len);

	write_cb_environment(info);
	return 0;
}

static int gs_check_var(struct fb_var_screeninfo *var,
	struct fb_info *info)
{
	printk("drm: %s %u %u %u %u %u %u %u %u %u\n",
		__func__,
		var->xres,
		var->yres,
		var->pixclock,
		var->left_margin,
		var->right_margin,
		var->upper_margin,
		var->lower_margin,
		var->hsync_len,
		var->vsync_len);

	return 0;
}

static int init_console_buffer(struct fb_info *info)
{
	static struct fb_ops fbops = {
		.owner		= THIS_MODULE,
		.fb_setcolreg	= gs_fb_setcolreg,
		.fb_set_par	= gs_set_par,
		.fb_check_var	= gs_check_var,
	};

	struct ps2fb_par *par = info_to_ps2par(info);

	fb_info(info, "Graphics Synthesizer console frame buffer device\n");

	info->screen_size = 0;
	info->screen_base = NULL;	/* mmap is unsupported by hardware */

	info->fix.smem_start = 0;	/* GS frame buffer is local memory */
	info->fix.smem_len = GS_MEMORY_SIZE;

	strscpy(info->fix.id, "PS2 GS", ARRAY_SIZE(info->fix.id));

	info->flags = 0;
	info->fbops = &fbops;

	gs_pan_init(info);
	gs_tile_init(info);

	info->pseudo_palette = par->pseudo_palette;

	return 0;
}

static struct drm_driver gs_driver = {
	.driver_features = DRIVER_GEM | DRIVER_MODESET | DRIVER_ATOMIC,

	.fops			 = &gs_driver_fops,
	DRM_GEM_SHMEM_DRIVER_OPS,
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

	printk("drm: %s\n", __func__);

	gs = devm_drm_dev_alloc(&pdev->dev, &gs_driver, struct gs_device, dev);
	if (IS_ERR(gs)) {
		dev_err(&pdev->dev, "gs device allocation failed\n");
		err = PTR_ERR(gs);
		goto err_gs_alloc;
	}

	spin_lock_init(&gs->ps2par.lock);

	err = gs_vram_init(&gs->vram);
	if (err)
		goto err_gs_alloc;

	gs->ps2par.package.buffer = (union package *)__get_free_page(GFP_DMA);
	if (!gs->ps2par.package.buffer) {
		dev_err(&pdev->dev, "Failed to allocate package buffer\n");
		err = -ENOMEM;
		goto err_package_buffer;
	}
	gs->ps2par.package.capacity = PAGE_SIZE / sizeof(union package);

	gs->info = framebuffer_alloc(0, &pdev->dev);
	if (!gs->info) {
		dev_err(&pdev->dev, "framebuffer_alloc failed\n");
		err = -ENOMEM;
		goto err_framebuffer_alloc;
	}
	err = fb_alloc_cmap(&gs->info->cmap, PALETTE_SIZE, 0);
	if (err) {
		dev_err(&pdev->dev, "fb_alloc_cmap failed with %d\n", err);
		goto err_fb_alloc_cmap;
	}
	INIT_LIST_HEAD(&gs->info->modelist);
	gs->info->var = (struct fb_var_screeninfo) { };
	gs->info->skip_vt_switch = true;

	gs->info->par = gs;
	err = init_console_buffer(gs->info);
	if (err)
		goto err_init_buffer;
	gs->ps2par.cb.block_count = var_to_block_count(gs->info);
	write_cb_environment(gs->info);
	write_cb_pan_display(&gs->info->var, gs->info);
	clear_screen(gs->info);

	dev = &gs->dev;
	dev->dev_private = gs;

	gs_mode_config_init(&gs->dev);

	err = drm_vblank_init(dev, 1);
	if (err)
		goto err_no_vsync;

	err = request_irq(IRQ_GS_VSYNC, gs_vsync_irq, 0, "gs-vsync", gs);
	if (err)
		goto err_no_vsync;

	err = gs_connector_init(dev, &gs->connector);
	if (err < 0)
		goto err_connector_init;

	err = gs_pipe_init(gs);
	if (err < 0)
		goto err_pipe_init;

	drm_mode_config_reset(dev);

	/*
	 * The GIF asserts DMA completion interrupts that are uninteresting
	 * because DMA operations are fast enough to busy-wait for.
	 */
	disable_irq(IRQ_DMAC_GIF);

	err = drm_dev_register(dev, 0);
	if (err)
		goto err_dev_register;

	platform_set_drvdata(pdev, gs);

	return 0;

err_dev_register:
	enable_irq(IRQ_DMAC_GIF);
	goto err_free_vsync;
err_pipe_init:
	/* FIXME: drm_mode_config_cleanup(dev); */
err_free_vsync:
	free_irq(IRQ_GS_VSYNC, gs);
err_no_vsync:
err_connector_init:
err_init_buffer:
err_fb_alloc_cmap:
err_framebuffer_alloc:
err_package_buffer:
	gs_vram_fini(&gs->vram);
	free_page((unsigned long)gs->ps2par.package.buffer);
err_gs_alloc:
	return err;
}

static void gs_remove(struct platform_device *pdev)
{
	struct drm_device *dev = platform_get_drvdata(pdev);
	struct gs_device *gs = dev->dev_private;

	printk("drm: %s\n", __func__);

	free_irq(IRQ_GS_VSYNC, gs);

	if (dev != NULL) {
		drm_dev_unregister(dev);
		drm_mode_config_cleanup(dev);
	}

	if (!gif_wait())
		dev_err(dev->dev, "Failed to complete GIF DMA transfer\n");

	free_page((unsigned long)gs->ps2par.package.buffer);
	gs_vram_fini(&gs->vram);
}

static struct platform_driver gs_platform_driver = {
	.probe		= gs_probe,
	.remove		= gs_remove,
	.driver = {
		.name	= DEVICE_NAME,
	},
};

static struct platform_device *gs_platform_device;

static int __init gs_drm_init(void)
{
	int err;

	printk("drm: %s\n", __func__);

	/* Default to a suitable PAL or NTSC broadcast mode. */
	if (!video_option)
		video_option = gs_region_pal() ? "576x460@50i" : "576x384@60i";

	gs_platform_device = platform_device_alloc("gs_drm", 0);
	if (!gs_platform_device)
		return -ENOMEM;

	err = platform_device_add(gs_platform_device);
	if (err < 0) {
		platform_device_put(gs_platform_device);
		return err;
	}

	return platform_driver_register(&gs_platform_driver);
}

static void __exit gs_drm_exit(void)
{
	printk("drm: %s\n", __func__);

	platform_driver_unregister(&gs_platform_driver);
	platform_device_unregister(gs_platform_device);
}

module_init(gs_drm_init);
module_exit(gs_drm_exit);

MODULE_DESCRIPTION("PlayStation 2 Graphics Synthesizer");
MODULE_AUTHOR("Fredrik Noring");
MODULE_LICENSE("GPL v2");
