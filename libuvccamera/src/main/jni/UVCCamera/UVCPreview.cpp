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
#if 1	// set 1 if you don't need debug log
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

#define	LOCAL_DEBUG 1
#define MAX_FRAME 4
#define PREVIEW_PIXEL_BYTES 4	// RGBA/RGBX
#define FRAME_POOL_SZ MAX_FRAME + 2

UVCPreview::UVCPreview(uvc_device_handle_t *devh)
:	mPreviewWindow(NULL),
	mCaptureWindow(NULL),
	mDeviceHandle(devh),
	requestWidth(DEFAULT_PREVIEW_WIDTH),
	requestHeight(DEFAULT_PREVIEW_HEIGHT),
	requestMinFps(DEFAULT_PREVIEW_FPS_MIN),
	requestMaxFps(DEFAULT_PREVIEW_FPS_MAX),
	requestMode(DEFAULT_PREVIEW_MODE),
	requestBandwidth(DEFAULT_BANDWIDTH),
	frameWidth(DEFAULT_PREVIEW_WIDTH),
	frameHeight(DEFAULT_PREVIEW_HEIGHT),
	frameBytes(DEFAULT_PREVIEW_WIDTH * DEFAULT_PREVIEW_HEIGHT * 2),	// YUYV
	frameMode(0),
	previewBytes(DEFAULT_PREVIEW_WIDTH * DEFAULT_PREVIEW_HEIGHT * PREVIEW_PIXEL_BYTES),
	previewFormat(WINDOW_FORMAT_RGBA_8888),
	mIsRunning(false),
	mIsCapturing(false),
	captureQueu(NULL),
	mFrameCallbackObj(NULL),
	mFrameCallbackFunc(NULL),
	callbackPixelBytes(2) {

	ENTER();

	LOGW("%p create with requestBandwidth:%d",this,(int)requestBandwidth);
	pthread_cond_init(&preview_sync, NULL);
	pthread_mutex_init(&preview_mutex, NULL);
//
	pthread_cond_init(&capture_sync, NULL);
	pthread_mutex_init(&capture_mutex, NULL);
//
	pthread_mutex_init(&pool_mutex, NULL);
	EXIT();
}

