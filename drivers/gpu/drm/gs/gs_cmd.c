// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer validated command submission
 *
 * Copyright (C) 2026 Fredrik Noring
 *
 * User space may submit GIF display lists, but only display-list-safe GS
 * registers are accepted. FRAME/ZBUF and the PCRTC registers are driver
 * owned; resource addresses are patched in from kernel-private VRAM blocks.
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/mutex.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/vmalloc.h>

#include <drm/drm_device.h>
#include <drm/drm_file.h>
#include <drm/drm_fourcc.h>
#include <drm/drm_framebuffer.h>
#include <drm/drm_gem.h>
#include <drm/drm_gem_shmem_helper.h>

#include <asm/mach-ps2/gif.h>
#include <asm/mach-ps2/gs-registers.h>

#include <uapi/asm/gs.h>
#include <uapi/asm/gif.h>
#include <uapi/drm/gs_drm.h>

#include "gs_cmd.h"
#include "gs_drm.h"
#include "gs_gem.h"
#include "gs_transfer.h"
#include "gs_vram.h"

#define GS_ENV_QWORDS 5		/* one tag + FRAME_1, FRAME_2, ZBUF_1, ZBUF_2 */

struct gs_submit_ctx {
	struct gs_device *gs;
	struct gs_gem_object *res[GS_SUBMIT_MAX_RESOURCES];
	u32 res_count;

	bool bitblt_valid;
	enum gs_psm dpsm;
	u32 dbw;
	bool trx_active;
	u32 trx_rrw;
	u32 trx_rrh;
	u32 trx_dsax;
	u32 trx_dsay;
	u32 trx_ssax;
	u32 trx_ssay;
	size_t trx_expected;
	size_t trx_sent;
	struct gs_resource *src_res;
	struct gs_resource *dst_res;
	bool dst_clut;
};

static u32 gs_psm_bpp(enum gs_psm psm)
{
	switch (psm) {
	case gs_psm_ct32:  return 32;
	case gs_psm_ct24:  return 24;
	case gs_psm_ct16:
	case gs_psm_ct16s: return 16;
	case gs_psm_t8:
	case gs_psm_t8h:   return 8;
	case gs_psm_t4:
	case gs_psm_t4hl:
	case gs_psm_t4hh:  return 4;
	default:           return 0;
	}
}

static bool gs_addr_allowed(u8 addr)
{
	if (addr >= gs_addr_prim && addr <= 0x0a)
		return true;
	if (addr >= 0x0c && addr <= 0x1c)
		return true;
	if (addr == 0x22)
		return true;
	if (addr >= 0x34 && addr <= 0x37)
		return true;
	if (addr == 0x3b || addr == 0x3d || addr == 0x3f)
		return true;
	if (addr >= 0x40 && addr <= 0x4b)
		return true;
	if (addr >= 0x50 && addr <= 0x54)
		return true;
	if (addr == 0x7f)	/* NOP */
		return true;

	/* FRAME_1/2 (0x4c-0x4d), ZBUF_1/2 (0x4e-0x4f) are driver owned. */
	return false;
}

static struct gs_resource *gs_slot(struct gs_submit_ctx *ctx, u32 slot)
{
	if (slot >= ctx->res_count)
		return NULL;
	return &ctx->res[slot]->res;
}

static int gs_patch_tex0(struct gs_submit_ctx *ctx, struct gs_tex0 *tex0)
{
	u32 slot;

	if (!(tex0->tbp0 & GS_RES_MARK))
		return -EPERM;

	slot = tex0->tbp0 & GS_RES_SLOT_MASK;
	if (slot >= ctx->res_count)
		return -EINVAL;

	{
		struct gs_resource *res = &ctx->res[slot]->res;

		if (!res->valid || !res->data)
			return -EINVAL;

		if (tex0->psm != res->psm || tex0->tbw != res->tbw)
			return -EINVAL;

		tex0->tbp0 = gs_vram_block_block(res->data);

		if (tex0->cbp & GS_RES_MARK_CLUT) {
			if (!res->clut)
				return -EINVAL;
			if (tex0->cpsm != res->clut_psm)
				return -EINVAL;
			tex0->cbp = gs_vram_block_block(res->clut);
		} else if (tex0->cbp) {
			return -EPERM;
		}
	}

	return 0;
}

