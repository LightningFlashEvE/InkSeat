#ifndef PROVISIONING_FONTS_H
#define PROVISIONING_FONTS_H

#include "fonts.h"

/* AP 配网页字体角色：
 * - 标题：中文大标题
 * - 提示：右侧主提示
 * - 标签：左下中文标签
 * - 动态值：热点名 / IP 等 ASCII 动态值
 */

static inline cFONT* provisioningTitleFont() {
    return &Font38CN;
}

static inline cFONT* provisioningHintFont() {
    return &Font36CN;
}

static inline cFONT* provisioningLabelFont() {
    return &Font20CN;
}

static inline sFONT* provisioningValueFont() {
    return &Font16;
}

#endif /* PROVISIONING_FONTS_H */
