#include "libuvc.h"
#ifdef __cplusplus
extern "C" {
#endif
uvc_error_t uvc_open_internal(uvc_device_t *dev, struct libusb_device_handle *usb_devh, uvc_device_handle_t **devh);
uvc_error_t uvc_open2(
    uvc_device_t *dev,
    uvc_device_handle_t **devh,
    struct libusb_device_handle *usb_devh);
uvc_error_t uvc_init2(uvc_context_t **ctx, struct libusb_context *usb_ctx, const char *usbfs);
uvc_error_t uvc_mjpeg2yuv(void *handler,uvc_frame_t *in, uvc_frame_t *out);		// XXX
void uvc_print_stream_ctrl2(uvc_stream_ctrl_t *ctrl);

#ifdef __cplusplus
}
#endif