static int gs_patch_bitbltbuf(struct gs_submit_ctx *ctx,
			      struct gs_bitbltbuf *b)
{
	ctx->src_res = NULL;
	ctx->dst_res = NULL;
	ctx->dst_clut = false;

	if (b->sbp & GS_RES_MARK) {
		u32 slot = b->sbp & GS_RES_SLOT_MASK;
		struct gs_resource *res = gs_slot(ctx, slot);

		if (!res || !res->data)
			return -EINVAL;
		if (b->spsm != res->psm || b->sbw != res->tbw)
			return -EINVAL;
		b->sbp = gs_vram_block_block(res->data);
		ctx->src_res = res;
	} else if (b->sbp) {
		return -EPERM;
	}

	if (!gs_psm_bpp(b->dpsm) || !gs_psm_bpp(b->spsm))
		return -EINVAL;

	ctx->dpsm = b->dpsm;
	ctx->dbw = b->dbw;

	if (b->dbp & GS_RES_MARK) {
		u32 slot = b->dbp & GS_RES_SLOT_MASK;
		struct gs_resource *res = gs_slot(ctx, slot);

		if (!res || !res->data)
			return -EINVAL;
		if (b->dpsm != res->psm || b->dbw != res->tbw)
			return -EINVAL;
		b->dbp = gs_vram_block_block(res->data);
		ctx->dst_res = res;
	} else if (b->dbp & GS_RES_MARK_CLUT) {
		u32 slot = b->dbp & GS_RES_SLOT_MASK;
		struct gs_resource *res = gs_slot(ctx, slot);

		if (!res || !res->clut)
			return -EINVAL;
		if (b->dpsm != res->clut_psm)
			return -EINVAL;
		b->dbp = gs_vram_block_block(res->clut);
		ctx->dst_res = res;
		ctx->dst_clut = true;
	} else if (b->dbp) {
		return -EPERM;
	}

	ctx->bitblt_valid = true;
	return 0;
}

static int gs_check_trx_bounds(struct gs_submit_ctx *ctx, bool source)
{
	const u32 bpp = gs_psm_bpp(ctx->dpsm) / 8;

	if (ctx->dst_res) {
		if (ctx->dst_clut) {
			if ((size_t)ctx->trx_rrw * ctx->trx_rrh * bpp >
			    (size_t)ctx->dst_res->clut_entries * 4)
				return -EINVAL;
		} else if (ctx->trx_dsax + ctx->trx_rrw >
			   ctx->dst_res->width ||
			   ctx->trx_dsay + ctx->trx_rrh >
			   ctx->dst_res->height) {
			return -EINVAL;
		}
	}

	if (!source || !ctx->src_res)
		return 0;

	if (ctx->trx_ssax + ctx->trx_rrw > ctx->src_res->width ||
	    ctx->trx_ssay + ctx->trx_rrh > ctx->src_res->height)
		return -EINVAL;

	return 0;
}

static int gs_apply_register(struct gs_submit_ctx *ctx, u8 addr,
			     union gif_reg *reg)
{
	switch (addr) {
	case gs_addr_tex0_1:
	case gs_addr_tex0_2:
		return gs_patch_tex0(ctx, &reg->tex0);
	case gs_addr_bitbltbuf:
		return gs_patch_bitbltbuf(ctx, &reg->bitbltbuf);
	case gs_addr_trxpos:
		ctx->trx_dsax = reg->trxpos.dsax;
		ctx->trx_dsay = reg->trxpos.dsay;
		ctx->trx_ssax = reg->trxpos.ssax;
		ctx->trx_ssay = reg->trxpos.ssay;
		return 0;
	case gs_addr_trxreg:
		if (!ctx->bitblt_valid)
			return -EINVAL;
		ctx->trx_rrw = reg->trxreg.rrw;
		ctx->trx_rrh = reg->trxreg.rrh;
		ctx->trx_expected = (size_t)ctx->trx_rrw * ctx->trx_rrh *
				    gs_psm_bpp(ctx->dpsm) / 8;
		return 0;
	case gs_addr_trxdir:
		if (ctx->trx_active &&
		    ctx->trx_sent < ctx->trx_expected)
			return -EINVAL;
		switch (reg->trxdir.xdir) {
		case gs_trxdir_host_to_local:
			if (!ctx->bitblt_valid || !ctx->trx_rrw ||
			    !ctx->dst_res)
				return -EINVAL;
			if (gs_check_trx_bounds(ctx, false))
				return -EINVAL;
			ctx->trx_active = true;
			ctx->trx_sent = 0;
			return 0;
		case gs_trxdir_local_to_local:
			if (!ctx->bitblt_valid || !ctx->src_res ||
			    !ctx->dst_res)
				return -EINVAL;
			if (gs_check_trx_bounds(ctx, true))
				return -EINVAL;
			ctx->trx_active = false;
			return 0;
		case gs_trxdir_local_to_host:
			return -EOPNOTSUPP;
		case gs_trxdir_nil:
			return 0;
		default:
			return -EINVAL;
		}
	default:
		return 0;
	}
}

