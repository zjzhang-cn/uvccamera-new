/*
 * UVCCamera
 * library and sample to access to UVC web camera on non-rooted Android device
 *
 * Copyright (c) 2014-2017 saki t_saki@serenegiant.com
 *
 * File name: UVCPreview.cpp
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 *
 * All files in the folder are under this Apache License, Version 2.0.
 * Files in the jni/libjpeg, jni/libusb, jin/libuvc, jni/rapidjson folder may have a different license, see the respective files.
*/

#include <stdlib.h>
#include <linux/time.h>
#include <unistd.h>
#if 0	// set 1 if you don't need debug log
	#ifndef LOG_NDEBUG
		#define	LOG_NDEBUG		// w/o LOGV/LOGD/MARK
	#endif
	#undef USE_LOGALL
#else
	#define USE_LOGALL
	#undef LOG_NDEBUG
//	#undef NDEBUG
#endif

#include "utilbase.h"
#include "UVCPreview.h"
#include "libuvc_internal.h"
#include "turbojpeg.h"
#include "libyuv.h"

#define	LOCAL_DEBUG 1
#define MAX_FRAME 4
#define PREVIEW_PIXEL_BYTES 4	// RGBA/RGBX
#define FRAME_POOL_SZ MAX_FRAME + 2

#ifndef YUV_SIZE
	#define YUV_SIZE(w,h) w*h*3/2
#endif
#ifndef Y
	#define Y(s) (const uint8_t *)s
#endif
#ifndef U
	#define U(s) (const uint8_t *)((uint8_t *)s+width*height)
#endif
#ifndef V
	#define V(s) (const uint8_t *)((uint8_t *)s+(width*height + width * height/4))
#endif
#ifndef V422
	#define V422(s) (const uint8_t *)((uint8_t *)s+(width*height + width * height/2))
#endif
#ifndef DY
	#define DY(s) (uint8_t *)s
#endif
#ifndef DU
	#define DU(s) (uint8_t *)((uint8_t *)s+width*height)
#endif
#ifndef DV
	#define DV(s) (uint8_t *)((uint8_t *)s+(width*height + width * height/4))
#endif

UVCPreview::UVCPreview(uvc_device_handle_t *devh)
:	mDeviceHandle(devh),
	requestWidth(DEFAULT_PREVIEW_WIDTH),
	requestHeight(DEFAULT_PREVIEW_HEIGHT),
	requestMinFps(DEFAULT_PREVIEW_FPS_MIN),
	requestMaxFps(DEFAULT_PREVIEW_FPS_MAX),
	formatIndex(DEFAULT_PREVIEW_MODE),
	frame_format(UVC_FRAME_FORMAT_UNKNOWN),
	requestBandwidth(DEFAULT_BANDWIDTH),
	frameWidth(DEFAULT_PREVIEW_WIDTH),
	frameHeight(DEFAULT_PREVIEW_HEIGHT),
	frameBytes(DEFAULT_PREVIEW_WIDTH * DEFAULT_PREVIEW_HEIGHT * 2),	// YUYV
	previewBytes(DEFAULT_PREVIEW_WIDTH * DEFAULT_PREVIEW_HEIGHT * PREVIEW_PIXEL_BYTES),
	previewFormat(WINDOW_FORMAT_RGBA_8888),
	mIsRunning(false),
	mFrameCallbackObj(NULL),
	mFrameCallbackFunc(NULL),
	callbackPixelBytes(2) {

	ENTER();
//
	pthread_mutex_init(&pool_mutex, NULL);
	EXIT();
}

UVCPreview::~UVCPreview() {

	ENTER();
	clear_pool();
	pthread_mutex_destroy(&pool_mutex);
	EXIT();
}

/**
 * get uvc_frame_t from frame pool
 * if pool is empty, create new frame
 * this function does not confirm the frame size
 * and you may need to confirm the size
 */
uvc_frame_t *UVCPreview::get_frame(size_t data_bytes) {
	return uvc_allocate_frame(data_bytes);
}

void UVCPreview::recycle_frame(uvc_frame_t *frame) {
    uvc_free_frame(frame);
}


void UVCPreview::init_pool(size_t data_bytes) {
	ENTER();

	clear_pool();
	pthread_mutex_lock(&pool_mutex);
	{
		for (int i = 0; i < FRAME_POOL_SZ; i++) {
			mFramePool.put(uvc_allocate_frame(data_bytes));
		}
	}
	pthread_mutex_unlock(&pool_mutex);

	EXIT();
}