UVCPreview::~UVCPreview() {

	ENTER();
	if (mPreviewWindow)
		ANativeWindow_release(mPreviewWindow);
	mPreviewWindow = NULL;
	if (mCaptureWindow)
		ANativeWindow_release(mCaptureWindow);
	mCaptureWindow = NULL;
	clearPreviewFrame();
	clearCaptureFrame();
	clear_pool();
	pthread_mutex_destroy(&preview_mutex);
	pthread_cond_destroy(&preview_sync);
	pthread_mutex_destroy(&capture_mutex);
	pthread_cond_destroy(&capture_sync);
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

int UVCPreview::setPreviewSize(int width, int height, int min_fps, int max_fps, int mode, float bandwidth) {
	ENTER();

    LOGE("setPreviewSize(%d,%d,%d,%d,%d,%.2f)",width,height,min_fps,max_fps,mode,bandwidth);
	int result = 0;
    requestBandwidth = bandwidth;
    requestMinFps = min_fps;
    requestMaxFps = max_fps;
	if ((requestWidth != width) || (requestHeight != height) || (requestMode != mode)) {
		requestWidth = width;
		requestHeight = height;
		requestMode = mode;

		// uvc_stream_ctrl_t ctrl;
		//result = uvc_get_stream_ctrl_format_size_fps(mDeviceHandle, &ctrl,
		//	!requestMode ? UVC_FRAME_FORMAT_YUYV : UVC_FRAME_FORMAT_MJPEG,
		//	requestWidth, requestHeight, requestMinFps, requestMaxFps);
	}

	RETURN(result, int);
}

int UVCPreview::setPreviewDisplay(ANativeWindow *preview_window) {
	ENTER();
	pthread_mutex_lock(&preview_mutex);
	{
		if (mPreviewWindow != preview_window) {
			if (mPreviewWindow)
				ANativeWindow_release(mPreviewWindow);
			mPreviewWindow = preview_window;
			if (LIKELY(mPreviewWindow)) {
				ANativeWindow_setBuffersGeometry(mPreviewWindow,
					frameWidth, frameHeight, previewFormat);
			}
		}
	}
	pthread_mutex_unlock(&preview_mutex);
	RETURN(0, int);
}

int UVCPreview::setFrameCallback(JNIEnv *env, jobject frame_callback_obj, int pixel_format) {

	ENTER();
	pthread_mutex_lock(&capture_mutex);
	{
		if (isRunning() && isCapturing()) {
			mIsCapturing = false;
			if (mFrameCallbackObj) {
				pthread_cond_signal(&capture_sync);
				pthread_cond_wait(&capture_sync, &capture_mutex);	// wait finishing capturing
			}
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
		if (frame_callback_obj) {
			mPixelFormat = pixel_format;
			callbackPixelFormatChanged();
		}
	}
	pthread_mutex_unlock(&capture_mutex);
	RETURN(0, int);
}

void UVCPreview::callbackPixelFormatChanged() {
	mFrameCallbackFunc = NULL;
	const size_t sz = requestWidth * requestHeight;
	switch (mPixelFormat) {
	  case PIXEL_FORMAT_RAW:
		LOGI("PIXEL_FORMAT_RAW:");
		callbackPixelBytes = sz * 2;
		break;
	  case PIXEL_FORMAT_YUV:
		LOGI("PIXEL_FORMAT_YUV:");
		callbackPixelBytes = sz * 2;
		break;
	  case PIXEL_FORMAT_RGB565:
		LOGI("PIXEL_FORMAT_RGB565:");
		mFrameCallbackFunc = uvc_any2rgb;
		callbackPixelBytes = sz * 2;
		break;
	  case PIXEL_FORMAT_RGBX:
		LOGI("PIXEL_FORMAT_RGBX:");
		mFrameCallbackFunc = uvc_any2rgbx;
		callbackPixelBytes = sz * 4;
		break;
	  case PIXEL_FORMAT_YUV20SP:
		LOGI("PIXEL_FORMAT_YUV20SP:");
		mFrameCallbackFunc = uvc_yuyv2iyuv420SP;
		callbackPixelBytes = (sz * 3) / 2;
		break;
	  case PIXEL_FORMAT_NV21:
		LOGI("PIXEL_FORMAT_NV21:");
		mFrameCallbackFunc = uvc_yuyv2yuv420SP;
		callbackPixelBytes = (sz * 3) / 2;
		break;
	}
}

void UVCPreview::clearDisplay() {
	ENTER();

	ANativeWindow_Buffer buffer;
	pthread_mutex_lock(&capture_mutex);
	{
		if (LIKELY(mCaptureWindow)) {
			if (LIKELY(ANativeWindow_lock(mCaptureWindow, &buffer, NULL) == 0)) {
				uint8_t *dest = (uint8_t *)buffer.bits;
				const size_t bytes = buffer.width * PREVIEW_PIXEL_BYTES;
				const int stride = buffer.stride * PREVIEW_PIXEL_BYTES;
				for (int i = 0; i < buffer.height; i++) {
					memset(dest, 0, bytes);
					dest += stride;
				}
				ANativeWindow_unlockAndPost(mCaptureWindow);
			}
		}
	}
	pthread_mutex_unlock(&capture_mutex);
	pthread_mutex_lock(&preview_mutex);
	{
		if (LIKELY(mPreviewWindow)) {
			if (LIKELY(ANativeWindow_lock(mPreviewWindow, &buffer, NULL) == 0)) {
				uint8_t *dest = (uint8_t *)buffer.bits;
				const size_t bytes = buffer.width * PREVIEW_PIXEL_BYTES;
				const int stride = buffer.stride * PREVIEW_PIXEL_BYTES;
				for (int i = 0; i < buffer.height; i++) {
					memset(dest, 0, bytes);
					dest += stride;
				}
				ANativeWindow_unlockAndPost(mPreviewWindow);
			}
		}
	}
	pthread_mutex_unlock(&preview_mutex);

	EXIT();
}

int UVCPreview::startPreview() {
	ENTER();

	int result = EXIT_FAILURE;
	if (!isRunning()) {
		mIsRunning = true;
		pthread_mutex_lock(&preview_mutex);
		{
			// if (LIKELY(mPreviewWindow))
			{
				result = pthread_create(&preview_thread, NULL, preview_thread_func, (void *)this);
			}
		}
		pthread_mutex_unlock(&preview_mutex);
		if (UNLIKELY(result != EXIT_SUCCESS)) {
			LOGW("UVCCamera::window does not exist/already running/could not create thread etc.");
			mIsRunning = false;
			pthread_mutex_lock(&preview_mutex);
			{
				pthread_cond_signal(&preview_sync);
			}
			pthread_mutex_unlock(&preview_mutex);
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
		pthread_cond_signal(&preview_sync);
        LOGW("UVCPreview::stopPreview pthread_cond_signal(&preview_sync);");
		pthread_cond_signal(&capture_sync);
        LOGW("UVCPreview::stopPreview pthread_cond_signal(&capture_sync);");
		if (pthread_join(preview_thread, NULL) != EXIT_SUCCESS) {
			LOGW("UVCPreview::terminate preview thread: pthread_join failed");
		}
        LOGW("UVCPreview::stopPreview preview_thread done;");
		clearDisplay();
        LOGW("UVCPreview::stopPreview clearDisplay;");
	}
	clearPreviewFrame();
        LOGW("UVCPreview::stopPreview clearPreviewFrame;");
	clearCaptureFrame();
        LOGW("UVCPreview::stopPreview clearCaptureFrame;");
	pthread_mutex_lock(&preview_mutex);
	if (mPreviewWindow) {
		ANativeWindow_release(mPreviewWindow);
		mPreviewWindow = NULL;
	}
        LOGW("UVCPreview::stopPreview ANativeWindow_release mPreviewWindow;");
	pthread_mutex_unlock(&preview_mutex);
	pthread_mutex_lock(&capture_mutex);
	if (mCaptureWindow) {
		ANativeWindow_release(mCaptureWindow);
		mCaptureWindow = NULL;
	}
        LOGW("UVCPreview::stopPreview ANativeWindow_release(mCaptureWindow);");
	pthread_mutex_unlock(&capture_mutex);
	RETURN(0, int);
}

//**********************************************************************
//
//**********************************************************************
void UVCPreview::uvc_preview_frame_callback(uvc_frame_t *frame, void *vptr_args) {

    LOGD("uvc_preview_frame_callback");
	UVCPreview *preview = reinterpret_cast<UVCPreview *>(vptr_args);
	if UNLIKELY(!preview->isRunning() || !frame || !frame->frame_format || !frame->data || !frame->data_bytes) {
        // LOGD("uvc_preview_frame_callback data invalid.isRunning = %d, frame:%d, fmt:%d,data:%d,data_bytes:%d",preview->isRunning(),(int)frame,frame->frame_format,(int)frame->data,frame->data_bytes);
	    return;
	}
	if (UNLIKELY(
		((frame->frame_format != UVC_FRAME_FORMAT_MJPEG) && (frame->data_bytes < preview->frameBytes))
		|| (frame->width != preview->frameWidth) || (frame->height != preview->frameHeight) )) {

//#if LOCAL_DEBUG
		LOGD("broken frame!:format=%d,actual_bytes=%d/%d(%d,%d/%d,%d)",
			frame->frame_format, frame->actual_bytes, preview->frameBytes,
			frame->width, frame->height, preview->frameWidth, preview->frameHeight);
//#endif
		return;
	}
	if (LIKELY(preview->isRunning())) {
		uvc_frame_t *copy = preview->get_frame(frame->data_bytes);
		if (UNLIKELY(!copy)) {
//#if LOCAL_DEBUG
			LOGE("uvc_callback:unable to allocate duplicate frame!");
//#endif
			return;
		}
		uvc_error_t ret = uvc_duplicate_frame(frame, copy);
		if (UNLIKELY(ret)) {
		    LOGE("uvc_callback:uvc_duplicate_frame failed!");
			preview->recycle_frame(copy);
			return;
		}
		LOGI("uvc_preview_frame_callback addPreviewFrame");
		preview->addPreviewFrame(copy);
	}
		LOGI("uvc_preview_frame_callback ok");
}

void UVCPreview::addPreviewFrame(uvc_frame_t *frame) {

	pthread_mutex_lock(&preview_mutex);
	if (isRunning() && (previewFrames.size() < MAX_FRAME)) {
		previewFrames.put(frame);
		frame = NULL;
		pthread_cond_signal(&preview_sync);
	}
	pthread_mutex_unlock(&preview_mutex);
	if (frame) {
		recycle_frame(frame);
	}
}

uvc_frame_t *UVCPreview::waitPreviewFrame() {
	uvc_frame_t *frame = NULL;
	pthread_mutex_lock(&preview_mutex);
	{
		if (!previewFrames.size()) {
			pthread_cond_wait(&preview_sync, &preview_mutex);
		}
		if (LIKELY(isRunning() && previewFrames.size() > 0)) {
			frame = previewFrames.remove(0);
		}
	}
	pthread_mutex_unlock(&preview_mutex);
	return frame;
}

void UVCPreview::clearPreviewFrame() {
	pthread_mutex_lock(&preview_mutex);
	{
		for (int i = 0; i < previewFrames.size(); i++)
			recycle_frame(previewFrames[i]);
		previewFrames.clear();
	}
	pthread_mutex_unlock(&preview_mutex);
}

void *UVCPreview::preview_thread_func(void *vptr_args) {
	int result;

	ENTER();
	UVCPreview *preview = reinterpret_cast<UVCPreview *>(vptr_args);
	if (LIKELY(preview)) {
		uvc_stream_ctrl_t ctrl;
		result = preview->prepare_preview(&ctrl);
		if (LIKELY(!result)) {
			preview->do_preview(&ctrl);
		}
	}
	PRE_EXIT();
	pthread_exit(NULL);
}

int UVCPreview::prepare_preview(uvc_stream_ctrl_t *ctrl) {
	uvc_error_t result;

	ENTER();
	result = uvc_get_stream_ctrl_format_size(mDeviceHandle, ctrl,
		!requestMode ? UVC_FRAME_FORMAT_YUYV : UVC_FRAME_FORMAT_MJPEG,
		requestWidth, requestHeight, requestMaxFps
	);
	if (LIKELY(!result)) {
#if LOCAL_DEBUG
		// uvc_print_stream_ctrl(ctrl, stderr);
#endif
/*
		uvc_frame_desc_t *frame_desc;
		result = uvc_get_frame_desc(mDeviceHandle, ctrl, &frame_desc);
		if (LIKELY(!result)) {
			frameWidth = frame_desc->wWidth;
			frameHeight = frame_desc->wHeight;
			LOGI("frameSize=(%d,%d)@%s", frameWidth, frameHeight, (!requestMode ? "YUYV" : "MJPEG"));
			pthread_mutex_lock(&preview_mutex);
			if (LIKELY(mPreviewWindow)) {
				ANativeWindow_setBuffersGeometry(mPreviewWindow,
					frameWidth, frameHeight, previewFormat);
			}
			pthread_mutex_unlock(&preview_mutex);
		} else
*/
		{
			frameWidth = requestWidth;
			frameHeight = requestHeight;
		}
		frameMode = requestMode;
		frameBytes = frameWidth * frameHeight * (!requestMode ? 2 : 4);
		previewBytes = frameWidth * frameHeight * PREVIEW_PIXEL_BYTES;
	} else {
		LOGE("could not negotiate with camera:err=%d", result);
	}
	RETURN(result, int);
}

uvc_error_t uvc_start_streaming_bandwidth2(uvc_device_handle_t *devh,uvc_stream_ctrl_t *ctrl,uvc_stream_handle_t **strmh) {
	uvc_error_t ret;

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

void UVCPreview::do_preview(uvc_stream_ctrl_t *ctrl) {
	ENTER();
	LOGW("%p, enter do preview uvc_start_streaming_bandwidth with %d",this,(int)requestBandwidth);

    JavaVM *vm = getVM();
    JNIEnv *env;
    // attach to JavaVM
    vm->AttachCurrentThread(&env, NULL);

	uvc_stream_handle_t *stmh;
	uvc_error_t result = uvc_start_streaming_bandwidth2(mDeviceHandle, ctrl, &stmh);
    tjhandle handler = tjInitDecompress();
	if (LIKELY(!result)) {
		clearPreviewFrame();

#if LOCAL_DEBUG
		LOGI("Streaming...");
#endif

		uvc_frame_t *frame = NULL;
		if (frameMode) {
			// MJPEG mode
			int total = 0;
			for ( ; LIKELY(isRunning()) ; ) {
				// frame_mjpeg = waitPreviewFrame();

	            uvc_frame_t *frame_mjpeg = NULL;
				result = uvc_stream_get_frame(stmh,&frame_mjpeg,30000000);
				if (LIKELY(result)){
				    LOGW("uvc_stream_get_frame failed:%d", result);
				    break;
				}
				if (!LIKELY(frame_mjpeg)){
				    LOGD("uvc_stream_get_frame timeout", result);
				    continue;
				}
	            frame = get_frame(frame_mjpeg->width*frame_mjpeg->height*2);
                if (!LIKELY(frame)){
                    LOGD("alloc frame failed");
                    continue;
                }
                result = uvc_mjpeg2yuv(handler,frame_mjpeg, frame);   // MJPEG => yuyv
                if (result != 0){
				    LOGD("uvc_mjpeg2yuv failed.%d\n", result);
                    continue;
                }
                if (total == 90){
                    FILE *f = fopen("/sdcard/v3.yuv","wb");
                    fwrite(frame->data,1,frame->data_bytes,f);
                    fclose(f);
                }
                if (total == 90){
                    FILE *f = fopen("/sdcard/v3.jpg","wb");
                    fwrite(frame_mjpeg->data,1,frame_mjpeg->data_bytes,f);
                    fclose(f);
                }
                if (LIKELY(!result)) {

                    // frame = draw_preview_one(frame, &mPreviewWindow, uvc_any2rgbx, 4);
                    // addCaptureFrame(frame);

	                pthread_mutex_lock(&capture_mutex);
	                if (mFrameCallbackObj){
                        jobject buf = env->NewDirectByteBuffer(frame->data, frame->data_bytes);
                        env->CallVoidMethod(mFrameCallbackObj, iframecallback_fields.onFrame, buf);
                        env->ExceptionClear();
                        env->DeleteLocalRef(buf);
                    }
                    pthread_mutex_unlock(&capture_mutex);
                }
                uvc_free_frame(frame);
                //if (total++ >100)
                  //break;
			}
		} else {
			// yuvyv mode
			for ( ; LIKELY(isRunning()) ; ) {
				frame = waitPreviewFrame();
				if (LIKELY(frame)) {
					frame = draw_preview_one(frame, &mPreviewWindow, uvc_any2rgbx, 4);
					#if LOCAL_DEBUG
                    		LOGD("preview_thread_func:addCaptureFrame");
                    #endif
					addCaptureFrame(frame);
				}
			}
		}
		uvc_stop_streaming(mDeviceHandle);
	} else {
		uvc_perror(result, "failed start_streaming");
	}
    tjDestroy(handler);
    vm->DetachCurrentThread();
    MARK("DetachCurrentThread");
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

// changed to return original frame instead of returning converted frame even if convert_func is not null.
uvc_frame_t *UVCPreview::draw_preview_one(uvc_frame_t *frame, ANativeWindow **window, convFunc_t convert_func, int pixcelBytes) {
	// ENTER();

	int b = 0;
	pthread_mutex_lock(&preview_mutex);
	{
		b = *window != NULL;
	}
	pthread_mutex_unlock(&preview_mutex);
	if (LIKELY(b)) {
		uvc_frame_t *converted;
		if (convert_func) {
			converted = get_frame(frame->width * frame->height * pixcelBytes);
			if LIKELY(converted) {
				b = convert_func(frame, converted);
				if (!b) {
					pthread_mutex_lock(&preview_mutex);
					copyToSurface(converted, window);
					pthread_mutex_unlock(&preview_mutex);
				} else {
					LOGE("failed converting");
				}
				recycle_frame(converted);
			}
		} else {
			pthread_mutex_lock(&preview_mutex);
			copyToSurface(frame, window);
			pthread_mutex_unlock(&preview_mutex);
		}
	}
	return frame; //RETURN(frame, uvc_frame_t *);
}

//======================================================================
//
//======================================================================
inline const bool UVCPreview::isCapturing() const { return mIsCapturing; }

int UVCPreview::setCaptureDisplay(ANativeWindow *capture_window) {
	ENTER();

	RETURN(0, int);
}

void UVCPreview::addCaptureFrame(uvc_frame_t *frame) {
	pthread_mutex_lock(&capture_mutex);
	if (LIKELY(isRunning())) {
		// keep only latest one
		if (captureQueu) {
			recycle_frame(captureQueu);
		}
		captureQueu = frame;
		pthread_cond_broadcast(&capture_sync);
	}
	pthread_mutex_unlock(&capture_mutex);
}

/**
 * get frame data for capturing, if not exist, block and wait
 */
uvc_frame_t *UVCPreview::waitCaptureFrame() {
	uvc_frame_t *frame = NULL;
	pthread_mutex_lock(&capture_mutex);
	{
		if (!captureQueu) {
			pthread_cond_wait(&capture_sync, &capture_mutex);
		}
		if (LIKELY(isRunning() && captureQueu)) {
			frame = captureQueu;
			captureQueu = NULL;
		}
	}
	pthread_mutex_unlock(&capture_mutex);
	return frame;
}

/**
 * clear drame data for capturing
 */
void UVCPreview::clearCaptureFrame() {
	pthread_mutex_lock(&capture_mutex);
	{
		if (captureQueu)
			recycle_frame(captureQueu);
		captureQueu = NULL;
	}
	pthread_mutex_unlock(&capture_mutex);
}

/**
* call IFrameCallback#onFrame if needs
 */
void UVCPreview::do_capture_callback(JNIEnv *env, uvc_frame_t *frame) {
	ENTER();

	if (LIKELY(frame)) {
		uvc_frame_t *callback_frame = frame;
		if (mFrameCallbackObj) {
			if (0 && mFrameCallbackFunc) {
				callback_frame = get_frame(callbackPixelBytes);
				if (LIKELY(callback_frame)) {
					int b = mFrameCallbackFunc(frame, callback_frame);
					recycle_frame(frame);
					if (UNLIKELY(b)) {
						LOGW("failed to convert for callback frame");
						goto SKIP;
					}
				} else {
					LOGW("failed to allocate for callback frame");
					callback_frame = frame;
					goto SKIP;
				}
			}
			jobject buf = env->NewDirectByteBuffer(callback_frame->data, callbackPixelBytes);
			env->CallVoidMethod(mFrameCallbackObj, iframecallback_fields.onFrame, buf);
			env->ExceptionClear();
			env->DeleteLocalRef(buf);
		}
 SKIP:
		recycle_frame(callback_frame);
	}
	EXIT();
}