static int gs_check_addresses(struct gs_submit_ctx *ctx,
			      const union gif_data *data, u32 count)
{
	u32 i;

	for (i = 0; i < count; i++) {
		const u8 addr = data[i].packed.ad.addr;

		if (!gs_addr_allowed(addr))
			return -EPERM;
	}
	return 0;
}

static int gs_validate_packed(struct gs_submit_ctx *ctx,
			      union gif_data *data, u32 nloop, u32 nreg)
{
	const u32 count = nloop * nreg;
	int err = gs_check_addresses(ctx, data, count);
	u32 i;

	if (err)
		return err;

	for (i = 0; i < count; i++) {
		union gif_reg reg;

		memcpy(&reg, &data[i].packed.ad.data, sizeof(reg));
		err = gs_apply_register(ctx, data[i].packed.ad.addr, &reg);
		if (err)
			return err;
		memcpy(&data[i].packed.ad.data, &reg, sizeof(reg));
	}

	return 0;
}

static u32 gs_tag_reg(const struct gif_tag *tag, u32 i)
{
	switch (i) {
	case 0: return tag->reg0;
	case 1: return tag->reg1;
	case 2: return tag->reg2;
	case 3: return tag->reg3;
	case 4: return tag->reg4;
	case 5: return tag->reg5;
	case 6: return tag->reg6;
	case 7: return tag->reg7;
	case 8: return tag->reg8;
	case 9: return tag->reg9;
	case 10: return tag->reg10;
	case 11: return tag->reg11;
	case 12: return tag->reg12;
	case 13: return tag->reg13;
	case 14: return tag->reg14;
	default: return tag->reg15;
	}
}

static int gs_validate_reglist(struct gs_submit_ctx *ctx,
			       const struct gif_tag *tag,
			       union gif_data *data, u32 nloop)
{
	const u32 nreg = tag->nreg ? tag->nreg : 16;
	const u32 qwords = (nreg + 1) / 2;
	u32 loop, q;

	for (loop = 0; loop < nloop; loop++) {
		for (q = 0; q < qwords; q++) {
			union gif_data *d = &data[loop * qwords + q];
			u32 desc0 = gs_tag_reg(tag, 2 * q) & 0xf;
			u32 desc1 = 2 * q + 1 < nreg ?
				gs_tag_reg(tag, 2 * q + 1) & 0xf : 0xf;
			int err;

			if (desc0 != 0xf) {
				if (!gs_addr_allowed(desc0))
					return -EPERM;
				err = gs_apply_register(ctx, desc0,
							&d->reg.lo);
				if (err)
					return err;
			}
			if (desc1 != 0xf) {
				if (!gs_addr_allowed(desc1))
					return -EPERM;
				err = gs_apply_register(ctx, desc1,
							&d->reg.hi);
				if (err)
					return err;
			}
		}
	}

	return 0;
}

static int gs_validate_image(struct gs_submit_ctx *ctx, u32 nloop)
{
	if (!ctx->trx_active)
		return -EINVAL;

	ctx->trx_sent += (size_t)nloop * 16;
	return 0;
}

static u32 gs_packet_qwords(const union gif_data *buf, u32 available)
{
	struct gif_tag tag;
	u32 data_qwords;

	if (available < 1)
		return 0;

	memcpy(&tag, &buf[0], sizeof(tag));

	switch (tag.flg) {
	case gif_packed_mode:
		data_qwords = tag.nloop * (tag.nreg ? tag.nreg : 16);
		break;
	case gif_reglist_mode:
		data_qwords = tag.nloop *
			((tag.nreg ? tag.nreg : 16) + 1) / 2;
		break;
	case gif_image_mode:
		data_qwords = tag.nloop;
		break;
	default:
		return 0;
	}

	if (data_qwords > available - 1)
		return 0;

	return 1 + data_qwords;
}