void UVCPreview::clear_pool() {
	ENTER();

	pthread_mutex_lock(&pool_mutex);
	{
		const int n = mFramePool.size();
		for (int i = 0; i < n; i++) {
			uvc_free_frame(mFramePool[i]);
		}
		mFramePool.clear();
	}
	pthread_mutex_unlock(&pool_mutex);
	EXIT();
}

inline const bool UVCPreview::isRunning() const {return mIsRunning; }

// width, height, min_fps, max_fps, frameFormat, bandwidthFactor
int UVCPreview::setPreviewSize(int width, int height,int fi) {
	ENTER();

    LOGE("setPreviewSize(%d,%d,%d)",width,height,fi);
	int result = 0;
    requestWidth = width;
    requestHeight = height;
	formatIndex = fi;

	RETURN(result, int);
}

int UVCPreview::setPreviewDisplay(ANativeWindow *preview_window) {
	ENTER();
	RETURN(0, int);
}

int UVCPreview::setFrameCallback(JNIEnv *env, jobject frame_callback_obj) {

	ENTER();
	if (isRunning()) {
		jclass illegalStateException = env->FindClass("java/lang/IllegalStateException");
		env->ThrowNew(illegalStateException,"Preview in progress.unable to set FrameCallback");
		return 0;
	}
		if (!env->IsSameObject(mFrameCallbackObj, frame_callback_obj))	{
			iframecallback_fields.onFrame = NULL;
			if (mFrameCallbackObj) {
				env->DeleteGlobalRef(mFrameCallbackObj);
			}
			mFrameCallbackObj = frame_callback_obj;
			if (frame_callback_obj) {
				// get method IDs of Java object for callback
				jclass clazz = env->GetObjectClass(frame_callback_obj);
				if (LIKELY(clazz)) {
					iframecallback_fields.onFrame = env->GetMethodID(clazz,
						"onFrame",	"(Ljava/nio/ByteBuffer;)V");
				} else {
					LOGW("failed to get object class");
				}
				env->ExceptionClear();
				if (!iframecallback_fields.onFrame) {
					LOGE("Can't find IFrameCallback#onFrame");
					env->DeleteGlobalRef(frame_callback_obj);
					mFrameCallbackObj = frame_callback_obj = NULL;
				}
			}
		}
	RETURN(0, int);
}



void UVCPreview::clearDisplay() {
	ENTER();

	EXIT();
}

int UVCPreview::startPreview() {
	ENTER();

	int result = EXIT_FAILURE;
	if (!isRunning()) {
		mIsRunning = true;
		result = pthread_create(&preview_thread, NULL, preview_thread_func, (void *)this);
		if (UNLIKELY(result != EXIT_SUCCESS)) {
			LOGW("UVCCamera::window does not exist/already running/could not create thread etc.");
			mIsRunning = false;
		}
	}
	RETURN(result, int);
}

int UVCPreview::stopPreview() {
	ENTER();
	bool b = isRunning();
	if (LIKELY(b)) {
		mIsRunning = false;
        LOGW("UVCPreview::stopPreview mIsRunning = false");
		if (pthread_join(preview_thread, NULL) != EXIT_SUCCESS) {
			LOGW("UVCPreview::terminate preview thread: pthread_join failed");
		}
        LOGW("UVCPreview::stopPreview preview_thread done;");
		clearDisplay();
        LOGW("UVCPreview::stopPreview clearDisplay;");
	}
		
	RETURN(0, int);
}

//**********************************************************************
//
//**********************************************************************
void UVCPreview::uvc_preview_frame_callback(uvc_frame_t *frame, void *vptr_args) {

    
}


void *UVCPreview::preview_thread_func(void *vptr_args) {
	int result;

	ENTER();
    JavaVM *vm = getVM();
    JNIEnv *env;
    // attach to JavaVM
    vm->AttachCurrentThread(&env, NULL);
	UVCPreview *preview = reinterpret_cast<UVCPreview *>(vptr_args);
	if (LIKELY(preview)) {
		uvc_stream_ctrl_t ctrl;
		result = preview->prepare_preview(&ctrl);
		if (LIKELY(!result)) {
			preview->do_preview(env, &ctrl);
		}
	}
	vm->DetachCurrentThread();
	PRE_EXIT();
	pthread_exit(NULL);
}

