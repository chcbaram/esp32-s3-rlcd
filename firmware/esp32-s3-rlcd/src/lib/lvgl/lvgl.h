#ifndef SRC_LIB_LVGL_LVGL_H_
#define SRC_LIB_LVGL_LVGL_H_


#include "hw_def.h"


#define LV_COLOR_DEPTH     16
#define LV_COLOR_16_SWAP   0
#define LV_ATTRIBUTE_LARGE_CONST

#if LV_COLOR_DEPTH == 1
#define LV_COLOR_SIZE 8
#elif LV_COLOR_DEPTH == 8
#define LV_COLOR_SIZE 8
#elif LV_COLOR_DEPTH == 16
#define LV_COLOR_SIZE 16
#elif LV_COLOR_DEPTH == 32
#define LV_COLOR_SIZE 32
#else
#error "Invalid LV_COLOR_DEPTH in lv_conf.h! Set it to 1, 8, 16 or 32!"
#endif


#define lv_image_dsc_t lvgl_img_t


#define LV_IMAGE_HEADER_MAGIC (0x19)



typedef enum {
    LV_COLOR_FORMAT_UNKNOWN           = 0,

    LV_COLOR_FORMAT_RAW               = 0x01,
    LV_COLOR_FORMAT_RAW_ALPHA         = 0x02,

    /*<=1 byte (+alpha) formats*/
    LV_COLOR_FORMAT_L8                = 0x06,
    LV_COLOR_FORMAT_I1                = 0x07,
    LV_COLOR_FORMAT_I2                = 0x08,
    LV_COLOR_FORMAT_I4                = 0x09,
    LV_COLOR_FORMAT_I8                = 0x0A,
    LV_COLOR_FORMAT_A8                = 0x0E,

    /*2 byte (+alpha) formats*/
    LV_COLOR_FORMAT_RGB565            = 0x12,
    LV_COLOR_FORMAT_ARGB8565          = 0x13,   /**< Not supported by sw renderer yet. */
    LV_COLOR_FORMAT_RGB565A8          = 0x14,   /**< Color array followed by Alpha array*/
    LV_COLOR_FORMAT_AL88              = 0x15,   /**< L8 with alpha >*/

    /*3 byte (+alpha) formats*/
    LV_COLOR_FORMAT_RGB888            = 0x0F,
    LV_COLOR_FORMAT_ARGB8888          = 0x10,
    LV_COLOR_FORMAT_XRGB8888          = 0x11,

    /*Formats not supported by software renderer but kept here so GPU can use it*/
    LV_COLOR_FORMAT_A1                = 0x0B,
    LV_COLOR_FORMAT_A2                = 0x0C,
    LV_COLOR_FORMAT_A4                = 0x0D,

    /* reference to https://wiki.videolan.org/YUV/ */
    /*YUV planar formats*/
    LV_COLOR_FORMAT_YUV_START         = 0x20,
    LV_COLOR_FORMAT_I420              = LV_COLOR_FORMAT_YUV_START,  /*YUV420 planar(3 plane)*/
    LV_COLOR_FORMAT_I422              = 0x21,  /*YUV422 planar(3 plane)*/
    LV_COLOR_FORMAT_I444              = 0x22,  /*YUV444 planar(3 plane)*/
    LV_COLOR_FORMAT_I400              = 0x23,  /*YUV400 no chroma channel*/
    LV_COLOR_FORMAT_NV21              = 0x24,  /*YUV420 planar(2 plane), UV plane in 'V, U, V, U'*/
    LV_COLOR_FORMAT_NV12              = 0x25,  /*YUV420 planar(2 plane), UV plane in 'U, V, U, V'*/

    /*YUV packed formats*/
    LV_COLOR_FORMAT_YUY2              = 0x26,  /*YUV422 packed like 'Y U Y V'*/
    LV_COLOR_FORMAT_UYVY              = 0x27,  /*YUV422 packed like 'U Y V Y'*/

    LV_COLOR_FORMAT_YUV_END           = LV_COLOR_FORMAT_UYVY,

    /*Color formats in which LVGL can render*/
#if LV_COLOR_DEPTH == 1
    LV_COLOR_FORMAT_NATIVE            = LV_COLOR_FORMAT_I1,
    LV_COLOR_FORMAT_NATIVE_WITH_ALPHA = LV_COLOR_FORMAT_I1,
#elif LV_COLOR_DEPTH == 8
    LV_COLOR_FORMAT_NATIVE            = LV_COLOR_FORMAT_L8,
    LV_COLOR_FORMAT_NATIVE_WITH_ALPHA = LV_COLOR_FORMAT_AL88,
#elif LV_COLOR_DEPTH == 16
    LV_COLOR_FORMAT_NATIVE            = LV_COLOR_FORMAT_RGB565,
    LV_COLOR_FORMAT_NATIVE_WITH_ALPHA = LV_COLOR_FORMAT_RGB565A8,
#elif LV_COLOR_DEPTH == 24
    LV_COLOR_FORMAT_NATIVE            = LV_COLOR_FORMAT_RGB888,
    LV_COLOR_FORMAT_NATIVE_WITH_ALPHA = LV_COLOR_FORMAT_ARGB8888,
#elif LV_COLOR_DEPTH == 32
    LV_COLOR_FORMAT_NATIVE            = LV_COLOR_FORMAT_XRGB8888,
    LV_COLOR_FORMAT_NATIVE_WITH_ALPHA = LV_COLOR_FORMAT_ARGB8888,
#else
#error "LV_COLOR_DEPTH should be 1, 8, 16, 24 or 32"
#endif

} lv_color_format_t;


typedef struct {
    uint32_t magic: 8;          /**< Magic number. Must be LV_IMAGE_HEADER_MAGIC*/
    uint32_t cf : 8;            /**< Color format: See `lv_color_format_t`*/
    uint32_t flags: 16;         /**< Image flags, see `lv_image_flags_t`*/

    uint32_t w: 16;
    uint32_t h: 16;
    uint32_t stride: 16;        /**< Number of bytes in a row*/
    uint32_t reserved_2: 16;    /**< Reserved to be used later*/
} lv_image_header_t;

/** Image header it is compatible with
 * the result from image converter utility*/
typedef struct {
    lv_image_header_t header;   /**< A header describing the basics of the image*/
    uint32_t data_size;         /**< Size of the image in bytes*/
    const uint8_t * data;       /**< Pointer to the data of the image*/
    const void * reserved;      /**< A reserved field to make it has same size as lv_draw_buf_t*/
} lv_image_dsc_t;


#endif /* SRC_LIB_LVGL_LVGL_H_ */