// SPDX-License-Identifier: GPL-2.0
/*
 * PlayStation 2 Graphics Synthesizer DRM connector
 *
 * Copyright (C) 2020 Fredrik Noring
 */

#ifndef GS_CONNECTOR_H
#define GS_CONNECTOR_H

#include <drm/drm_connector.h>

int gs_connector_init(struct drm_device *dev, struct drm_connector *connector);

#endif /* GS_CONNECTOR_H */