void UVCPreview::get_uvc_format(uvc_format_desc_t **fmt){
	uvc_streaming_interface_t *stream_if;
	uvc_format_desc_t *format;
	int stream_idx = 0;
	DL_FOREACH(mDeviceHandle->info->stream_ifs, stream_if)
	{
		++stream_idx;
		uvc_format_desc_t *format_descs;
		DL_FOREACH(stream_if->format_descs, format_descs){
			DL_FOREACH(format_descs, format) {
				if (format->bFormatIndex == formatIndex || formatIndex == 0){	// 0的话,取第一个
					*fmt = format;
					break;
				}
			}
		}
	}	
}

int UVCPreview::prepare_preview(uvc_stream_ctrl_t *ctrl) {
	uvc_error_t result;

	ENTER();


#define LOG_TO_FILE 0
#if LOG_TO_FILE
    FILE *f = fopen("/sdcard/uvc_dialog.txt","w");
    uvc_print_diag(mDeviceHandle, f);
	fflush(f);
	fclose(f);
#endif

#ifdef EXAMPLE
      const uvc_format_desc_t *format_desc = uvc_get_format_descs(devh);
      const uvc_frame_desc_t *frame_desc = format_desc->frame_descs;
      enum uvc_frame_format frame_format;
      int width = 640;
      int height = 480;
      int fps = 30;

      switch (format_desc->bDescriptorSubtype) {
      case UVC_VS_FORMAT_MJPEG:
        frame_format = UVC_COLOR_FORMAT_MJPEG;
        break;
      case UVC_VS_FORMAT_FRAME_BASED:
        frame_format = UVC_FRAME_FORMAT_H264;
        break;
      default:
        frame_format = UVC_FRAME_FORMAT_YUYV;
        break;
      }

      if (frame_desc) {
        width = frame_desc->wWidth;
        height = frame_desc->wHeight;
        fps = 10000000 / frame_desc->dwDefaultFrameInterval;
      }
#endif
	uvc_format_desc *format = NULL;
	get_uvc_format(&format);
	if (format == NULL){
		LOGI("format not found:%d.",formatIndex);
		result = UVC_ERROR_OTHER;
		RETURN(result, int);
	}
	uint8_t bBitsPerPixel = format->bBitsPerPixel;
	switch (format->bDescriptorSubtype) {
      	case UVC_VS_FORMAT_MJPEG:
			frame_format = UVC_FRAME_FORMAT_MJPEG;
			LOGI("format is MJPEG");
			break;
    //   case UVC_VS_FORMAT_FRAME_BASED:
    //     frame_format = UVC_FRAME_FORMAT_H264;
	// 	LOGI("format is H264");
    //     break;
		case UVC_VS_FORMAT_UNCOMPRESSED:
			switch(FOURCC(format->fourccFormat[0],format->fourccFormat[1],format->fourccFormat[2],format->fourccFormat[3])){
				case libyuv::FOURCC_NV12:
				case libyuv::FOURCC_I420:
					frame_format = UVC_FRAME_FORMAT_NV12;
					break;
				case libyuv::FOURCC_YUYV:
				case libyuv::FOURCC_YUY2:
					frame_format = UVC_FRAME_FORMAT_YUYV;
					break;
				case libyuv::FOURCC_UYVY:
					frame_format = UVC_FRAME_FORMAT_UYVY;
					break;
				default:
					frame_format = UVC_FRAME_FORMAT_UNKNOWN;
					break;
			}
			LOGI("format is UNCOMPRESSED.FOURCC:(%4s), format:%d",format->fourccFormat,frame_format);
			break;
        default:
            frame_format = UVC_FRAME_FORMAT_UNKNOWN;
	}
	if (frame_format == UVC_FRAME_FORMAT_UNKNOWN){
		LOGE("frame_format unknown.subType=%d!", format->bDescriptorSubtype);
		result = UVC_ERROR_OTHER;
		RETURN(result, int);
	}
	result = uvc_get_stream_ctrl_format_size(mDeviceHandle, ctrl,
	    frame_format,requestWidth, requestHeight, 0
	);
	if (LIKELY(!result)) {
#if LOCAL_DEBUG
		// uvc_print_stream_ctrl(ctrl, stderr);
#endif
		frameWidth = requestWidth;
		frameHeight = requestHeight;
		frameBytes = frameWidth * frameHeight * bBitsPerPixel/8;
		previewBytes = frameWidth * frameHeight * PREVIEW_PIXEL_BYTES;
	} else {
		LOGE("could not negotiate with camera:err=%d", result);
	}
	RETURN(result, int);
}