static int gs_validate_commands(struct gs_submit_ctx *ctx,
				union gif_data *buf, u32 qwords)
{
	u32 i = 0;

	while (i < qwords) {
		struct gif_tag tag;
		u32 packet_qwords;
		int err;

		packet_qwords = gs_packet_qwords(&buf[i], qwords - i);
		if (!packet_qwords)
			return -EINVAL;

		memcpy(&tag, &buf[i], sizeof(tag));

		switch (tag.flg) {
		case gif_packed_mode:
			err = gs_validate_packed(ctx, &buf[i + 1], tag.nloop,
						 tag.nreg ? tag.nreg : 16);
			break;
		case gif_reglist_mode:
			err = gs_validate_reglist(ctx, &tag, &buf[i + 1],
						  tag.nloop);
			break;
		case gif_image_mode:
			err = gs_validate_image(ctx, tag.nloop);
			break;
		default:
			return -EINVAL;
		}

		if (err)
			return err;

		i += packet_qwords;
	}

	if (ctx->trx_active) {
		if (ctx->trx_sent < ctx->trx_expected)
			return -EINVAL;
		if (ctx->trx_sent - ctx->trx_expected > 16)
			return -EINVAL;
	}

	return 0;
}

static u8 gs_zbuf_psm(enum gs_psm psm)
{
	switch (psm) {
	case gs_psm_z32:	return 0;
	case gs_psm_z24:	return 1;
	case gs_psm_z16:	return 2;
	case gs_psm_z16s:	return 3;
	default:		return 0;
	}
}

static void gs_emit_env(struct gs_submit_ctx *ctx, union gif_data *env)
{
	struct gs_device *gs = ctx->gs;
	struct gs_gem_object *fb_obj = NULL;
	enum gs_psm psm = gs_psm_ct32;
	u32 fbp = 0, fbw = 0;

	if (gs->scanout && gs->scanout->obj[0]) {
		fb_obj = to_gs_gem_obj(gs->scanout->obj[0]);
		if (fb_obj->vram) {
			fbp = gs_vram_block_page(fb_obj->vram);
			fbw = DIV_ROUND_UP(gs->scanout->width,
					   GS_FB_PAGE_WIDTH);
			gs_fourcc_to_psm(gs->scanout->format->format, &psm);
		}
	}

	memset(env, 0, sizeof(union gif_data) * GS_ENV_QWORDS);
	env[0].tag = (struct gif_tag) {
		.nloop = GS_ENV_QWORDS - 1,
		.flg = gif_packed_mode,
		.nreg = 1,
		.eop = 1,
	};
	env[1].packed.ad = (struct gif_packed_ad) {
		.addr = gs_addr_frame_1,
		.data.frame_1 = { .fbp = fbp, .fbw = fbw, .psm = psm },
	};
	env[2].packed.ad = (struct gif_packed_ad) {
		.addr = gs_addr_frame_2,
		.data.frame_2 = { .fbp = fbp, .fbw = fbw, .psm = psm },
	};
	env[3].packed.ad = (struct gif_packed_ad) {
		.addr = gs_addr_zbuf_1,
		.data.zbuf = { .zmsk = 1, .zbp = 0,
				       .psm = gs_zbuf_psm(gs_psm_z32) },
	};
	env[4].packed.ad = (struct gif_packed_ad) {
		.addr = gs_addr_zbuf_2,
		.data.zbuf = { .zmsk = 1, .zbp = 0,
				       .psm = gs_zbuf_psm(gs_psm_z32) },
	};
}

static u32 gs_chunk_qwords(const union gif_data *cmd, u32 qwords, u32 max)
{
	u32 chunk = 0;

	while (chunk < qwords) {
		u32 packet = gs_packet_qwords(&cmd[chunk], qwords - chunk);

		if (!packet || chunk + packet > max)
			break;

		chunk += packet;
	}

	return chunk;
}

