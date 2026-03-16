/* SPDX-License-Identifier: MIT */

#ifndef TU_A8XX_POLICY_H
#define TU_A8XX_POLICY_H

#include "common/freedreno_dev_info.h"

static inline bool
tu_a8xx_relaxed_fs_lrz_disable(const struct fd_dev_info *info)
{
   /*
    * A8xx has reliable GPU-side LRZ direction tracking, so FS-side effects can
    * be handled with a temporary LRZ disable in more cases instead of
    * invalidating LRZ for the remainder of the render pass.
    */
   return info->chip >= 8 && info->props.has_lrz_dir_tracking;
}

static inline bool
tu_a8xx_mutable_ubwc_policy(const struct fd_dev_info *info)
{
   /*
    * Keep mutable-format UBWC decisions centralized so per-chip quirks can be
    * added without scattering chip checks in tu_image.cc.
    */
   return info->chip >= 8;
}

#endif /* TU_A8XX_POLICY_H */