uvc_error_t uvc_start_streaming_bandwidth2(uvc_device_handle_t *devh,uvc_stream_ctrl_t *ctrl,uvc_stream_handle_t **strmh) {
	uvc_error_t ret;
	uvc_print_stream_ctrl2(ctrl);
	ret = uvc_stream_open_ctrl(devh, strmh, ctrl);
	if (UNLIKELY(ret != UVC_SUCCESS))
		return ret;

	ret = uvc_stream_start(*strmh, NULL, NULL, 0);
	if (UNLIKELY(ret != UVC_SUCCESS)) {
		uvc_stream_close(*strmh);
		return ret;
	}

	return UVC_SUCCESS;
}

void UVCPreview::do_preview(JNIEnv *env, uvc_stream_ctrl_t *ctrl) {
	ENTER();
	LOGW("%p, enter do preview uvc_start_streaming_bandwidth with %d",this,(int)requestBandwidth);


	uvc_stream_handle_t *stmh;
	uvc_error_t result = uvc_start_streaming_bandwidth2(mDeviceHandle, ctrl, &stmh);


	if (LIKELY(result)) {
		LOGE("failed to start streaming:%d",result);
		return;
	}

	uvc_frame_t *frame = NULL;
	// MJPEG mode
	LOGI("uvc_start_streaming_bandwidth2 done. result ok: 0");
	tjhandle handler = tjInitDecompress();
	LOGI("tjInitDecompress done. handler: %p",handler);
	if (!handler){
		LOGE("failed to tjInitDecompress..",result);
		uvc_stop_streaming(mDeviceHandle);
		return;
	}
	int total = 0;
	int firstFrame = 1;
	for ( ; LIKELY(isRunning()) ; ) {
		// frame_mjpeg = waitPreviewFrame();

		uvc_frame_t *src_frame = NULL;
		uvc_frame_t *i420;
		// LOGD("begin uvc_stream_get_frame");
		result = uvc_stream_get_frame(stmh,&src_frame,1000000);
		if (LIKELY(result)){
			LOGW("uvc_stream_get_frame failed:%d", result);
			if (UVC_ERROR_TIMEOUT == result){
			    continue;
			}else{
			    break;
			}
		}
		if (!LIKELY(src_frame)){
			LOGD("uvc_stream_get_frame timeout", result);
			continue;
		}

		const uint32_t width = src_frame->width;
		const uint32_t height = src_frame->height;
		const uvc_frame_format frame_format = src_frame->frame_format;
		if (firstFrame){
		    firstFrame = 0;
		    LOGI("first frame gotten.%dX%d,format:%d",width, height, frame_format);
		}
		//LOGD("end uvc_stream_get_frame.width:%d,height:%d,format:%d,size:%d,meta:%d",width,height,(int)frame_format,
		//	src_frame->data_bytes,src_frame->metadata_bytes);
		if (frame_format == UVC_FRAME_FORMAT_MJPEG){
			if (!LIKELY(frame)){
				frame = get_frame(src_frame->width*src_frame->height*2);
				if (!frame){
					LOGE("failed to get_frame:%d",src_frame->width*src_frame->height*2);
					break;
				}
			}else{
				frame->data_bytes = src_frame->width*src_frame->height*2;
			}
			result = uvc_mjpeg2yuv(handler,src_frame, frame);   // MJPEG => yuyv
			if (result != 0){
				LOGD("uvc_mjpeg2yuv failed.%d\n", result);
				continue;
			}
			i420 = get_frame(width*height*3/2);
			libyuv::I422ToI420(
				Y(frame->data), width,
				U(frame->data), (width+1)/2,
				V422(frame->data),(width+1)/2,
				DY(i420->data), width,
				DU(i420->data), (width+1)/2,
				DV(i420->data),(width+1)/2,
				width, height
			);
		}else if (frame_format == UVC_FRAME_FORMAT_NV12){
			i420 = get_frame(src_frame->data_bytes);
			if (!i420){
				LOGE("failed to get_frame:%d",src_frame->data_bytes);
				break;
			}
			libyuv::NV12ToI420(
				Y(src_frame->data), width,
				U(src_frame->data), width,
				DY(i420->data), width,
				DU(i420->data), (width+1)/2,
				DV(i420->data),(width+1)/2,
				width, height
			);
		}else if (frame_format == UVC_FRAME_FORMAT_YUYV){
			i420 = get_frame(width*height*3/2);
			if (!i420){
				LOGE("failed to get_frame:%d",width*height*3/2);
				break;
			}
			libyuv::YUY2ToI420(
				Y(src_frame->data), width*2,
				DY(i420->data), width,
				DU(i420->data), (width+1)/2,
				DV(i420->data),(width+1)/2,
				width, height
			);
		}else if (frame_format == UVC_FRAME_FORMAT_UYVY){
			i420 = get_frame(width*height*3/2);
			if (!i420){
				LOGE("failed to get_frame:%d",width*height*3/2);
				break;
			}
			libyuv::UYVYToI420(
				Y(src_frame->data), width*2,
				DY(i420->data), width,
				DU(i420->data), (width+1)/2,
				DV(i420->data),(width+1)/2,
				width, height
			);
		}
		if (mFrameCallbackObj){
			jobject buf = env->NewDirectByteBuffer(i420->data, i420->data_bytes);
			env->CallVoidMethod(mFrameCallbackObj, iframecallback_fields.onFrame, buf);
			env->ExceptionClear();
			env->DeleteLocalRef(buf);
		}
		if (LIKELY(i420) && src_frame != i420){
			uvc_free_frame(i420);
		}
		//if (total++ >100)
			//break;
	}
	if (LIKELY(frame)){
		uvc_free_frame(frame);
	}
	
	tjDestroy(handler);
	uvc_stop_streaming(mDeviceHandle);
	EXIT();
}

