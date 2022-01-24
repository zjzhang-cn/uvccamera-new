#include "libuvc/libuvc2.h"
#include "libuvc/libuvc_internal.h"
#include "turbojpeg.h"


uvc_error_t uvc_open2(
    uvc_device_t *dev,
    uvc_device_handle_t **devh,libusb_device_handle *usb_devh) {
  uvc_error_t ret;
  // struct libusb_device_handle *usb_devh;

  UVC_ENTER();
  ret = uvc_open_internal(dev, usb_devh, devh);
  UVC_EXIT(ret);
  return ret;
}

/** @brief Initializes the UVC context
 * @ingroup init
 *
 * @note If you provide your own USB context, you must handle
 * libusb event processing using a function such as libusb_handle_events.
 *
 * @param[out] pctx The location where the context reference should be stored.
 * @param[in]  usb_ctx Optional USB context to use
 * @return Error opening context or UVC_SUCCESS
 */
uvc_error_t uvc_init2(uvc_context_t **pctx, struct libusb_context *usb_ctx, const char *usbfs) {
	uvc_error_t ret = UVC_SUCCESS;
	uvc_context_t *ctx = calloc(1, sizeof(*ctx));

	if (usb_ctx == NULL) {
        ret = libusb_set_option(NULL, LIBUSB_OPTION_WEAK_AUTHORITY, NULL);
        if (ret != LIBUSB_SUCCESS) {
            LOGD("libusb_init failed: %d\n", r);
            return -1;
        }
        ret = libusb_init(&ctx->usb_ctx);
        if (ret < 0) {
            LOGD("libusb_init failed: %d\n", r);
            return ret;
        }
		ctx->own_usb_ctx = 1;
		if (UNLIKELY(ret != UVC_SUCCESS)) {
			LOGW("failed:err=%d", ret);
			free(ctx);
			ctx = NULL;
		}
	} else {
		ctx->own_usb_ctx = 0;
		ctx->usb_ctx = usb_ctx;
	}

	if (ctx != NULL)
		*pctx = ctx;

	return ret;
}


uvc_error_t uvc_mjpeg2yuv(void *handler, uvc_frame_t *in, uvc_frame_t *out) {

	if (UNLIKELY(in->frame_format != UVC_FRAME_FORMAT_MJPEG))
		return UVC_ERROR_INVALID_PARAM;

	size_t lines_read = 0;
	int i, j;
	int num_scanlines;
	register uint8_t *yuyv, *ycbcr;
// const unsigned char *jpegBuf, unsigned long jpegSize,unsigned char **dstPlanes, int width, int *strides, int height, int flags
	// int r = tjDecompressToYUVPlanes((tjhandle)handler,in->data, in->data_bytes, planes,in->width,NULL, in->height, 0);

	/*
	DLLEXPORT int DLLCALL tjDecompressToYUV2(tjhandle handle,
      const unsigned char *jpegBuf, unsigned long jpegSize, unsigned char *dstBuf,
      int width, int pad, int height, int flags);
	*/
	int width,height, jpegSubsamp, jpegColorspace;
	int r = tjDecompressHeader3((tjhandle)handler,in->data, in->data_bytes, &width,&height,&jpegSubsamp,&jpegColorspace);
	if (r < 0){
	    LOGE("tjDecompressHeader3 err.%s", tjGetErrorStr());
	    return r;
	}
    size_t size = tjBufSizeYUV2(width,4,height,jpegSubsamp);
    if (size > out->data_bytes){
	    LOGI("frame too small.width:%d,height:%d,jpegSubsamp:%d,need:%d,actual:%d",width, height, jpegSubsamp,size,out->data_bytes);
        return -1;
    }
	out->width = width;
	out->height = height;
	out->capture_time = in->capture_time;
	out->source = in->source;
	out->data_bytes = size;


	r = tjDecompressToYUV((tjhandle)handler,in->data, in->data_bytes, out->data,0);
	if (r != 0){
	    char *err = tjGetErrorStr();
	    // LOGW(err);
	    LOGI("err:%s\n",err);
	    uvc_free_frame(out);
	    return r;
	}
    return UVC_SUCCESS;
}