static int gs_stream_commands(struct gs_submit_ctx *ctx,
			      const union gif_data *cmd, u32 qwords)
{
	struct gs_device *gs = ctx->gs;
	u32 done = 0;
	int err;

	/*
	 * Bind FRAME and ZBUF once, in a packet of its own, so that the
	 * remaining chunks can be split at GIF packet boundaries.
	 */
	gs_emit_env(ctx, (union gif_data *)gs->package.buffer);
	err = gif_submit((union gif_data *)gs->package.buffer, GS_ENV_QWORDS);
	if (err)
		return err;

	while (done < qwords) {
		union gif_data *stage = (union gif_data *)gs->package.buffer;
		u32 chunk = gs_chunk_qwords(cmd + done, qwords - done,
					    gs->package.capacity);

		if (!chunk)
			return -EINVAL;

		memcpy(stage, cmd + done, (size_t)chunk * sizeof(union gif_data));

		err = gif_submit(stage, chunk);
		if (err)
			return err;

		done += chunk;
	}

	return 0;
}

int gs_submit_init(struct gs_device *gs)
{
	mutex_init(&gs->submit_lock);
	gs->sequence = 0;
	return 0;
}

void gs_submit_fini(struct gs_device *gs)
{
}

int gs_submit_commands(struct gs_device *gs, struct drm_file *file_priv,
		       u32 cmd_handle, u32 cmd_offset, u32 cmd_qwords,
		       u32 res_count, const u32 __user *res_handles,
		       u32 *sequence)
{
	struct gs_submit_ctx ctx = { .gs = gs };
	struct drm_gem_object *obj, *cmd_obj;
	struct gs_gem_object *gs_cmd;
	struct iosys_map iosys;
	u32 handles[GS_SUBMIT_MAX_RESOURCES];
	union gif_data *cmdbuf = NULL;
	union gif_data *buf;
	int err, i;

	if (res_count > GS_SUBMIT_MAX_RESOURCES)
		return -EINVAL;

	if (cmd_qwords == 0 || cmd_qwords > 0x40000)
		return -EINVAL;

	if (cmd_offset & 0xf)
		return -EINVAL;

	if (res_count) {
		if (!res_handles)
			return -EINVAL;
		if (copy_from_user(handles, res_handles,
				   sizeof(u32) * res_count))
			return -EFAULT;
	}

	mutex_lock(&gs->submit_lock);

	for (i = 0; i < res_count; i++) {
		obj = drm_gem_object_lookup(file_priv, handles[i]);
		if (!obj) {
			err = -ENOENT;
			goto out_put;
		}
		ctx.res[ctx.res_count] = to_gs_gem_obj(obj);
		ctx.res_count++;
		if (!ctx.res[i]->res.valid) {
			err = -EINVAL;
			goto out_put;
		}
	}

	cmd_obj = drm_gem_object_lookup(file_priv, cmd_handle);
	if (!cmd_obj) {
		err = -ENOENT;
		goto out_put;
	}

	if (cmd_obj->size < (size_t)cmd_offset + (size_t)cmd_qwords * 16) {
		err = -EINVAL;
		goto out_put_cmd;
	}

	gs_cmd = to_gs_gem_obj(cmd_obj);

	err = drm_gem_vmap(&gs_cmd->base.base, &iosys);
	if (err)
		goto out_put_cmd;

	/*
	 * Validation patches resource addresses into TEX0/BITBLTBUF fields.
	 * Work on a private copy so that the user's display list remains
	 * unmodified and can be submitted again.
	 */
	cmdbuf = vmalloc((size_t)cmd_qwords * sizeof(*cmdbuf));
	if (!cmdbuf) {
		err = -ENOMEM;
		goto out_unmap;
	}
	iosys_map_memcpy_from(cmdbuf, &iosys, cmd_offset,
			      (size_t)cmd_qwords * sizeof(*cmdbuf));
	buf = cmdbuf;

	err = gs_validate_commands(&ctx, buf, cmd_qwords);
	if (err)
		goto out_unmap;

	err = gs_stream_commands(&ctx, buf, cmd_qwords);
	if (!err) {
		if (!gif_wait())
			err = -ETIMEDOUT;
		else
			gs->sequence++;
	}

	if (!err && sequence)
		*sequence = gs->sequence;

out_unmap:
	vfree(cmdbuf);
	drm_gem_vunmap(&gs_cmd->base.base, &iosys);
out_put_cmd:
	drm_gem_object_put(cmd_obj);
out_put:
	mutex_unlock(&gs->submit_lock);
	for (i = 0; i < ctx.res_count; i++)
		drm_gem_object_put(&ctx.res[i]->base.base);

	return err;
}