static void copyFrame(const uint8_t *src, uint8_t *dest, const int width, int height, const int stride_src, const int stride_dest) {
	const int h8 = height % 8;
	for (int i = 0; i < h8; i++) {
		memcpy(dest, src, width);
		dest += stride_dest; src += stride_src;
	}
	for (int i = 0; i < height; i += 8) {
		memcpy(dest, src, width);
		dest += stride_dest; src += stride_src;
		memcpy(dest, src, width);
		dest += stride_dest; src += stride_src;
		memcpy(dest, src, width);
		dest += stride_dest; src += stride_src;
		memcpy(dest, src, width);
		dest += stride_dest; src += stride_src;
		memcpy(dest, src, width);
		dest += stride_dest; src += stride_src;
		memcpy(dest, src, width);
		dest += stride_dest; src += stride_src;
		memcpy(dest, src, width);
		dest += stride_dest; src += stride_src;
		memcpy(dest, src, width);
		dest += stride_dest; src += stride_src;
	}
}


// transfer specific frame data to the Surface(ANativeWindow)
int copyToSurface(uvc_frame_t *frame, ANativeWindow **window) {
	// ENTER();
	int result = 0;
	if (LIKELY(*window)) {
		ANativeWindow_Buffer buffer;
		if (LIKELY(ANativeWindow_lock(*window, &buffer, NULL) == 0)) {
			// source = frame data
			const uint8_t *src = (uint8_t *)frame->data;
			const int src_w = frame->width * PREVIEW_PIXEL_BYTES;
			const int src_step = frame->width * PREVIEW_PIXEL_BYTES;
			// destination = Surface(ANativeWindow)
			uint8_t *dest = (uint8_t *)buffer.bits;
			const int dest_w = buffer.width * PREVIEW_PIXEL_BYTES;
			const int dest_step = buffer.stride * PREVIEW_PIXEL_BYTES;
			// use lower transfer bytes
			const int w = src_w < dest_w ? src_w : dest_w;
			// use lower height
			const int h = frame->height < buffer.height ? frame->height : buffer.height;
			// transfer from frame data to the Surface
			copyFrame(src, dest, w, h, src_step, dest_step);
			ANativeWindow_unlockAndPost(*window);
		} else {
			result = -1;
		}
	} else {
		result = -1;
	}
	return result; //RETURN(result, int);
}
