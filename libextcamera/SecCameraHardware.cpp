/*
 * Copyright 2008, The Android Open Source Project
 * Copyright 2013, Samsung Electronics Co. LTD
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

 /*!
 * \file      SecCameraHardware.cpp
 * \brief     source file for Android Camera Ext HAL
 * \author    teahyung kim (tkon.kim@samsung.com)
 * \date      2013/04/30
 *
 */

#ifndef ANDROID_HARDWARE_SECCAMERAHARDWARE_CPP
#define ANDROID_HARDWARE_SECCAMERAHARDWARE_CPP

#define LOG_NDEBUG 0
#define LOG_TAG "SecCameraHardware"

#if 0
	#define gprintf(fmt, args...) ALOGE("%s(%d): " fmt, __FUNCTION__, __LINE__, ##args)
#else
	#define gprintf(fmt, args...)
#endif


#include "SecCameraHardware.h"

#define CLEAR(x)                memset(&(x), 0, sizeof(x))

#define CHECK_ERR(x, log)       if (CC_UNLIKELY(x < 0)) { \
                                    ALOGE log; \
                                    return false; \
                                }

#define CHECK_ERR_N(x, log)     if (CC_UNLIKELY(x < 0)) { \
                                    ALOGE log; \
                                    return x; \
                                }

#define CHECK_ERR_GOTO(out, x, log) if (CC_UNLIKELY(x < 0)) { \
                                        ALOGE log; \
                                        goto out; \
                                    }

#define FLITE0_DEV_PATH  "/dev/video36"
#define FLITE1_DEV_PATH  "/dev/video37"
#define FLITE2_DEV_PATH  "/dev/video38"

#define MAX_THUMBNAIL_SIZE (60000)

#define EXYNOS_MEM_DEVICE_DEV_NAME "/dev/exynos-mem"

/* for MC */
#define BACK_SENSOR_ENTITY_A_NM         "S5K4ECGX 5-0056"
#define BACK_SENSOR_ENTITY_B_NM         "S5K4ECGX 4-0056"
#define PFX_MIPI_CSIS_SUBDEV_ENTITY_NM  "s5p-mipi-csis"
#define PFX_FLITE_SUBDEV_ENTITY_NM      "flite-subdev"
#define PFX_FLITE_VIDEODEV_ENTITY_NM    "exynos-fimc-lite"
#define MEDIA_DEV_EXT_PATH              "/dev/media2"
#define PSFX_ENTITY_NUM_A 0
#define PSFX_ENTITY_NUM_B 1

namespace android {

gralloc_module_t const* SecCameraHardware::mGrallocHal;

SecCameraHardware::SecCameraHardware(int cameraId, camera_device_t *dev)
    : ISecCameraHardware(cameraId, dev)
{
#if 1 // ghcstop fix	
    if (cameraId == CAMERA_FACING_BACK)
        mFliteFormat = CAM_PIXEL_FORMAT_YUV422I;
    else
        mFliteFormat = CAM_PIXEL_FORMAT_YUV422I;
#else
	mFliteFormat = CAM_PIXEL_FORMAT_YUV420SP; // ghcstop 
#endif        

    mPreviewFormat      = CAM_PIXEL_FORMAT_YUV420SP;
    /* set suitably */
    mRecordingFormat    = CAM_PIXEL_FORMAT_YUV420;
    mPictureFormat      = CAM_PIXEL_FORMAT_JPEG;

    mZoomValue          = 10;
    mFimc1CSC           = NULL;
    mFimc2CSC           = NULL;

#if IS_FW_DEBUG
    m_mem_fd = -1;
#endif

    CLEAR(mWindowBuffer);

    if (!mGrallocHal) {
        int ret = 0;
        ret = hw_get_module(GRALLOC_HARDWARE_MODULE_ID, (const hw_module_t **)&mGrallocHal);
        if (CC_UNLIKELY(ret))
            ALOGE("SecCameraHardware: error, fail on loading gralloc HAL(0x%X)", (uint32_t)mGrallocHal);
    }

    createInstance(cameraId);
}

SecCameraHardware::~SecCameraHardware()
{
}

SecCameraHardware::FLiteV4l2::FLiteV4l2()
{
    mCameraFd = -1;
    mBufferCount = 0;
    mIsInternalISP = 0;
    mStreamOn = false;
    mCmdStop = 0;
}

SecCameraHardware::FLiteV4l2::~FLiteV4l2()
{
}

int SecCameraHardware::FLiteV4l2::init(const char *devPath, const int cameraId)
{
    int err;

    mCameraFd = open(devPath, O_RDWR);
    CHECK_ERR_N(mCameraFd, ("FLiteV4l2 init: error %d, open %s (%d - %s)", mCameraFd, devPath, errno, strerror(errno)));
    ALOGV("FLiteV4l2 init: %s, fd(%d)", devPath, mCameraFd);

    /* fimc_v4l2_querycap */
    struct v4l2_capability cap;
    CLEAR(cap);
    err = ioctl(mCameraFd, VIDIOC_QUERYCAP, &cap);
    CHECK_ERR_N(err, ("FLiteV4l2 init: error %d, VIDIOC_QUERYCAP", err));

    if (!(cap.capabilities & V4L2_CAP_VIDEO_CAPTURE_MPLANE)) {
        ALOGE("FLiteV4l2 init: error, no capture devices");
        return -1;
    }

    /* fimc_v4l2_enuminput */
    struct v4l2_input input;
    CLEAR(input);
    /* input.index = cameraId; */
    input.index = 0;

    err = ioctl(mCameraFd, VIDIOC_ENUMINPUT, &input);
    CHECK_ERR_N(err, ("FLiteV4l2 init: error %d, VIDIOC_ENUMINPUT", err));
    ALOGV("FLiteV4l2 init: camera[%d] %s", input.index, input.name);

    mIsInternalISP = !strncmp((const char*)input.name, "ISP Camera", 10);

    /* fimc_v4l2_s_input */
    err = ioctl(mCameraFd, VIDIOC_S_INPUT, &input);
    CHECK_ERR_N(err, ("FLiteV4l2 init: error %d, VIDIOC_S_INPUT", err));

#ifdef FAKE_SENSOR
    fakeIndex = 0;
    fakeByteData = 0;
#endif
    return 0;
}

void SecCameraHardware::FLiteV4l2::deinit()
{
    if (mCameraFd >= 0) {
        close(mCameraFd);
        mCameraFd = -1;
    }
    mBufferCount = 0;
    ALOGV("FLiteV4l2 deinit EX");
}

void SecCameraHardware::FLiteV4l2::getSensorSize(int frm_ratio, uint32_t *width, uint32_t *height)
{
    switch (frm_ratio) {
    case CAM_FRMRATIO_QCIF:
        *width = 1232;
        *height = 1008;
        break;
    case CAM_FRMRATIO_VGA:
        *width = 1392;  /* 1280 */;
        *height = 1044; /* 960 */;
        break;
    case CAM_FRMRATIO_SQUARE:
        *width = 1392;
        *height = 1392;
        break;
    case CAM_FRMRATIO_D1:
        *width = 1392; /* 720 */;
        *height = 928; /* 480 */;
        break;
    case CAM_FRMRATIO_HD:
        *width = 1344; /* 1280 */;
        *height = 756; /* 720 */;
        break;
    default:
        ALOGW("nativeGetSensorSize: invalid frame ratio %d", frm_ratio);
        *width = 1392;  /* 1280 */;
        *height = 1044; /* 960 */;
        break;
    }

}

int SecCameraHardware::FLiteV4l2::startPreview(image_rect_type *fliteSize,
    cam_pixel_format format, int numBufs, int fps, bool movieMode, node_info_t *mFliteNode)
{
    /* fimc_v4l2_enum_fmt */
    int err;
    bool found = false;
    struct v4l2_fmtdesc fmtdesc;
    CLEAR(fmtdesc);
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmtdesc.index = 0;

    while ((err = ioctl(mCameraFd, VIDIOC_ENUM_FMT, &fmtdesc)) == 0) {
        if (fmtdesc.pixelformat == (uint32_t)format) {
            ALOGV("FLiteV4l2 start: %s", fmtdesc.description);
            found = true;
            break;
        }
        fmtdesc.index++;
    }
    if (!found) {
        ALOGE("FLiteV4l2 start: error, unsupported pixel format (%c%c%c%c)"
            " fmtdesc.pixelformat = %d, %s, err=%d", format, format >> 8, format >> 16, format >> 24,
            fmtdesc.pixelformat, fmtdesc.description, err);
        return -1;
    }

#ifdef USE_CAPTURE_MODE
    err = sctrl(CAM_CID_CAPTURE_MODE, false);
    if (err < 0)
        ALOGE("ERROR(%s): mFlite.sctrl(%d) CAM_CID_CAPTURE_MODE failed", __FUNCTION__, err);
#endif

    v4l2_field field;
    if (movieMode)
        field = (enum v4l2_field)IS_MODE_PREVIEW_VIDEO;
    else
        field = (enum v4l2_field)IS_MODE_PREVIEW_STILL;

    /* fimc_v4l2_s_fmt */
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);
    if (mIsInternalISP) {
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        v4l2_fmt.fmt.pix.pixelformat = format;
        v4l2_fmt.fmt.pix.field = field;

        getSensorSize(fliteSize->width * 10 / fliteSize->height,
            &(v4l2_fmt.fmt.pix.width), &(v4l2_fmt.fmt.pix.height));
        ALOGD("FIMC IS FMT width:%d, height:%d", v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);

        err = ioctl(mCameraFd, VIDIOC_S_FMT, &v4l2_fmt);
        CHECK_ERR_N(err, ("FLiteV4l2 startPreview: error %d, VIDIOC_S_FMT", err));
		ALOGI("Louis(%s) : FIMC IS width:%d, height:%d", __func__, v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);
    } else {
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        v4l2_fmt.fmt.pix_mp.width = fliteSize->width;
        v4l2_fmt.fmt.pix_mp.height = fliteSize->height;
        v4l2_fmt.fmt.pix_mp.pixelformat = format;
        v4l2_fmt.fmt.pix_mp.field = field;
        err = ioctl(mCameraFd, VIDIOC_S_FMT, &v4l2_fmt);
        CHECK_ERR_N(err, ("FLiteV4l2 start: error %d, VIDIOC_S_FMT", err));
		ALOGI("Louis(%s) : FIMC width:%d, height:%d", __func__, v4l2_fmt.fmt.pix_mp.width, v4l2_fmt.fmt.pix_mp.height);		
    }

#ifdef CACHEABLE
    sctrl(V4L2_CID_CACHEABLE, 1);
#endif
    /* setting fps */
    ALOGV("DEBUG(%s): FLiteV4l2 setting fps: %d", __FUNCTION__, fps);
    err = sctrl(V4L2_CID_CAM_FRAME_RATE, fps);
    CHECK_ERR_N(err, ("ERR(%s): sctrl V4L2_CID_CAM_FRAME_RATE(%d) value(%d)", __FUNCTION__, err, fps));

    /* sctrl(V4L2_CID_EMBEDDEDDATA_ENABLE, 0); */

    /* fimc_v4l2_reqbufs */
    struct v4l2_requestbuffers req;
    CLEAR(req);
    req.count = numBufs;
    req.type   = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
#ifdef USE_USERPTR
    req.memory = V4L2_MEMORY_USERPTR;
#else
    req.memory = V4L2_MEMORY_MMAP;
#endif
    err = ioctl(mCameraFd, VIDIOC_REQBUFS, &req);
    CHECK_ERR_N(err, ("FLiteV4l2 start: error %d, VIDIOC_REQBUFS", err));

    mBufferCount = (int)req.count;

    if (mIsInternalISP)
        sctrl(V4L2_CID_IS_S_SCENARIO_MODE, field);

    /* setting mFliteNode for Q/DQ */
    mFliteNode->width  = v4l2_fmt.fmt.pix.width;
    mFliteNode->height = v4l2_fmt.fmt.pix.height;
    mFliteNode->format = v4l2_fmt.fmt.pix.pixelformat;
    mFliteNode->planes = NUM_FLITE_BUF_PLANE;
    mFliteNode->memory = req.memory;
    mFliteNode->type   = req.type;
    mFliteNode->buffers= numBufs;

    return 0;
}

int SecCameraHardware::FLiteV4l2::startCapture(image_rect_type *img,
    cam_pixel_format format, int numBufs)
{
    /* fimc_v4l2_enum_fmt */
    int err;
    bool found = false;
    struct v4l2_fmtdesc fmtdesc;
    CLEAR(fmtdesc);
    fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    fmtdesc.index = 0;

    while ((err = ioctl(mCameraFd, VIDIOC_ENUM_FMT, &fmtdesc)) == 0) {
        if (fmtdesc.pixelformat == (uint32_t)format) {
            ALOGV("FLiteV4l2 start: %s", fmtdesc.description);
            found = true;
            break;
        }
        fmtdesc.index++;
    }

    if (!found) {
        ALOGE("FLiteV4l2 start: error, unsupported pixel format (%c%c%c%c)"
            " fmtdesc.pixelformat = %d, %s, err=%d", format, format >> 8, format >> 16, format >> 24,
            fmtdesc.pixelformat, fmtdesc.description, err);
        return -1;
    }

    /* fimc_v4l2_s_fmt */
    struct v4l2_format v4l2_fmt;
    CLEAR(v4l2_fmt);

#ifdef NOTDEFINED
    if (mIsInternalISP) {
        v4l2_fmt.type = V4L2_BUF_TYPE_PRIVATE;
        v4l2_fmt.fmt.pix.pixelformat = format;
        v4l2_fmt.fmt.pix.field = (enum v4l2_field)IS_MODE_CAPTURE_STILL;

        ALOGV("reqeusted capture size %dx%d", img->width, img->height);
        getSensorSize(img->width*10/img->height,
          &(v4l2_fmt.fmt.pix.width), &(v4l2_fmt.fmt.pix.height));
        ALOGD("FIMC IS FMT width:%d, height:%d", v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);

        err = ioctl(mCameraFd, VIDIOC_S_FMT, &v4l2_fmt);
        CHECK_ERR_N(err, ("FLiteV4l2 startCapture: error %d, VIDIOC_S_FMT", err));
    }
#endif

    ALOGV("DEBUG(%s): reqeusted capture size %dx%d %d", __FUNCTION__, img->width, img->height, format);

    if (!mIsInternalISP) {
        v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        v4l2_fmt.fmt.pix_mp.width = img->width;
        v4l2_fmt.fmt.pix_mp.height = img->height;
        v4l2_fmt.fmt.pix_mp.pixelformat = format;

        err = ioctl(mCameraFd, VIDIOC_S_FMT, &v4l2_fmt);
        CHECK_ERR_N(err, ("FLiteV4l2 start: error %d, VIDIOC_S_FMT", err));
    }

#ifdef USE_CAPTURE_MODE
    err = sctrl(CAM_CID_CAPTURE_MODE, true);
    CHECK_ERR_N(err, ("ERR(%s): sctrl V4L2_CID_CAMERA_CAPTURE(%d) enable failed", __FUNCTION__, err));
#endif

    if (mIsInternalISP)
        sctrl(V4L2_CID_IS_S_SCENARIO_MODE, IS_MODE_CAPTURE_STILL);

    /* fimc_v4l2_reqbufs */
    struct v4l2_requestbuffers req;
    CLEAR(req);
    req.count = SKIP_CAPTURE_CNT;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
#ifdef USE_USERPTR
    req.memory = V4L2_MEMORY_USERPTR;
#else
    req.memory = V4L2_MEMORY_MMAP;
#endif

    err = ioctl(mCameraFd, VIDIOC_REQBUFS, &req);
    CHECK_ERR_N(err, ("FLiteV4l2 start: error %d, VIDIOC_REQBUFS", err));

    mBufferCount = (int)req.count;

    return 0;
}

int SecCameraHardware::FLiteV4l2::startRecord(image_rect_type *img, image_rect_type *videoSize,
    cam_pixel_format format, int numBufs)
{
    /* fimc_v4l2_enum_fmt */
     int err;
     bool found = false;
     struct v4l2_fmtdesc fmtdesc;
     CLEAR(fmtdesc);
     fmtdesc.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
     fmtdesc.index = 0;

     while ((err = ioctl(mCameraFd, VIDIOC_ENUM_FMT, &fmtdesc)) == 0) {
         if (fmtdesc.pixelformat == (uint32_t)format) {
             ALOGV("FLiteV4l2 start: %s", fmtdesc.description);
             found = true;
             break;
         }
         fmtdesc.index++;
     }

     if (!found) {
         ALOGE("FLiteV4l2 start: error, unsupported pixel format (%c%c%c%c)"
             " fmtdesc.pixelformat = %d, %s, err=%d", format, format >> 8, format >> 16, format >> 24,
             fmtdesc.pixelformat, fmtdesc.description, err);
         return -1;
     }

     /* fimc_v4l2_s_fmt */
     struct v4l2_format v4l2_fmt;
     CLEAR(v4l2_fmt);

     if (mIsInternalISP) {
         v4l2_fmt.type = V4L2_BUF_TYPE_PRIVATE;
         v4l2_fmt.fmt.pix.pixelformat = format;
         v4l2_fmt.fmt.pix.field = (enum v4l2_field)IS_MODE_PREVIEW_VIDEO;

         getSensorSize(videoSize->width*10/videoSize->height,
           &(v4l2_fmt.fmt.pix.width), &(v4l2_fmt.fmt.pix.height));
         ALOGD("FIMC IS FMT width:%d, height:%d", v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);

         err = ioctl(mCameraFd, VIDIOC_S_FMT, &v4l2_fmt);
         CHECK_ERR_N(err, ("FLiteV4l2 startCapture: error %d, VIDIOC_S_FMT", err));
     }

     v4l2_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
     v4l2_fmt.fmt.pix.width = videoSize->width;
     v4l2_fmt.fmt.pix.height = videoSize->height;
     v4l2_fmt.fmt.pix.pixelformat = format;
     v4l2_fmt.fmt.pix.priv = V4L2_PIX_FMT_MODE_CAPTURE;

     err = ioctl(mCameraFd, VIDIOC_S_FMT, &v4l2_fmt);
     CHECK_ERR_N(err, ("FLiteV4l2 start: error %d, VIDIOC_S_FMT", err));

     if (!mIsInternalISP) {
         v4l2_fmt.type = V4L2_BUF_TYPE_PRIVATE;
         v4l2_fmt.fmt.pix.width = videoSize->width;
         v4l2_fmt.fmt.pix.height = videoSize->height;
         v4l2_fmt.fmt.pix.pixelformat = format;
         v4l2_fmt.fmt.pix.field = (enum v4l2_field)IS_MODE_PREVIEW_STILL;

         ALOGD("Sensor FMT %dx%d", v4l2_fmt.fmt.pix.width, v4l2_fmt.fmt.pix.height);

         err = ioctl(mCameraFd, VIDIOC_S_FMT, &v4l2_fmt);
         CHECK_ERR_N(err, ("FLiteV4l2 start: error %d, VIDIOC_S_FMT", err));
     }

     if (mIsInternalISP)
         sctrl(V4L2_CID_IS_S_SCENARIO_MODE, IS_MODE_PREVIEW_VIDEO);

     /* fimc_v4l2_reqbufs */
     struct v4l2_requestbuffers req;
     CLEAR(req);
     req.count = numBufs;
     req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
#ifdef USE_USERPTR
     req.memory = V4L2_MEMORY_USERPTR;
#else
     req.memory = V4L2_MEMORY_MMAP;
#endif

     err = ioctl(mCameraFd, VIDIOC_REQBUFS, &req);
     CHECK_ERR_N(err, ("FLiteV4l2 start: error %d, VIDIOC_REQBUFS", err));

     mBufferCount = (int)req.count;

     return 0;
}

int SecCameraHardware::FLiteV4l2::reqBufZero(node_info_t *mFliteNode)
{
    int err;
    /* fimc_v4l2_reqbufs */
    struct v4l2_requestbuffers req;
    CLEAR(req);
    req.count = 0;
    req.type = mFliteNode->type;
    req.memory = mFliteNode->memory;
    ALOGV("DEBUG(%s): type[%d] memory[%d]", __FUNCTION__, req.type, req.memory);
    err = ioctl(mCameraFd, VIDIOC_REQBUFS, &req);
    CHECK_ERR_N(err, ("FLiteV4l2 reqBufZero: error %d", err));
    return 1;
}

int SecCameraHardware::FLiteV4l2::querybuf2(unsigned int index, int planeCnt, ExynosBuffer *buf)
{
    struct v4l2_buffer v4l2_buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    int err;

    CLEAR(v4l2_buf);
    CLEAR(planes);

    /* loop for buffer count */
    v4l2_buf.index      = index;
    v4l2_buf.type       = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_buf.memory     = V4L2_MEMORY_MMAP;
    v4l2_buf.length     = planeCnt;
    v4l2_buf.m.planes   = planes;

    err = ioctl(mCameraFd , VIDIOC_QUERYBUF, &v4l2_buf);
    if (err < 0) {
        ALOGE("ERR(%s)(%d): error %d, index %d", __func__, __LINE__, err, index);
        return 0;
    }

    /* loop for planes */
    for (int i = 0; i < planeCnt; i++) {
        unsigned int length = v4l2_buf.m.planes[i].length;
        unsigned int offset = v4l2_buf.m.planes[i].m.mem_offset;
        char *vAdr = (char *) mmap(0,
                    v4l2_buf.m.planes[i].length,
                    PROT_READ | PROT_WRITE, MAP_SHARED,
                    mCameraFd, offset);
        ALOGV("DEBUG(%s): [%d][%d] length %d, offset %d vadr %p", __FUNCTION__, index, i, length, offset, vAdr);
        if (vAdr == NULL) {
            ALOGE("ERR(%s)(%d): [%d][%d] vAdr is %p", __func__, __LINE__,  index, i, vAdr);
            return 0;
        } else {
            buf->virt.extP[i] = vAdr;
            buf->size.extS[i] = length;
            memset(buf->virt.extP[i], 0x0, buf->size.extS[i]);
        }
    }

    return 1;
}

int SecCameraHardware::FLiteV4l2::expBuf(unsigned int index, int planeCnt, ExynosBuffer *buf)
{
    struct v4l2_exportbuffer v4l2_expbuf;
    int err;

    for (int i = 0; i < planeCnt; i++) {
        memset(&v4l2_expbuf, 0, sizeof(v4l2_expbuf));
        v4l2_expbuf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
        v4l2_expbuf.index = index;
        err = ioctl(mCameraFd, VIDIOC_EXPBUF, &v4l2_expbuf);
        if (err < 0) {
            ALOGE("ERR(%s)(%d): [%d][%d] failed %d", __func__, __LINE__, index, i, err);
            goto EXP_ERR;
        } else {
            ALOGV("DEBUG(%s): [%d][%d] fd %d", __FUNCTION__, index, i, v4l2_expbuf.fd);
            buf->fd.extFd[i] = v4l2_expbuf.fd;
        }
    }
    return 1;

EXP_ERR:
    for (int i = 0; i < planeCnt; i++) {
        if (buf->fd.extFd[i] > 0)
            ion_free(buf->fd.extFd[i]);
    }
    return 0;
}


sp<MemoryHeapBase> SecCameraHardware::FLiteV4l2::querybuf(uint32_t *frmsize)
{
    struct v4l2_buffer v4l2_buf;
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_buf.length = 0;
    int err;

    for (int i = 0; i < mBufferCount; i++) {
        v4l2_buf.index = i;
        err = ioctl(mCameraFd , VIDIOC_QUERYBUF, &v4l2_buf);
        if (err < 0) {
            ALOGE("FLiteV4l2 querybufs: error %d, index %d", err, i);
            *frmsize = 0;
            return NULL;
        }
    }

    *frmsize = v4l2_buf.length;

    return mBufferCount == 1 ?
        new MemoryHeapBase(mCameraFd, v4l2_buf.length, v4l2_buf.m.offset) : NULL;
}

int SecCameraHardware::FLiteV4l2::qbuf(uint32_t index)
{
    struct v4l2_buffer v4l2_buf;
    CLEAR(v4l2_buf);
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;
    v4l2_buf.index = index;

    int err = ioctl(mCameraFd, VIDIOC_QBUF, &v4l2_buf);
    CHECK_ERR_N(err, ("FLiteV4l2 qbuf: error %d", err));

    return 0;
}

int SecCameraHardware::FLiteV4l2::dqbuf()
{
    struct v4l2_buffer v4l2_buf;
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;

    int err = ioctl(mCameraFd, VIDIOC_DQBUF, &v4l2_buf);
    CHECK_ERR_N(err, ("FLiteV4l2 dqbuf: error %d", err));

    if (v4l2_buf.index > (uint32_t)mBufferCount) {
        ALOGE("FLiteV4l2 dqbuf: error %d, invalid index", v4l2_buf.index);
        return -1;
    }

    return v4l2_buf.index;
}

int SecCameraHardware::FLiteV4l2::dqbuf(uint32_t *index)
{
    struct v4l2_buffer v4l2_buf;
    v4l2_buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    v4l2_buf.memory = V4L2_MEMORY_MMAP;

    int err = ioctl(mCameraFd, VIDIOC_DQBUF, &v4l2_buf);
    if (err >= 0)
        *index = v4l2_buf.index;

    return err;
}

int SecCameraHardware::FLiteV4l2::qbuf2(node_info_t *node, uint32_t index)
{
    struct v4l2_buffer v4l2_buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    int i;
    int ret = 0;

    CLEAR(planes);
    CLEAR(v4l2_buf);

    v4l2_buf.m.planes   = planes;
    v4l2_buf.type       = node->type;
    v4l2_buf.memory     = node->memory;
    v4l2_buf.index      = index;
    v4l2_buf.length     = node->planes;

    for(i = 0; i < node->planes; i++){
        if (node->memory == V4L2_MEMORY_DMABUF)
            v4l2_buf.m.planes[i].m.fd = (int)(node->buffer[index].fd.extFd[i]);
        else if (node->memory == V4L2_MEMORY_USERPTR) {
            v4l2_buf.m.planes[i].m.userptr = (unsigned long)(node->buffer[index].virt.extP[i]);
        } else if (node->memory == V4L2_MEMORY_MMAP) {
            v4l2_buf.m.planes[i].m.userptr = (unsigned long)(node->buffer[index].virt.extP[i]);
        } else {
            ALOGE("ERR(%s):invalid node->memory(%d)", __func__, node->memory);
            return -1;
        }
        v4l2_buf.m.planes[i].length  = (unsigned long)(node->buffer[index].size.extS[i]);
    }

    ret = exynos_v4l2_qbuf(node->fd, &v4l2_buf);
    if (ret < 0) {
        ALOGE("ERR(%s):exynos_v4l2_qbuf failed (index:%d)(ret:%d)", __func__, index, ret);
        return -1;
    }

    return ret;
}

int SecCameraHardware::FLiteV4l2::qbufForCapture(ExynosBuffer *buf, uint32_t index)
{
    struct v4l2_buffer v4l2_buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    unsigned int i;
    int ret = 0;

    CLEAR(planes);
    CLEAR(v4l2_buf);

    v4l2_buf.m.planes   = planes;
    v4l2_buf.type       = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
#ifdef USE_USERPTR
    v4l2_buf.memory     = V4L2_MEMORY_USERPTR;
#else
    v4l2_buf.memory     = V4L2_MEMORY_MMAP;
#endif
    v4l2_buf.index      = index;
    v4l2_buf.length     = 1;

    for(i = 0; i < v4l2_buf.length; i++){
        if (v4l2_buf.memory == V4L2_MEMORY_DMABUF)
            v4l2_buf.m.planes[i].m.fd = (int)(buf->fd.extFd[i]);
        else if (v4l2_buf.memory == V4L2_MEMORY_USERPTR) {
            v4l2_buf.m.planes[i].m.userptr = (unsigned long)(buf->virt.extP[i]);
        } else if (v4l2_buf.memory == V4L2_MEMORY_MMAP) {
            v4l2_buf.m.planes[i].m.userptr = (unsigned long)(buf->virt.extP[i]);
        } else {
            ALOGE("ERR(%s):invalid node->memory(%d)", __func__, v4l2_buf.memory);
            return -1;
        }
        v4l2_buf.m.planes[i].length  = (unsigned long)(buf->size.extS[i]);
    }

    ret = exynos_v4l2_qbuf(this->getFd(), &v4l2_buf);
    if (ret < 0) {
        ALOGE("ERR(%s):exynos_v4l2_qbuf failed (index:%d)(ret:%d)", __func__, index, ret);
        return -1;
    }

    return ret;
}


int SecCameraHardware::FLiteV4l2::dqbuf2(node_info_t *node)
{
    struct v4l2_buffer v4l2_buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    int ret;

    CLEAR(planes);
    CLEAR(v4l2_buf);

    v4l2_buf.type       = node->type;
    v4l2_buf.memory     = node->memory;
    v4l2_buf.m.planes   = planes;
    v4l2_buf.length     = node->planes;

    ret = exynos_v4l2_dqbuf(node->fd, &v4l2_buf);
    if (ret < 0) {
        if (ret != -EAGAIN)
            ALOGE("ERR(%s):VIDIOC_DQBUF failed (%d)", __func__, ret);
        return ret;
    }

    if (v4l2_buf.flags & V4L2_BUF_FLAG_ERROR) {
        ALOGE("ERR(%s):VIDIOC_DQBUF returned with error (%d)", __func__, V4L2_BUF_FLAG_ERROR);
        return -1;
    }

    return v4l2_buf.index;
}

int SecCameraHardware::FLiteV4l2::dqbufForCapture(ExynosBuffer *buf)
{
    struct v4l2_buffer v4l2_buf;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    int ret;

    CLEAR(planes);
    CLEAR(v4l2_buf);

    v4l2_buf.m.planes   = planes;
    v4l2_buf.type       = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
#ifdef USE_USERPTR
    v4l2_buf.memory     = V4L2_MEMORY_USERPTR;
#else
    v4l2_buf.memory     = V4L2_MEMORY_MMAP;
#endif
    v4l2_buf.length     = 1;

    ret = exynos_v4l2_dqbuf(this->getFd(), &v4l2_buf);
    if (ret < 0) {
        if (ret != -EAGAIN)
            ALOGE("ERR(%s):VIDIOC_DQBUF failed (%d)", __func__, ret);
        return ret;
    }

    if (v4l2_buf.flags & V4L2_BUF_FLAG_ERROR) {
        ALOGE("ERR(%s):VIDIOC_DQBUF returned with error (%d)", __func__, V4L2_BUF_FLAG_ERROR);
        return -1;
    }

    return v4l2_buf.index;
}

#ifdef FAKE_SENSOR
int SecCameraHardware::FLiteV4l2::fakeQbuf2(node_info_t *node, uint32_t index)
{
    int i;
    fakeByteData++;
    fakeByteData = fakeByteData % 0xFF;

    for (int i = 0; i < node->planes; i++) {
        memset(node->buffer[index].virt.extP[i], fakeByteData,
                node->buffer[index].size.extS[i]);

    }
    fakeIndex = index;
    return fakeIndex;
}

int SecCameraHardware::FLiteV4l2::fakeDqbuf2(node_info_t *node)
{
    return fakeIndex;
}
#endif


int SecCameraHardware::FLiteV4l2::stream(bool on)
{
    ALOGV("DEBUG(%s): stream E", __FUNCTION__);
    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    int err = ioctl(mCameraFd, on ? VIDIOC_STREAMON : VIDIOC_STREAMOFF, &type);
    CHECK_ERR_N(err, ("FLiteV4l2 stream: error %d", err));

    mStreamOn = on;

    ALOGV("FLiteV4l2 stream: stream turning %s", on ? "on" : "off");

    return 0;
}

int SecCameraHardware::FLiteV4l2::polling(bool recordingMode)
{
    int err = 0;
    struct pollfd events;
    CLEAR(events);
    events.fd = mCameraFd;
    events.events = POLLIN | POLLERR;

    const long timeOut = 1500;

    if (recordingMode) {
        const long sliceTimeOut = 66;

        for (int i = 0; i < (timeOut / sliceTimeOut); i++) {
            if (!mStreamOn)
                return 0;
            err = poll(&events, 1, sliceTimeOut);
            if (err > 0)
                break;
        }
    } else {
        err = poll(&events, 1, timeOut);
    }

    if (CC_UNLIKELY(err <= 0))
        ALOGE("FLiteV4l2 poll: error %d", err);

    return err;
}

int SecCameraHardware::FLiteV4l2::gctrl(uint32_t id, int *value)
{
    struct v4l2_control ctrl;
    CLEAR(ctrl);
    ctrl.id = id;

    int err = ioctl(mCameraFd, VIDIOC_G_CTRL, &ctrl);
    CHECK_ERR_N(err, ("FLiteV4l2 gctrl: error %d, id %#x", err, id));
    *value = ctrl.value;

    return 0;
}

int SecCameraHardware::FLiteV4l2::gctrl(uint32_t id, unsigned short *value)
{
    struct v4l2_control ctrl;
    CLEAR(ctrl);
    ctrl.id = id;

    int err = ioctl(mCameraFd, VIDIOC_G_CTRL, &ctrl);
    CHECK_ERR_N(err, ("FLiteV4l2 gctrl: error %d, id %#x", err, id));
    *value = (unsigned short)ctrl.value;

    return 0;
}

int SecCameraHardware::FLiteV4l2::gctrl(uint32_t id, char *value)
{
    struct v4l2_ext_controls ctrls;
    struct v4l2_ext_control ctrl;

    CLEAR(ctrls);
    ctrls.ctrl_class = V4L2_CTRL_CLASS_CAMERA;
    ctrls.count = 1;
    ctrls.controls = &ctrl;

    CLEAR(ctrl);
    ctrl.id = id;
    ctrl.string = value;

    int err = ioctl(mCameraFd, VIDIOC_G_EXT_CTRLS, &ctrls);
    CHECK_ERR_N(err, ("FLiteV4l2 gctrl: error %d, id %#x", err, id));

    return 0;
}

int SecCameraHardware::FLiteV4l2::sctrl(uint32_t id, int value)
{
    struct v4l2_control ctrl;
    CLEAR(ctrl);
    ctrl.id = id;
    ctrl.value = value;

    int err = ioctl(mCameraFd, VIDIOC_S_CTRL, &ctrl);
    CHECK_ERR_N(err, ("FLiteV4l2 sctrl: error %d, id %#x value %d", err, id, value));

    return 0;
}

int SecCameraHardware::FLiteV4l2::getYuvPhyaddr(int index,
                                       phyaddr_t *y,
                                       phyaddr_t *cbcr)
{
    struct v4l2_control ctrl;
    int err;

    if (y) {
        CLEAR(ctrl);
        ctrl.id = V4L2_CID_PADDR_Y;
        ctrl.value = index;

        err = ioctl(mCameraFd, VIDIOC_S_CTRL, &ctrl);
        CHECK_ERR_N(err, ("FLiteV4l2 getYuvPhyaddr: error %d, V4L2_CID_PADDR_Y", err));

        *y = ctrl.value;
    }

    if (cbcr) {
        CLEAR(ctrl);
        ctrl.id = V4L2_CID_PADDR_CBCR;
        ctrl.value = index;

        err = ioctl(mCameraFd, VIDIOC_S_CTRL, &ctrl);
        CHECK_ERR_N(err, ("FLiteV4l2 getYuvPhyaddr: error %d, V4L2_CID_PADDR_CBCR", err));

        *cbcr = ctrl.value;
    }
    return 0;
}

int SecCameraHardware::FLiteV4l2::getYuvPhyaddr(int index,
                                       phyaddr_t *y,
                                       phyaddr_t *cb,
                                       phyaddr_t *cr)
{
    struct v4l2_control ctrl;
    int err;

    if (y) {
        CLEAR(ctrl);
        ctrl.id = V4L2_CID_PADDR_Y;
        ctrl.value = index;

        err = ioctl(mCameraFd, VIDIOC_S_CTRL, &ctrl);
        CHECK_ERR_N(err, ("FLiteV4l2 getYuvPhyaddr: error %d, V4L2_CID_PADDR_Y", err));

        *y = ctrl.value;
    }

    if (cb) {
        CLEAR(ctrl);
        ctrl.id = V4L2_CID_PADDR_CB;
        ctrl.value = index;

        err = ioctl(mCameraFd, VIDIOC_S_CTRL, &ctrl);
        CHECK_ERR_N(err, ("FLiteV4l2 getYuvPhyaddr: error %d, V4L2_CID_PADDR_CB", err));

        *cb = ctrl.value;
    }

    if (cr) {
        CLEAR(ctrl);
        ctrl.id = V4L2_CID_PADDR_CR;
        ctrl.value = index;

        err = ioctl(mCameraFd, VIDIOC_S_CTRL, &ctrl);
        CHECK_ERR_N(err, ("FLiteV4l2 getYuvPhyaddr: error %d, V4L2_CID_PADDR_CR", err));

        *cr = ctrl.value;
    }

    return 0;
}

int SecCameraHardware::FLiteV4l2::reset()
{
    struct v4l2_control ctrl;
    CLEAR(ctrl);
    ctrl.id = V4L2_CID_CAMERA_RESET;
    ctrl.value = 0;

    int err = ioctl(mCameraFd, VIDIOC_S_CTRL, &ctrl);
    CHECK_ERR_N(err, ("FLiteV4l2 reset: error %d", err));

    return 0;
}

void SecCameraHardware::FLiteV4l2::forceStop()
{
    mCmdStop = 1;
    return;
}

int SecCameraHardware::FLiteV4l2::getFd()
{
    return mCameraFd;
}

#ifdef ENABLE_MC
bool SecCameraHardware::initMC()
{
    int err = -1;
    unsigned int i;
    int devNum;
    char node[30];

    struct media_link   *links = NULL;

    mMedia = exynos_media_open(MEDIA_DEV_EXT_PATH);

    if (mMedia == NULL) {
        ALOGE("ERR(%s):Cannot open media device (error : %s)", __func__, strerror(errno));
        goto err;
    } else {
        CLEAR(node);

        if (mCameraId == CAMERA_FACING_BACK) {
            /* camera sensor */
            strncpy(node, BACK_SENSOR_ENTITY_A_NM, sizeof(node) - 1);
            ALOGV("DEBUG(%s):node : %s", __func__, node);
            mSensorEntity = exynos_media_get_entity_by_name(mMedia, node, strlen(node));
            ALOGV("DEBUG(%s):mSensorEntity : 0x%p [%s]", __func__, mSensorEntity, mSensorEntity->info.name);
            CLEAR(node);

            /* mipi subdev */
            snprintf(node, sizeof(node) - 1, "%s.%d", PFX_MIPI_CSIS_SUBDEV_ENTITY_NM, PSFX_ENTITY_NUM_A);
            ALOGV("DEBUG(%s):node : %s", __func__, node);
            mMipiEntity = exynos_media_get_entity_by_name(mMedia, node, strlen(node));
            ALOGV("DEBUG(%s):mMipiEntity : 0x%p [%s]", __func__, mMipiEntity, mMipiEntity->info.name);
            CLEAR(node);

            /* fimc-lite subdev */
            snprintf(node, sizeof(node) - 1, "%s.%d", PFX_FLITE_SUBDEV_ENTITY_NM, PSFX_ENTITY_NUM_A);
            ALOGV("DEBUG(%s):node : %s", __func__, node);
            mFliteSdEntity = exynos_media_get_entity_by_name(mMedia, node, strlen(node));
            ALOGV("DEBUG(%s):mFliteSdEntity : 0x%p [%s]", __func__, mFliteSdEntity, mFliteSdEntity->info.name);
            CLEAR(node);

            /* fimc-lite videodev */
            snprintf(node, sizeof(node) - 1, "%s.%d", PFX_FLITE_VIDEODEV_ENTITY_NM, PSFX_ENTITY_NUM_A);
            ALOGV("DEBUG(%s):node : %s", __func__, node);
            mFliteVdEntity = exynos_media_get_entity_by_name(mMedia, node, strlen(node));
            ALOGV("DEBUG(%s):mFliteVdEntity : 0x%p [%s]", __func__, mFliteVdEntity, mFliteVdEntity->info.name);
            CLEAR(node);

            ALOGV("DEBUG(%s):sensor_sd : numlink : %d [%s]", __func__, mSensorEntity->num_links, mSensorEntity->info.name);
            ALOGV("DEBUG(%s):mipi_sd   : numlink : %d [%s]", __func__, mMipiEntity->num_links, mMipiEntity->info.name);
            ALOGV("DEBUG(%s):flite_sd  : numlink : %d [%s]", __func__, mFliteSdEntity->num_links, mFliteSdEntity->info.name);
            ALOGV("DEBUG(%s):flite_vd  : numlink : %d [%s]", __func__, mFliteVdEntity->num_links, mFliteVdEntity->info.name);

            /* sensor subdev to mipi subdev */
            links = mSensorEntity->links;
            if (links == NULL ||
                links->source->entity != mSensorEntity ||
                links->sink->entity != mMipiEntity) {
                ALOGE("ERR(%s):Cannot make link camera sensor to mipi", __func__);
                goto err;
            }

            if (exynos_media_setup_link(mMedia,  links->source,  links->sink, MEDIA_LNK_FL_ENABLED) < 0) {
                ALOGE("ERR(%s):Cannot make setup camera sensor to mipi", __func__);
                goto err;
            }
            ALOGV("DEBUG(%s):[LINK SUCCESS] sensor subdev to mipi subdev", __func__);

            /* mipi subdev to fimc-lite subdev */
            for (i = 0; i < mMipiEntity->num_links; i++) {
                links = &mMipiEntity->links[i];
                ALOGV("DEBUG(%s):i=%d: links->source->entity : %p, mMipiEntity : %p", __func__, i,
                        links->source->entity, mMipiEntity);
                ALOGV("DEBUG(%s):i=%d: links->sink->entity : %p, mFliteSdEntity : %p", __func__, i,
                        links->sink->entity, mFliteSdEntity);
                if (links == NULL ||
                    links->source->entity != mMipiEntity ||
                    links->sink->entity != mFliteSdEntity) {
                    continue;
                } else if (exynos_media_setup_link(mMedia,  links->source,  links->sink, MEDIA_LNK_FL_ENABLED) < 0) {
                    ALOGE("ERR(%s):Cannot make setup mipi subdev to fimc-lite subdev", __func__);
                    goto err;
                }
            }
            ALOGV("DEBUG(%s):[LINK SUCCESS] mipi subdev to fimc-lite subdev", __func__);

            /* fimc-lite subdev TO fimc-lite video dev */
            for (i = 0; i < mFliteSdEntity->num_links; i++) {
                links = &mFliteSdEntity->links[i];
                ALOGV("DEBUG(%s):i=%d: links->source->entity : %p, mFliteSdEntity : %p", __func__, i,
                    links->source->entity, mFliteSdEntity);
                ALOGV("DEBUG(%s):i=%d: links->sink->entity : %p, mFliteVdEntity : %p", __func__, i,
                    links->sink->entity, mFliteVdEntity);
                if (links == NULL ||
                    links->source->entity != mFliteSdEntity ||
                    links->sink->entity != mFliteVdEntity) {
                    continue;
                } else if (exynos_media_setup_link(mMedia,  links->source,  links->sink, MEDIA_LNK_FL_ENABLED) < 0) {
                    ALOGE("ERR(%s):Cannot make setup fimc-lite subdev to fimc-lite video dev", __func__);
                    goto err;
                }
            }
            ALOGV("DEBUG(%s):[LINK SUCCESS] fimc-lite subdev to fimc-lite video dev", __func__);

            setAllMCSubdevFormat(mFLiteSize.width, mFLiteSize.height, CAMERA_EXT_PREVIEW);

        }

    }

    return true;
err:
    if (mMedia)
        exynos_media_close(mMedia);
    mMedia = NULL;

    return false;
}
#endif

bool SecCameraHardware::setMCSubdevFormat(struct media_entity *entity,
                                         int w, int h, unsigned int pad,
                                         int ext_cam_mode)
{
    struct v4l2_subdev_format fmt;
    struct v4l2_mbus_framefmt format;
    CLEAR(fmt);
    CLEAR(format);
    ALOGV("DEBUG(%s): name %s", __func__, entity->info.name);
    entity->fd = exynos_subdev_open_devname(entity->info.name, O_RDWR);
    if (entity->fd < 0) {
        ALOGE("DEBUG(%s): failed invalid fd : %d", __func__, entity->fd);
        return false;

    } else {
        ALOGD("DEBUG(%s): successed subdev open fd:%d", __func__, entity->fd);

        format.width = w;
        format.height = h;

        switch (ext_cam_mode) {
        case CAMERA_EXT_PREVIEW:
        case CAMERA_EXT_CAPTURE_YUV:
            format.code = V4L2_MBUS_FMT_YUYV8_2X8;
            break;
        case CAMERA_EXT_CAPTURE_JPEG:
            format.code = V4L2_MBUS_FMT_JPEG_1X8;
            break;
        default:
            format.code = V4L2_MBUS_FMT_YUYV8_2X8;
            break;
        }

        fmt.pad = pad;
        fmt.which = V4L2_SUBDEV_FORMAT_ACTIVE;
        fmt.format = format;

        if (exynos_subdev_s_fmt(entity->fd, &fmt) < 0) {
            ALOGE("ERR(%s): subdev set format is failed", __func__);
            return false;
        } else {
            ALOGV("DEBUG(%s): subdev set format is successed", __func__);
        }
    }

    int fd = exynos_subdev_close(entity->fd);
    if (fd < 0)
        ALOGE("ERR(%s): failed close fd : %d", __func__, entity->fd);

    return true;
}

bool SecCameraHardware::setAllMCSubdevFormat(int w, int h, int ext_cam_mode)
{
    ALOGV("DEBUG(%s): w(%d), h(%d), ext_cam_mode(%d)", __func__, w, h, ext_cam_mode);

    /* Sensor */
    if (setMCSubdevFormat(mSensorEntity, w, h, 0, ext_cam_mode) == false) {
        ALOGE("ERR(%s): failed m_set_subdev_format mSensorEntity", __func__);
        return false;
    }

    /* MIPI-CSIS subdev set format */
    if (setMCSubdevFormat(mMipiEntity, w, h, 0, ext_cam_mode) == false) {
        ALOGE("ERR(%s): failed m_set_subdev_format mMipiEntity(0)", __func__);
        return false;
    }
    if (setMCSubdevFormat(mMipiEntity, w, h, 1, ext_cam_mode) == false) {
        ALOGE("ERR(%s): failed m_set_subdev_format mMipiEntity(1)", __func__);
        return false;
    }

    /* FIMC-LITE subdev set format */
    if (setMCSubdevFormat(mFliteSdEntity, w, h, 1, ext_cam_mode) == false) {
        ALOGE("ERR(%s): failed m_set_subdev_format mFliteSdEntity", __func__);
        return false;
    }

    return true;
}


bool SecCameraHardware::init()
{
    ALOGD("init E");

    LOG_PERFORMANCE_START(1);
    CLEAR(mFliteNode);
    CSC_METHOD cscMethod = CSC_METHOD_HW;

#ifdef ENABLE_MC
    /* init media controller */
    if (initMC() == false) {
        ALOGE("initMC X: error");
        goto fimc_out;
    }
#endif
    int err;

    if (mCameraId == CAMERA_FACING_BACK)
    //    err = mFlite.init(FLITE0_DEV_PATH, mCameraId);
        err = mFlite.init(FLITE1_DEV_PATH, mCameraId);
    else
        err = mFlite.init(FLITE2_DEV_PATH, mCameraId);

    if (CC_UNLIKELY(err < 0)) {
        ALOGE("initCamera X: error(%d), %s", err,
        //    (mCameraId == CAMERA_FACING_BACK) ? FLITE0_DEV_PATH : FLITE2_DEV_PATH);
            (mCameraId == CAMERA_FACING_BACK) ? FLITE1_DEV_PATH : FLITE2_DEV_PATH);
        goto fimc_out;
    }
    mFliteNode.fd = mFlite.getFd();
    ALOGV("mFlite fd %d", mFlite.getFd());

#if 0
    if (!mEnableDZoom) {
        err = mFimc1.init(FLITE2_DEV_PATH, mCameraId);
        if (CC_UNLIKELY(err < 0)) {
            ALOGE("initCamera X: error, %s", FLITE2_DEV_PATH);
            goto fimc1_out;
        }

        if (mFlite.mIsInternalISP) {
            mFlite.sctrl(CAM_CID_IS_S_FORMAT_SCENARIO, IS_MODE_PREVIEW_STILL);
        }
    }
#endif

    setExifFixedAttribute();

    /* init CSC (fimc0, fimc1) */
    mFimc1CSC = csc_init(cscMethod);
    if (mFimc1CSC == NULL)
        ALOGE("ERR(%s): mFimc1CSC csc_init() fail", __func__);
    err = csc_set_hw_property(mFimc1CSC, CSC_HW_PROPERTY_FIXED_NODE, FIMC1_NODE_NUM);
    if (err != 0)
        ALOGE("ERR(%s): fimc0 open failed %d", __func__, err);

    mFimc2CSC = csc_init(cscMethod);
    if (mFimc2CSC == NULL)
        ALOGE("ERR(%s): mFimc2CSC csc_init() fail", __func__);
    err = csc_set_hw_property(mFimc2CSC, CSC_HW_PROPERTY_FIXED_NODE, FIMC2_NODE_NUM);
    if (err != 0)
        ALOGE("ERR(%s): fimc1 open failed %d", __func__, err);

    LOG_PERFORMANCE_END(1, "total");
    return ISecCameraHardware::init();

fimc_out:
#if IS_FW_DEBUG
    if ((mCameraId == CAMERA_FACING_FRONT) || (mCameraId == FD_SERVICE_CAMERA_ID)) {
        mPrevOffset = 0;
        mCurrOffset = 0;
        mPrevWp = 0;
        mCurrWp = 0;
        mDebugVaddr = 0;

        int ret = nativeGetDebugAddr(&mDebugVaddr);
        if (ret < 0) {
            ALOGE("ERR(%s):Fail on SecCamera->getDebugAddr()", __func__);
            mPrintDebugEnabled = false;
        } else {
            mPrintDebugEnabled = true;
            ISecCameraHardware::printDebugFirmware();
            munmap((void *)mDebugVaddr, FIMC_IS_FW_MMAP_REGION_SIZE);
            if (m_mem_fd >= 0)
                close(m_mem_fd);
            m_mem_fd = -1;
            mPrintDebugEnabled = false;
        }
    }
#endif

    mFlite.deinit();
    return false;
}

void SecCameraHardware::initDefaultParameters()
{
    char str[16];
    CLEAR(str);
    if (mCameraId == CAMERA_FACING_BACK) {
        snprintf(str, sizeof(str), "%f", (double)Exif::DEFAULT_BACK_FOCAL_LEN_NUM/
            Exif::DEFAULT_BACK_FOCAL_LEN_DEN);
        mParameters.set(CameraParameters::KEY_FOCAL_LENGTH, str);
        mParameters.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "59.6");
        mParameters.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "46.3");
    } else {
        snprintf(str, sizeof(str), "%f", (double)Exif::DEFAULT_FRONT_FOCAL_LEN_NUM/
            Exif::DEFAULT_FRONT_FOCAL_LEN_DEN);
        mParameters.set(CameraParameters::KEY_FOCAL_LENGTH, str);
        mParameters.set(CameraParameters::KEY_HORIZONTAL_VIEW_ANGLE, "54.7");
        mParameters.set(CameraParameters::KEY_VERTICAL_VIEW_ANGLE, "42.3");
    }

    ISecCameraHardware::initDefaultParameters();
}

void SecCameraHardware::release()
{
    ALOGD("release E");
    ExynosBuffer nullBuf;
    int i = 0;

    ISecCameraHardware::release();

    mFlite.deinit();

    /* flite buffer free */
    for (i = 0; i < FLITE_BUF_CNT; i++) {
        freeMemSinglePlane(&mFliteNode.buffer[i], 0);
        mFliteNode.buffer[i] = nullBuf;
    }

    /* capture buffer free */
    for (i = 0; i < SKIP_CAPTURE_CNT; i++) {
        freeMemSinglePlane(&mPictureBufDummy[i], 0);
        mPictureBufDummy[i] = nullBuf;
    }
    freeMemSinglePlane(&mPictureBuf, 0);
    mPictureBuf = nullBuf;

    mInitRecSrcQ();
    mInitRecDstBuf();

    /* deinit CSC (fimc0, fimc1) */
    if (mFimc1CSC)
        csc_deinit(mFimc1CSC);
    mFimc1CSC = NULL;

    if (mFimc2CSC)
        csc_deinit(mFimc2CSC);
    mFimc2CSC = NULL;

#ifdef ENABLE_MC
    if (mMedia)
        exynos_media_close(mMedia);
#endif
    mMedia = NULL;
}

bool SecCameraHardware::nativeCreateSurface(uint32_t width, uint32_t height, uint32_t halPixelFormat)
{
    ALOGV("nativeCreateSurfaces: E");
    int min_bufs;

    if (CC_UNLIKELY(mFlagANWindowRegister == true)) {
        ALOGI("Surface already exist!!");
        return true;
    }

    status_t err;

    if (mPreviewWindow->get_min_undequeued_buffer_count(mPreviewWindow, &min_bufs)) {
        ALOGE("%s: could not retrieve min undequeued buffer count", __FUNCTION__);
        return INVALID_OPERATION;
    }

    if (min_bufs >= PREVIEW_BUF_CNT) {
        ALOGE("%s: warning, min undequeued buffer count %d is too high (expecting at most %d)", __FUNCTION__,
             min_bufs, PREVIEW_BUF_CNT - 1);
    }

    ALOGD("%s: setting buffer count to %d", __FUNCTION__, PREVIEW_BUF_CNT);
    if (mPreviewWindow->set_buffer_count(mPreviewWindow, PREVIEW_BUF_CNT)) {
        ALOGE("%s: could not set buffer count", __FUNCTION__);
        return INVALID_OPERATION;
    }

    if (mPreviewWindow->set_usage(mPreviewWindow, GRALLOC_SET_USAGE_FOR_CAMERA) != 0) {
        ALOGE("ERR(%s):could not set usage on gralloc buffer", __func__);
        return INVALID_OPERATION;
    }

    ALOGD("DEBUG(%s): %d/%d", __FUNCTION__, width, height);
    if (mPreviewWindow->set_buffers_geometry(mPreviewWindow,
                                width, height,
                                halPixelFormat)) {
        ALOGE("%s: could not set buffers geometry ", __FUNCTION__);
        return INVALID_OPERATION;
    }

    mFlagANWindowRegister = true;

    return true;
}

bool SecCameraHardware::nativeDestroySurface(void)
{
    ALOGV("nativeDestroySurface: E");
    mFlagANWindowRegister = false;

    return true;
}

bool SecCameraHardware::nativeFlushSurface(uint32_t width, uint32_t height, uint32_t size, uint32_t index)
{
    /* ALOGV("%s: width=%d, height=%d", __FUNCTION__, width, height); */
    ExynosBuffer dstBuf;
    buffer_handle_t *buf_handle = NULL;

    if (CC_UNLIKELY(!mPreviewWindowSize.width)) {
        ALOGE("nativeFlushSurface: error, Preview window %dx%d", mPreviewWindowSize.width, mPreviewWindowSize.height);
        return false;
    }

    if (CC_UNLIKELY(mFlagANWindowRegister == false)) {
        ALOGE("%s::mFlagANWindowRegister == false fail", __FUNCTION__);
        return false;
    }

    if (mPreviewWindow && mGrallocHal) {
        int stride;
        if (0 != mPreviewWindow->dequeue_buffer(mPreviewWindow, &buf_handle, &stride)) {
            ALOGE("Could not dequeue gralloc buffer!\n");
            return false;
        } else {
            if (mPreviewWindow->lock_buffer(mPreviewWindow, buf_handle) != 0)
                ALOGE("ERR(%s):Could not lock gralloc buffer !!", __func__ );
        }

        unsigned int vaddr[3];
        if (!mGrallocHal->lock(mGrallocHal,
                               *buf_handle,
                               GRALLOC_LOCK_FOR_CAMERA,
                               0, 0, width, height, (void **)vaddr)) {

            /* csc start flite(s) -> fimc0 -> gralloc(d) */
            if (mFimc1CSC) {
                /* set zoom info */
                mPreviewZoomRect.w = ALIGN_DOWN((mFLiteSize.width * 10 / mZoomValue), 2);
                mPreviewZoomRect.h = ALIGN_DOWN((mFLiteSize.height * 10 / mZoomValue), 2);

                mPreviewZoomRect.x = ALIGN_DOWN(((mFLiteSize.width - mPreviewZoomRect.w) / 2), 2);
                mPreviewZoomRect.y = ALIGN_DOWN(((mFLiteSize.height - mPreviewZoomRect.h) / 2), 2);

                /* src : FLite */
                csc_set_src_format(mFimc1CSC,
                        mFLiteSize.width, mFLiteSize.height,
                        mPreviewZoomRect.x, mPreviewZoomRect.y,
                        mPreviewZoomRect.w, mPreviewZoomRect.h,
                        V4L2_PIX_2_HAL_PIXEL_FORMAT(mFliteNode.format),
                        0);

                csc_set_src_buffer(mFimc1CSC,
                                   (void **)mFliteNode.buffer[index].fd.extFd, CSC_MEMORY_DMABUF);

            /*    mSaveDump("/data/camera_recording%d.yuv", &mFliteNode.buffer[index], index); */

                int halPixelFormat = HAL_PIXEL_FORMAT_EXYNOS_YCrCb_420_SP_FULL;

                /* when recording, narrow color range will be applied */
                if (mMovieMode) 
                    halPixelFormat = V4L2_PIX_2_HAL_PIXEL_FORMAT(V4L2_PIX_FMT_NV12); 
                
                /* dst : GRALLOC */
                csc_set_dst_format(mFimc1CSC,
                        mPreviewSize.width, mPreviewSize.height,
                        0, 0,
                        mPreviewSize.width, mPreviewSize.height,
                        halPixelFormat,
                        0);

                const private_handle_t *priv_handle = private_handle_t::dynamicCast(*buf_handle);
                dstBuf.fd.extFd[0]  = priv_handle->fd;
                dstBuf.fd.extFd[1]  = priv_handle->fd1;
                dstBuf.virt.extP[0] = (char *)vaddr[0];
                dstBuf.virt.extP[1] = (char *)vaddr[1];
                dstBuf.size.extS[0] = mPreviewSize.width * mPreviewSize.height;
                dstBuf.size.extS[1] = mPreviewSize.width * mPreviewSize.height / 2;

                csc_set_dst_buffer(mFimc1CSC,
                                   (void **)dstBuf.fd.extFd, CSC_MEMORY_DMABUF);

                if (csc_convert(mFimc1CSC) != 0) {
                    ALOGE("ERR(%s):csc_convert(mFimc1CSC) fail", __func__);
                    goto CANCEL;
                }
            }

            mGrallocHal->unlock(mGrallocHal, *buf_handle);
        }

        if (0 != mPreviewWindow->enqueue_buffer(mPreviewWindow, buf_handle)) {
            ALOGE("Could not enqueue gralloc buffer!\n");
            return false;
        }
    } else if (!mGrallocHal) {
        ALOGE("nativeFlushSurface: gralloc HAL is not loaded");
        return false;
    }

    /* if CAMERA_MSG_PREVIEW_METADATA, prepare callback buffer */
    if (mMsgEnabled & CAMERA_MSG_PREVIEW_FRAME) {
        if (nativePreviewCallback(index, &dstBuf) < 0)
            ALOGE("ERROR(%s): nativePreviewCallback failed", __func__);
    }

    return true;

CANCEL:
    if (mPreviewWindow->cancel_buffer(mPreviewWindow, buf_handle) != 0)
        ALOGE("ERR(%s):Fail to cancel buffer", __func__);

    return false;
}

image_rect_type SecCameraHardware::nativeGetWindowSize()
{
    image_rect_type window;
    if (!mMovieMode) {
        window.width = mPreviewSize.width;
        window.height = mPreviewSize.height;
        return window;
    }

    switch (FRM_RATIO(mPreviewSize)) {
    case CAM_FRMRATIO_QCIF:
        window.width = 528;
        window.height = 432;
        break;
    case CAM_FRMRATIO_VGA:
        window.width = 640;
        window.height = 480;
        break;
    case CAM_FRMRATIO_WVGA:
        window.width = 800;
        window.height = 480;
        break;
    case CAM_FRMRATIO_D1:
        window.width = 720;
        window.height = 480;
        break;
    case CAM_FRMRATIO_HD:
        window.width = 800;
        window.height = 450;
        break;
    default:
        ALOGW("nativeGetWindowSize: invalid frame ratio %d", FRM_RATIO(mPreviewSize));
        window.width = mPreviewSize.width;
        window.height = mPreviewSize.height;
        break;
    }
    return window;
}

bool SecCameraHardware::allocatePreviewHeap()
{
    if (mPreviewHeap) {
        mPreviewHeap->release(mPreviewHeap);
        mPreviewHeap = 0;
        mPreviewHeapFd = -1;
    }

    /* not need to alloc MHB by mM2MExyMemFd for ion */

    if (mEnableDZoom) {
        mPreviewHeap = mGetMemoryCb(-1, mPreviewFrameSize, PREVIEW_BUF_CNT, &mPreviewHeapFd);
    } else {
        mPreviewHeap = mGetMemoryCb((int)mFlite.getfd(),
                        mPreviewFrameSize, PREVIEW_BUF_CNT, 0);
    }

    if (!mPreviewHeap || mPreviewHeapFd < 0) {
        ALOGE("allocatePreviewHeap: error, fail to create preview heap");
        return false;
    }

    ALOGD("allocatePreviewHeap: %dx%d, frame %dx%d",
        mOrgPreviewSize.width, mOrgPreviewSize.height, mPreviewFrameSize, PREVIEW_BUF_CNT);

    return true;
}

status_t SecCameraHardware::nativeStartPreview()
{
    ALOGV("nativeStartPreview E");

    int err;
    int formatMode;

    /* YV12 */
    ALOGD("nativeStartPreview: Preview Format = %s",
        mFliteFormat == CAM_PIXEL_FORMAT_YVU420P ? "YV12" :
        mFliteFormat == CAM_PIXEL_FORMAT_YUV420SP ? "NV21" :
        "Others");

    /* This is for VT test mode (SelfTest Mode) */
    if ((mVtMode > 0) && mFlite.mIsInternalISP && !mFullPreviewRunning)
        nativeSetParameters(CAM_CID_VT_MODE, mVtMode);

    err = mFlite.startPreview(&mFLiteSize, mFliteFormat, FLITE_BUF_CNT, mFps, mMovieMode, &mFliteNode);
    CHECK_ERR_N(err, ("nativeStartPreview: error, mFlite.start"));

    mFlite.querybuf(&mPreviewFrameSize);
    if (mPreviewFrameSize == 0) {
        ALOGE("nativeStartPreview: error, mFlite.querybuf");
        return UNKNOWN_ERROR;
    }

    if (!allocatePreviewHeap()) {
        ALOGE("nativeStartPreview: error, allocatePreviewHeap");
        return NO_MEMORY;
    }

    for (int i = 0; i < FLITE_BUF_CNT; i++) {
        err = mFlite.qbuf(i);
        CHECK_ERR_N(err, ("nativeStartPreview: error %d, mFlite.qbuf(%d)", err, i));
    }

    err = mFlite.stream(true);
    CHECK_ERR_N(err, ("nativeStartPreview: error %d, mFlite.stream", err));

    ALOGV("nativeStartPreview X");
    return NO_ERROR;
}

status_t SecCameraHardware::nativeStartPreviewZoom()
{
    ALOGV("nativeStartPreviewZoom E");

    int err;
    int formatMode;
    int i = 0;
    ExynosBuffer nullBuf;

    /* YV12 */
    ALOGE("nativeStartPreview: FLite Format = %s",
        mFliteFormat == CAM_PIXEL_FORMAT_YVU420P ? "YV12" :
        mFliteFormat == CAM_PIXEL_FORMAT_YUV420SP ? "NV21" :
        mFliteFormat == CAM_PIXEL_FORMAT_YUV422I ? "YUYV" :
        "Others");

    err = mFlite.startPreview(&mFLiteSize, mFliteFormat, FLITE_BUF_CNT, mFps, mMovieMode, &mFliteNode);
    CHECK_ERR_N(err, ("nativeStartPreviewZoom: error, mFlite.start"));
    mFliteNode.ionClient = mIonCameraClient;

    ALOGE("INFO(%s): mFliteNode.fd     [%d]"   , __func__, mFliteNode.fd);
    ALOGE("INFO(%s): mFliteNode.width  [%d]"   , __func__, mFliteNode.width);
    ALOGE("INFO(%s): mFliteNode.height [%d]"   , __func__, mFliteNode.height);
    ALOGE("INFO(%s): mFliteNode.format [%c%c%c%c]" , __func__, mFliteNode.format,
                mFliteNode.format >> 8, mFliteNode.format >> 16, mFliteNode.format >> 24);
    ALOGE("INFO(%s): mFliteNode.planes [%d]"   , __func__, mFliteNode.planes);
    ALOGE("INFO(%s): mFliteNode.buffers[%d]"   , __func__, mFliteNode.buffers);
    ALOGE("INFO(%s): mFliteNode.memory [%d]"   , __func__, mFliteNode.memory);
    ALOGE("INFO(%s): mFliteNode.type   [%d]"   , __func__, mFliteNode.type);
    ALOGE("INFO(%s): mFliteNode.ionClient [%d]", __func__, mFliteNode.ionClient);

#ifdef USE_USERPTR
    /* For FLITE buffer */
    for (i = 0; i < FLITE_BUF_CNT; i++) {
        mFliteNode.buffer[i] = nullBuf;
        /* YUV size */
        getAlignedYUVSize(mFliteFormat, mFLiteSize.width, mFLiteSize.height, &mFliteNode.buffer[i]);
        if (allocMem(mIonCameraClient, &mFliteNode.buffer[i], 1 << 1) == false) {
            ALOGE("ERR(%s):mFliteNode allocMem() fail", __func__);
            return UNKNOWN_ERROR;
        } else {
            ALOGV("DEBUG(%s): mFliteNode allocMem(%d) adr(%p), size(%d), ion(%d)", __FUNCTION__,
                i, mFliteNode.buffer[i].virt.extP[0], mFliteNode.buffer[i].size.extS[0], mIonCameraClient);
            memset(mFliteNode.buffer[i].virt.extP[0], 0, mFliteNode.buffer[i].size.extS[0]);
        }
    }
#else
    /* loop for buffer count */
    for (int i = 0; i < mFliteNode.buffers; i++) {
        err = mFlite.querybuf2(i, mFliteNode.planes, &mFliteNode.buffer[i]);
        CHECK_ERR_N(err, ("nativeStartPreviewZoom: error, mFlite.querybuf2"));
    }
#endif
    ALOGV("DEBUG(%s): mMsgEnabled(%d)", __FUNCTION__, mMsgEnabled);

    /* allocate preview callback buffer */
    mPreviewFrameSize = getAlignedYUVSize(mPreviewFormat, mOrgPreviewSize.width, mOrgPreviewSize.height, NULL);
    ALOGV("DEBUG(%s): mPreviewFrameSize(%d)(%dx%d) for callback", __FUNCTION__,
        mPreviewFrameSize, mOrgPreviewSize.width, mOrgPreviewSize.height);
    if (!allocatePreviewHeap()) {
        ALOGE("ERR(%s)(%d): allocatePreviewHeap() fail", __FUNCTION__, __LINE__);
        goto DESTROYMEM;
    }

    mPreviewZoomRect.x = 0;
    mPreviewZoomRect.y = 0;
    mPreviewZoomRect.w = mFLiteSize.width;
    mPreviewZoomRect.h = mFLiteSize.height;

    for (int i = 0; i < FLITE_BUF_CNT; i++) {
        err = mFlite.qbuf2(&(mFliteNode), i);
        CHECK_ERR_GOTO(DESTROYMEM, err, ("nativeStartPreviewZoom: error %d, mFlite.qbuf(%d)", err, i));
    }

#if !defined(USE_USERPTR)
    /* export FD */
    for (int i = 0; i < mFliteNode.buffers; i++) {
        err = mFlite.expBuf(i, mFliteNode.planes, &mFliteNode.buffer[i]);
        CHECK_ERR_N(err, ("nativeStartPreviewZoom: error, mFlite.expBuf"));
    }
#endif
    err = mFlite.stream(true);
    CHECK_ERR_GOTO(DESTROYMEM, err, ("nativeStartPreviewZoom: error %d, mFlite.stream", err));

    if (mCameraId == CAMERA_FACING_FRONT) {
        int val;
        val = mParameters.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION);
        err = nativeSetParameters(CAM_CID_BRIGHTNESS, val);
        CHECK_ERR_GOTO(DESTROYMEM, err, ("nativeStartPreviewZoom: error %d, nativeSetParameters", err));

        val = mParameters.getInt("blur");
        if (val >= 0) {
            err = nativeSetParameters(CAM_CID_BLUR, val);
            CHECK_ERR_GOTO(DESTROYMEM, err, ("nativeStartPreviewZoom: error %d, nativeSetParameters", err));
        }
    }

    ALOGV("nativeStartPreviewZoom X");
    return NO_ERROR;

DESTROYMEM:
#ifdef USE_USERPTR
    /* flite buffer free */
    for (i = 0; i < FLITE_BUF_CNT; i++) {
        freeMemSinglePlane(&mFliteNode.buffer[i], 0);
        mFliteNode.buffer[i] = nullBuf;
    }
#else
    if (mFlite.reqBufZero(&mFliteNode) < 0)
        ALOGE("ERR(%s): mFlite.reqBufZero() fail", __func__);
    for (i = 0; i < FLITE_BUF_CNT; i++)
        mFliteNode.buffer[i] = nullBuf;
#endif
    return UNKNOWN_ERROR;
}

int SecCameraHardware::nativeGetPreview()
{
    int index = -1;
    int retries, retries2;
    int ret = -1;

    retries = 3;
    /* All buffers are empty. Request a frame to the device */
retry:
    if (mFlite.polling() == 0) {
        if (retries-- <= 0)
            return -1;

        ALOGE("nativeGetPreview: no first frame!");
        nativeStopPreview();

        if (mEnableDZoom)
            ret = nativeStartPreviewZoom();
        else
            ret = nativeStartPreview();

        goto retry;
    } else {
        ret = mFlite.dqbuf2(&mFliteNode);
        index = ret;
        CHECK_ERR_N(index, ("nativeGetPreview: error, mFlite.dqbuf"));
    }

    if (mEnableDZoom)
        mRecordTimestamp = systemTime(SYSTEM_TIME_MONOTONIC);

    if (!mPreviewInitialized) {
        mPreviewInitialized = true;
        ALOGD("preview started");
    }

    return index;
}

status_t SecCameraHardware::nativePreviewCallback(int index, ExynosBuffer *grallocBuf)
{
    /* If orginal size and format(defined by app) is different to
     * changed size and format(defined by hal), do CSC again.
     * But orginal and changed size and format(defined by app) is same,
     * just copy to callback buffer from gralloc buf
     */

    ExynosBuffer dstBuf;
    dstBuf.fd.extFd[0] = mPreviewHeapFd;
    getAlignedYUVSize(mPreviewFormat, mOrgPreviewSize.width, mOrgPreviewSize.height, &dstBuf);
    char *srcAdr = NULL;
    srcAdr = (char *)mPreviewHeap->data;
    srcAdr += (index * mPreviewFrameSize);

    if (mPreviewFormat == V4L2_PIX_FMT_NV21 ||
        mPreviewFormat == V4L2_PIX_FMT_NV21M) {
        dstBuf.virt.extP[0] = srcAdr;
        dstBuf.virt.extP[1] = dstBuf.virt.extP[0] + dstBuf.size.extS[0];
    } else if (mPreviewFormat == V4L2_PIX_FMT_YVU420 ||
                mPreviewFormat == V4L2_PIX_FMT_YVU420M) {
        dstBuf.virt.extP[0] = srcAdr;
        dstBuf.virt.extP[1] = dstBuf.virt.extP[0] + dstBuf.size.extS[0];
        dstBuf.virt.extP[2] = dstBuf.virt.extP[1] + dstBuf.size.extS[1];
    }

    if (grallocBuf == NULL ||
        mOrgPreviewSize.width != mPreviewSize.width ||
        mOrgPreviewSize.height != mPreviewSize.height ||
        HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP != V4L2_PIX_2_HAL_PIXEL_FORMAT(mPreviewFormat)) {

        /* csc start flite(s) -> fimc0 -> callback(d) */
        if (mFimc2CSC) {
            /* src : FLite */
            csc_set_src_format(mFimc2CSC,
                    mFLiteSize.width, mFLiteSize.height,
                    mPreviewZoomRect.x, mPreviewZoomRect.y,
                    mPreviewZoomRect.w, mPreviewZoomRect.h,
                    V4L2_PIX_2_HAL_PIXEL_FORMAT(mFliteNode.format),
                    0);

#ifdef USE_USERPTR
            csc_set_src_buffer(mFimc2CSC, (void **)mFliteNode.buffer[index].virt.extP, CSC_MEMORY_USERPTR);
#else
            csc_set_dst_buffer(mFimc2CSC, (void **)mFliteNode.buffer[index].fd.extFd, CSC_MEMORY_DMABUF);
#endif

            /* dst : callback buffer(MHB) */
            csc_set_dst_format(mFimc2CSC,
                    mOrgPreviewSize.width, mOrgPreviewSize.height,
                    0, 0,
                    mOrgPreviewSize.width, mOrgPreviewSize.height,
                    V4L2_PIX_2_HAL_PIXEL_FORMAT(mPreviewFormat),
                    0);

            csc_set_dst_buffer(mFimc2CSC,
                               (void **)dstBuf.virt.extP, CSC_MEMORY_USERPTR);

            if (csc_convert(mFimc2CSC) != 0) {
                ALOGE("ERR(%s):csc_convert(mFimc0CSC) fail", __func__);
                return false;
            }
        } else {
            ALOGE("ERR(%s): mFimc2CSC == NULL", __func__);
            return false;
        }
    } else {
        /* just memcpy only */
        char *srcAddr = NULL;
        char *dstAddr = NULL;

        for (int plane = 0; plane < 2; plane++) {
            srcAddr = grallocBuf->virt.extP[plane];
            dstAddr = dstBuf.virt.extP[plane];
            memcpy(dstAddr, srcAddr, dstBuf.size.extS[plane]);
        }
    }
    /* mSaveDump("/data/camera_preview%d.yuv", &dstBuf, index); */

    return NO_ERROR;
}


int SecCameraHardware::nativeReleasePreviewFrame(int index)
{
#ifdef FAKE_SENSOR
    return mFlite.fakeQbuf2(&mFliteNode, index);
#else
    return mFlite.qbuf2(&mFliteNode, index);
#endif
}

void SecCameraHardware::nativeStopPreview(void)
{
    int err = 0;
    int i = 0;
    ExynosBuffer nullBuf;

    err = mFlite.stream(false);
    if (CC_UNLIKELY(err < 0))
        ALOGE("nativeStopPreview: error, mFlite.stream");

#ifdef USE_USERPTR
    /* flite buffer free */
    for (i = 0; i < FLITE_BUF_CNT; i++) {
        freeMemSinglePlane(&mFliteNode.buffer[i], 0);
        mFliteNode.buffer[i] = nullBuf;
    }
#else
    for (i = 0; i < FLITE_BUF_CNT; i++) {
        for (int j = 0; j < mFliteNode.planes; j++) {
            munmap((void *)mFliteNode.buffer[i].virt.extP[j],
                mFliteNode.buffer[i].size.extS[j]);
            ion_free(mFliteNode.buffer[i].fd.extFd[j]);
        }
        mFliteNode.buffer[i] = nullBuf;
    }

    if (mFlite.reqBufZero(&mFliteNode) < 0)
        ALOGE("ERR(%s): mFlite.reqBufZero() fail", __func__);
#endif
    ALOGD("nativeStopPreview EX");
}

bool SecCameraHardware::allocateRecordingHeap()
{
    int framePlaneSize1 = ALIGN(mVideoSize.width, 16) * ALIGN(mVideoSize.height, 16);
    int framePlaneSize2 = ALIGN(mVideoSize.width, 16) * ALIGN(mVideoSize.height, 16) / 2;

    if (mRecordingHeap != NULL) {
        mRecordingHeap->release(mRecordingHeap);
        mRecordingHeap = 0;
        mRecordHeapFd = -1;
    }
    mRecordingHeap = mGetMemoryCb(-1, sizeof(struct addrs), REC_BUF_CNT, &mRecordHeapFd);
#ifdef NOTDEFINED
    if (mRecordingHeap == NULL) {
        ALOGE("ERR(%s): mGetMemoryCb(mRecordingHeap(%d), size(%d) fail [Heap %p]",\
            __func__, REC_BUF_CNT, sizeof(struct addrs), mRecordingHeap);
        return false;
    }
#endif
    if (mRecordingHeap == NULL || mRecordHeapFd < 0) {
        ALOGE("ERR(%s): mGetMemoryCb(mRecordingHeap(%d), size(%d) fail [Heap %p, Fd %d]",\
            __func__, REC_BUF_CNT, sizeof(struct addrs), mRecordingHeap, mRecordHeapFd);
        return false;
    }

    for (int i = 0; i < REC_BUF_CNT; i++) {
        for (int j = 0; j < REC_PLANE_CNT; j++) {

            if (mRecordDstHeap[i][j] != NULL) {
                mRecordDstHeap[i][j]->release(mRecordDstHeap[i][j]);
                mRecordDstHeap[i][j] = 0;
                mRecordDstHeapFd[i][j] = -1;
            }
        }
        mRecordDstHeap[i][0] = mGetMemoryCb(-1, framePlaneSize1, 1, &(mRecordDstHeapFd[i][0]));
        mRecordDstHeap[i][1] = mGetMemoryCb(-1, framePlaneSize2, 1, &(mRecordDstHeapFd[i][1]));
        ALOGV("DEBUG(%s): mRecordDstHeap[%d][0] adr(%p), fd(%d)", __func__, i, mRecordDstHeap[i][0]->data, mRecordDstHeapFd[i][0]);
        ALOGV("DEBUG(%s): mRecordDstHeap[%d][1] adr(%p), fd(%d)", __func__, i, mRecordDstHeap[i][1]->data, mRecordDstHeapFd[i][1]);

        if (mRecordDstHeap[i][0] == NULL || mRecordDstHeapFd[i][0] <= 0 ||
            mRecordDstHeap[i][1] == NULL || mRecordDstHeapFd[i][1] <= 0) {
            ALOGE("ERR(%s): mGetMemoryCb(mRecordDstHeap[%d] size(%d/%d) fail",\
                __func__, i, framePlaneSize1, framePlaneSize2);
            return false;
        }

#ifdef NOTDEFINED
            if (mRecordDstHeap[i][j] == NULL) {
                ALOGE("ERR(%s): mGetMemoryCb(mRecordDstHeap[%d][%d], size(%d) fail",\
                    __func__, i, j, framePlaneSize);
                return false;
            }
#endif
    }

    ALOGD("DEBUG(%s): %dx%d, frame plane (%d/%d)x%d", __func__,
        ALIGN(mVideoSize.width, 16), ALIGN(mVideoSize.height, 2), framePlaneSize1, framePlaneSize2, REC_BUF_CNT);

    return true;
}

#ifdef RECORDING_CAPTURE
bool SecCameraHardware::allocateRecordingSnapshotHeap()
{
    /* init jpeg heap */
    if (mJpegHeap) {
        mJpegHeap->release(mJpegHeap);
        mJpegHeap = NULL;
    }

    mJpegHeap = mGetMemoryCb(-1, mRecordingPictureFrameSize, 1, NULL);
    if (CC_UNLIKELY(!mJpegHeap)) {
        ALOGE("allocateRecordingSnapshotHeap: error, fail to create jpeg heap");
        return UNKNOWN_ERROR;
    }

    ALOGD("allocateRecordingSnapshotHeap: jpeg %dx%d, size %d",
        mPreviewSize.width, mPreviewSize.height, mPreviewFrameSize);

    return true;
}
#endif

status_t SecCameraHardware::nativeStartRecording(void)
{
    ALOGD("nativeStartRecording E");
#ifdef NOTDEFINED
    if (mMirror) {
        nativeSetParameters(CAM_CID_HFLIP, 1, 0);
        nativeSetParameters(CAM_CID_HFLIP, 1, 1);
    } else {
        nativeSetParameters(CAM_CID_HFLIP, 0, 0);
        nativeSetParameters(CAM_CID_HFLIP, 0, 1);
    }

    uint32_t recordingTotalFrameSize;
    mFimc1.querybuf(&recordingTotalFrameSize);
    if (recordingTotalFrameSize == 0) {
        ALOGE("nativeStartPreview: error, mFimc1.querybuf");
        return UNKNOWN_ERROR;
    }

    if (!allocateRecordingHeap()) {
        ALOGE("nativeStartRecording: error, allocateRecordingHeap");
        return NO_MEMORY;
    }

    for (int i = 0; i < REC_BUF_CNT; i++) {
        err = mFimc1.qbuf(i);
        CHECK_ERR_N(err, ("nativeStartRecording: error, mFimc1.qbuf(%d)", i));
    }

    err = mFimc1.stream(true);
    CHECK_ERR_N(err, ("nativeStartRecording: error, mFimc1.stream"));
#endif
    ALOGD("nativeStartRecording X");
    return NO_ERROR;
}

/* for zoom recording */
status_t SecCameraHardware::nativeStartRecordingZoom(void)
{
    int err;
    Mutex::Autolock lock(mNativeRecordLock);

    ALOGD("nativeStartRecordingZoom E (%d/%d)", mVideoSize.width, mVideoSize.height);

    /* 1. init zoom size info */
    mRecordZoomRect.x = 0;
    mRecordZoomRect.y = 0;
    mRecordZoomRect.w = mVideoSize.width;
    mRecordZoomRect.h = mVideoSize.height;

    /* 2. init buffer var src and dst */
    mInitRecSrcQ();
    mInitRecDstBuf();

    /* 3. alloc MHB for recording dst */
    if (!allocateRecordingHeap()) {
        ALOGE("nativeStartPostRecording: error, allocateRecordingHeap");
        goto destroyMem;
    }

    ALOGD("nativeStartRecordingZoom X");
    return NO_ERROR;

destroyMem:
    mInitRecSrcQ();
    mInitRecDstBuf();
    return UNKNOWN_ERROR;
}

#ifdef NOTDEFINED
int SecCameraHardware::nativeGetRecording()
{
    int index;
    phyaddr_t y, cbcr;
    int err, retries = 3;

retry:
    err = mFimc1.polling(true);
    if (CC_UNLIKELY(err <= 0)) {
        if (mFimc1.getStreamStatus()) {
            if (!err && (retries > 0)) {
                ALOGW("nativeGetRecording: warning, wait for input (%d)", retries);
                usleep(300000);
                retries--;
                goto retry;
            }
            ALOGE("nativeGetRecording: error, mFimc1.polling err=%d cnt=%d", err, retries);
        } else {
            ALOGV("nativeGetRecording: stop getting a frame");
        }
        return UNKNOWN_ERROR;
    }

    index = mFimc1.dqbuf();
    CHECK_ERR_N(index, ("nativeGetRecording: error, mFimc1.dqbuf"));

    /* get fimc capture physical addr */
    err = mFimc1.getYuvPhyaddr(index, &y, &cbcr);
    CHECK_ERR_N(err, ("nativeGetRecording: error, mFimc1.getYuvPhyaddr"));

    Mutex::Autolock lock(mNativeRecordLock);

    if (!mRecordingHeap)
        return NO_MEMORY;

    struct record_heap *heap = (struct record_heap *)mRecordingHeap->data;
    heap[index].type = kMetadataBufferTypeCameraSource;
    heap[index].y = y;
    heap[index].cbcr = cbcr;
    heap[index].buf_index = index;

    return index;
}

int SecCameraHardware::nativeReleaseRecordingFrame(int index)
{
    return mFimc1.qbuf(index);
}

int SecCameraHardware::nativeReleasePostRecordingFrame(int index)
{
    return NO_ERROR;
}
void SecCameraHardware::nativeStopPostRecording()
{
    Mutex::Autolock lock(mNativeRecordLock);

    ALOGD("nativeStopPostRecording EX");
}
#endif

void SecCameraHardware::nativeStopRecording()
{
    Mutex::Autolock lock(mNativeRecordLock);

    mInitRecSrcQ();
    mInitRecDstBuf();

    ALOGD("nativeStopRecording EX");
}

status_t SecCameraHardware::nativeCSCRecording(rec_src_buf_t *srcBuf, int dstIdx)
{
    Mutex::Autolock lock(mNativeRecordLock);
    struct addrs *addrs;

    /* csc start flite(s) -> fimc1 -> callback(d) */
    if (mFimc2CSC) {
        /* set zoom info */
        mRecordZoomRect.w = ALIGN_DOWN((mFLiteSize.width * 10 / mZoomValue), 2);
        mRecordZoomRect.h = ALIGN_DOWN((mFLiteSize.height * 10 / mZoomValue), 2);

        mRecordZoomRect.x = ALIGN_DOWN(((mFLiteSize.width - mRecordZoomRect.w) / 2), 2);
        mRecordZoomRect.y = ALIGN_DOWN(((mFLiteSize.height - mRecordZoomRect.h) / 2), 2);

#ifdef DEBUG_RECORDING
        ALOGD("DEBUG(%s) (%d): src(%d,%d,%d,%d,%d,%d), dst(%d,%d) %d", __func__, __LINE__
            , mFLiteSize.width
            , mFLiteSize.height
            , mRecordZoomRect.x
            , mRecordZoomRect.y
            , mRecordZoomRect.w
            , mRecordZoomRect.h
            , mVideoSize.width
            , mVideoSize.height
            , dstIdx
        );
#endif
        /* src : FLite */
        csc_set_src_format(mFimc2CSC,
                mFLiteSize.width, mFLiteSize.height,
                mRecordZoomRect.x, mRecordZoomRect.y,
                mRecordZoomRect.w, mRecordZoomRect.h,
                V4L2_PIX_2_HAL_PIXEL_FORMAT(mFliteNode.format),
                0);

        csc_set_src_buffer(mFimc2CSC, (void **)srcBuf->buf->virt.extP, CSC_MEMORY_USERPTR);

        /* dst : MHB(callback */
        csc_set_dst_format(mFimc2CSC,
                mVideoSize.width, mVideoSize.height,
                0, 0, mVideoSize.width, mVideoSize.height,
                V4L2_PIX_2_HAL_PIXEL_FORMAT(mRecordingFormat),
                /* HAL_PIXEL_FORMAT_CUSTOM_YCbCr_420_SP, */
                0);

        ExynosBuffer dstBuf;
        getAlignedYUVSize(mRecordingFormat, mVideoSize.width, mVideoSize.height, &dstBuf);
        for (int i = 0; i < REC_PLANE_CNT; i++) {
            dstBuf.virt.extP[i] = (char *)mRecordDstHeap[dstIdx][i]->data;
            dstBuf.fd.extFd[i] = mRecordDstHeapFd[dstIdx][i];
        }

#ifdef DEBUG_RECORDING
        ALOGD("DEBUG(%s) (%d): dst(%d,%d,%p,%p,%d,%d) index(%d)", __func__, __LINE__
            , dstBuf.fd.extFd[0]
            , dstBuf.fd.extFd[1]
            , dstBuf.virt.extP[0]
            , dstBuf.virt.extP[1]
            , dstBuf.size.extS[0]
            , dstBuf.size.extS[1]
            , dstIdx
        );
#endif
        csc_set_dst_buffer(mFimc2CSC, (void **)dstBuf.fd.extFd, CSC_MEMORY_DMABUF);

        if (csc_convert(mFimc2CSC) != 0) {
            ALOGE("ERR(%s):csc_convert(mFimc2CSC) fail", __func__);
            return false;
        }

        addrs = (struct addrs *)mRecordingHeap->data;
        addrs[dstIdx].type      = kMetadataBufferTypeCameraSource;
        addrs[dstIdx].fd_y      = (unsigned int)dstBuf.fd.extFd[0];
        addrs[dstIdx].fd_cbcr   = (unsigned int)dstBuf.fd.extFd[1];
        addrs[dstIdx].buf_index = dstIdx;
        /* ALOGV("DEBUG(%s): After CSC Camera Meta index %d fd(%d, %d)", __func__, dstIdx, addrs[dstIdx].fd_y, addrs[dstIdx].fd_cbcr); */
        /* mSaveDump("/data/camera_recording%d.yuv", &dstBuf, dstIdx); */
      } else {
        ALOGE("ERR(%s): mFimc1CSC == NULL", __func__);
        return false;
    }
    return true;
}

status_t SecCameraHardware::nativeCSCCapture(ExynosBuffer *srcBuf, ExynosBuffer *dstBuf)
{
    if (mFimc1CSC) {
        /* set zoom info */
        mPictureZoomRect.w = ALIGN_DOWN((mFLiteCaptureSize.width * 10 / mZoomValue), 2);
        mPictureZoomRect.h = ALIGN_DOWN((mFLiteCaptureSize.height * 10 / mZoomValue), 2);

        mPictureZoomRect.x = ALIGN_DOWN(((mFLiteCaptureSize.width - mPictureZoomRect.w) / 2), 2);
        mPictureZoomRect.y = ALIGN_DOWN(((mFLiteCaptureSize.height - mPictureZoomRect.h) / 2), 2);

#ifdef DEBUG_CAPTURE
        ALOGD("DEBUG(%s) (%d): (%d, %d), (%d, %d), (%d, %d, %d, %d)", __func__, __LINE__
            , mFLiteCaptureSize.width
            , mFLiteCaptureSize.height
            , mPictureSize.width
            , mPictureSize.height
            , mPictureZoomRect.x
            , mPictureZoomRect.y
            , mPictureZoomRect.w
            , mPictureZoomRect.h
        );
#endif
        /* src : FLite */
        csc_set_src_format(mFimc1CSC,
                mFLiteCaptureSize.width, mFLiteCaptureSize.height,
                mPictureZoomRect.x, mPictureZoomRect.y,
                mPictureZoomRect.w, mPictureZoomRect.h,
                V4L2_PIX_2_HAL_PIXEL_FORMAT(mFliteNode.format),
                0);

        csc_set_src_buffer(mFimc1CSC, (void **)srcBuf->virt.extP, CSC_MEMORY_USERPTR);

        /* dst : buffer */
        csc_set_dst_format(mFimc1CSC,
                mPictureSize.width, mPictureSize.height,
                0, 0, mPictureSize.width, mPictureSize.height,
                V4L2_PIX_2_HAL_PIXEL_FORMAT(mFliteNode.format),
                0);

        csc_set_dst_buffer(mFimc1CSC, (void **)dstBuf->fd.extFd, CSC_MEMORY_DMABUF);

        if (csc_convert(mFimc1CSC) != 0) {
            ALOGE("ERR(%s):csc_convert(mFimc0CSC) fail", __func__);
            return false;
        }
        /* mSaveDump("/data/camera_recording%d.yuv", &dstBuf, dstIdx); */
    } else {
        ALOGE("ERR(%s): mFimc1CSC == NULL", __func__);
        return false;
    }
    return true;
}

status_t SecCameraHardware::nativeSetZoomRatio(int value)
{
    /* Calculation of the crop information */
    int Zoom = value + 10;
    ALOGD("%s: Zoomlevel = %d, Zoom coeficient = %d", __FUNCTION__, value, Zoom);

    mZoomValue = Zoom;

    /* ALOGD("x = %d, y = %d, w = %d, h = %d", mPreviewZoomRect.x, mPreviewZoomRect.y, mPreviewZoomRect.w, mPreviewZoomRect.h); */

    return NO_ERROR;
}

#ifdef NOTDEFINED
int SecCameraHardware::nativePostPreview(int index)
{
    /* ALOGD("x = %d, y = %d, w = %d, h = %d", mPreviewZoomRect.x, mPreviewZoomRect.y, mPreviewZoomRect.w, mPreviewZoomRect.h); */
    mPreviewZoomRect.w = mCameraSize.width * 10 / mZoomValue;
    mPreviewZoomRect.h = mCameraSize.height * 10 / mZoomValue;

    if (mPreviewZoomRect.w % 2)
        mPreviewZoomRect.w -= 1;

    if (mPreviewZoomRect.h % 2)
        mPreviewZoomRect.h -= 1;

    mPreviewZoomRect.x = (mCameraSize.width - mPreviewZoomRect.w) / 2;
    mPreviewZoomRect.y = (mCameraSize.height - mPreviewZoomRect.h) / 2;

    if (mPreviewZoomRect.x % 2)
        mPreviewZoomRect.x -= 1;

    if (mPreviewZoomRect.y % 2)
        mPreviewZoomRect.y -= 1;

    int paddr[3];
    paddr[0] = mPostPhyAddr + mPreviewFrameSize * index;

    if (mPreviewFormat == CAM_PIXEL_FORMAT_YVU420P) {
        paddr[2] = paddr[0] + mPreviewSize.width * mPreviewSize.height;
        paddr[1] = paddr[2] + mPreviewSize.width * mPreviewSize.height / 4;
    } else if (mPreviewFormat == CAM_PIXEL_FORMAT_YUV420SP) {
        paddr[1] = paddr[0] + mPreviewSize.width * mPreviewSize.height;
        paddr[2] = 0;
    } else {
        ALOGE("unsupported preview format (%c%c%c%c)", mPreviewFormat,
            mPreviewFormat >> 8, mPreviewFormat >> 16, mPreviewFormat >> 24);
        return BAD_VALUE;
    }

    return NO_ERROR;
}

int SecCameraHardware::nativePostRecord(int index)
{
    int err;

    Mutex::Autolock lock(mNativeRecordLock);

    mRecordZoomRect.w = mCameraSize.width * 10 / mZoomValue;
    mRecordZoomRect.h = mCameraSize.height * 10 / mZoomValue;

    if (mRecordZoomRect.w % 2)
        mRecordZoomRect.w -= 1;

    if (mRecordZoomRect.h % 2)
        mRecordZoomRect.h -= 1;

    mRecordZoomRect.x = (mCameraSize.width - mRecordZoomRect.w) / 2;
    mRecordZoomRect.y = (mCameraSize.height - mRecordZoomRect.h) / 2;

    if (mRecordZoomRect.x % 2)
        mRecordZoomRect.x -= 1;

    if (mRecordZoomRect.y % 2)
        mRecordZoomRect.y -= 1;

    int paddr[3];
    paddr[0] = mPostRecPhyAddr[index];
    paddr[1] = paddr[0] + ALIGN(mVideoSize.width * mVideoSize.height, 64*1024);

    struct record_heap *heap = (struct record_heap *)mRecordingHeap->data;
    heap[index].type = kMetadataBufferTypeCameraSource;
    heap[index].y = paddr[0];
    heap[index].cbcr = paddr[1];
    heap[index].buf_index = index;

    return NO_ERROR;
}
#endif

#ifdef RECORDING_CAPTURE
bool SecCameraHardware::nativeGetRecordingJpeg(uint8_t *rawSrc, uint32_t srcSize, uint32_t width, uint32_t height)
{
    int ret;
    int jpegFd = api_jpeg_encode_init();
    CHECK_ERR(jpegFd, ("getEncodedJpeg: error %d, api_jpeg_encode_init", jpegFd));

    int jpegSrcFrameSize;
    uint8_t *inBuf, *outBuf, *jpeg, *thumb;
    int jpegSize, thumbSize;
    struct jpeg_enc_param jpegParam;

    Exif exif(mCameraId);
    uint32_t exifSize;

    sp<MemoryHeapBase> rawThumbnail;
    bool thumbnail = false;

    /* Thumbnail */
    LOG_PERFORMANCE_START(1);

    if (mThumbnailSize.width == 0 || mThumbnailSize.height == 0)
        goto encode_jpeg;

    jpegSrcFrameSize = mThumbnailSize.width*mThumbnailSize.height*2;
    rawThumbnail = new MemoryHeapBase(jpegSrcFrameSize);
    if (CC_UNLIKELY(!initialized(rawThumbnail))) {
        ALOGE("getJpeg: error, could not initialize raw thumb heap");
        return false;
    }

    LOG_PERFORMANCE_START(3);

    ALOGD("%s: scaleDownYuv422, src addr=%d(0x%X), size=%d, dest addr=%d(0x%X),  size=%d",
            __FUNCTION__, rawSrc, rawSrc, srcSize, (uint8_t *)rawThumbnail->base(), (uint8_t *)rawThumbnail->base(), rawThumbnail->getSize());
    scaleDownYuv422((uint8_t *)rawSrc, (int)width, (int)height, (uint8_t *)rawThumbnail->base(),
                    (int)mThumbnailSize.width, (int)mThumbnailSize.height);
    LOG_PERFORMANCE_END(3, "scaleDownYuv422");

    CLEAR(jpegParam);
    jpegParam.width = mThumbnailSize.width;
    jpegParam.height = mThumbnailSize.height;
    jpegParam.in_fmt = YUV_422;
    jpegParam.out_fmt = JPEG_422;
    jpegParam.quality = 42;
    api_jpeg_set_encode_param(&jpegParam);

    inBuf = (uint8_t *)api_jpeg_get_encode_in_buf(jpegFd, jpegSrcFrameSize);
    if (CC_UNLIKELY(!inBuf)) {
        ALOGW("getEncodedJpeg: error, thumbnail api_jpeg_get_encode_in_buf");
        goto encode_jpeg;
    }
    memcpy(inBuf, rawThumbnail->base(), jpegSrcFrameSize);

    outBuf = (uint8_t *)api_jpeg_get_encode_out_buf(jpegFd);
    if (CC_UNLIKELY(!outBuf)) {
        ALOGW("getEncodedJpeg: error, thumbnail api_jpeg_get_encode_out_buf");
        goto encode_jpeg;
    }

    ret = api_jpeg_encode_exe(jpegFd, &jpegParam);
    if (CC_UNLIKELY(ret != JPEG_ENCODE_OK)) {
        ALOGE("getEncodedJpeg: error, thumbnail api_jpeg_get_encode_in_buf");
        goto encode_jpeg;
    }
    thumbSize = jpegParam.size;
    thumbnail = true;

    LOG_PERFORMANCE_END(1, "encode thumbnail");

encode_jpeg:
    /* EXIF */
    setExifChangedAttribute();

    sp<MemoryHeapBase> exifHeap = new MemoryHeapBase(EXIF_MAX_LEN);
    if (CC_UNLIKELY(!initialized(exifHeap))) {
        ALOGE("getJpeg: error, could not initialize Camera exif heap");
        return false;
    }

    if (CC_LIKELY(thumbnail))
        exifSize = exif.make(exifHeap->base(), &mExifInfo, exifHeap->getSize(), outBuf, thumbSize);
    else
        exifSize = exif.make(exifHeap->base(), &mExifInfo);

    if (CC_UNLIKELY(!exifSize)) {
        ALOGE("getJpeg: error, fail to make EXIF");
        return false;
    }

    /* Jpeg */
    LOG_PERFORMANCE_START(2);

    CLEAR(jpegParam);
    jpegParam.width = width;
    jpegParam.height = height;
    jpegParam.in_fmt = YUV_422;
    jpegParam.out_fmt = JPEG_422;
    jpegParam.quality = 90;
    api_jpeg_set_encode_param(&jpegParam);

    inBuf = (uint8_t *)api_jpeg_get_encode_in_buf(jpegFd, srcSize);
    if (CC_UNLIKELY(!inBuf)) {
        ALOGW("getEncodedJpeg: error, api_jpeg_get_encode_in_buf");
        goto out;
    }
    LOG_PERFORMANCE_START(5);
    memcpy(inBuf, rawSrc, srcSize);
    LOG_PERFORMANCE_END(5, "memcpy jpeg");
    outBuf = (uint8_t *)api_jpeg_get_encode_out_buf(jpegFd);
    if (CC_UNLIKELY(!outBuf)) {
        ALOGW("getEncodedJpeg: error, api_jpeg_get_encode_out_buf");
        goto out;
    }

    ret = api_jpeg_encode_exe(jpegFd, &jpegParam);
    if (CC_UNLIKELY(ret != JPEG_ENCODE_OK)) {
        ALOGE("getEncodedJpeg: error, api_jpeg_get_encode_in_buf");
        goto out;
    }

    jpegSize = jpegParam.size;

    mRecordingPictureFrameSize = jpegSize + exifSize;
    /* picture frame size is should be calculated before call allocateSnapshotHeap */
    if (!allocateRecordingSnapshotHeap()) {
        ALOGE("getJpeg: error, allocateSnapshotHeap");
        return UNKNOWN_ERROR;
    }

    LOG_PERFORMANCE_END(2, "encode jpeg");

    LOG_PERFORMANCE_START(4);
    jpeg = outBuf;
    memcpy(mJpegHeap->data, outBuf, 2);
    memcpy((uint8_t *)mJpegHeap->data + 2, exifHeap->base(), exifSize);
    memcpy((uint8_t *)mJpegHeap->data + 2 + exifSize, outBuf + 2, jpegSize - 2);
    LOG_PERFORMANCE_END(4, "jpeg + exif");

    api_jpeg_encode_deinit(jpegFd);
    return true;

out:
    api_jpeg_encode_deinit(jpegFd);
    return false;
}
#endif /* RECORDING_CAPTURE*/

#if IS_FW_DEBUG
int SecCameraHardware::nativeGetDebugAddr(unsigned int *vaddr)
{
    int err;
    int paddr;
    *vaddr = 0;

    err = mFlite.gctrl(V4L2_CID_IS_FW_DEBUG_REGION_ADDR, &paddr);
    CHECK_ERR_N(err, ("nativeGetDebugAddr: error %d", err));

    if (paddr <= 0) {
        ALOGE("nativeGetDebugAddr V4L2_CID_IS_FW_DEBUG_REGION_ADDR failed..");
        return NO_MEMORY;
    }
    ALOGD("nativeGetDebugAddr paddr = 0x%x", paddr);

    m_mem_fd = open(EXYNOS_MEM_DEVICE_DEV_NAME, O_RDWR);
    if (m_mem_fd < 0) {
        ALOGE("ERR(%s):Cannot open %s (error : %s)", __func__, EXYNOS_MEM_DEVICE_DEV_NAME, strerror(errno));
        return NO_MEMORY;
    }

    *vaddr = (unsigned int)mmap(0, FIMC_IS_FW_MMAP_REGION_SIZE, PROT_READ | PROT_WRITE, MAP_SHARED, m_mem_fd, paddr);

    return 0;
}
#endif

#if FRONT_ZSL
bool SecCameraHardware::allocateFullPreviewHeap()
{
    if (mFullPreviewHeap) {
        mFullPreviewHeap->release(mFullPreviewHeap);
        mFullPreviewHeap = NULL;
    }

    mFullPreviewHeap = mGetMemoryCb((int)mFimc1.getfd(),
                            mFullPreviewFrameSize, kBufferZSLCount, 0);
    if (!mFullPreviewHeap || mFullPreviewHeap->data == MAP_FAILED) {
        ALOGE("ERR(%s): heap creation fail", __func__);
        return false;
    }

    ALOGD("allocateFullPreviewHeap: %dx%d, frame %dx%d",
        mPictureSize.width, mPictureSize.height, mFullPreviewFrameSize, kBufferZSLCount);

    return true;
}

status_t SecCameraHardware::nativeStartFullPreview(void)
{
    ALOGD("nativeStartFullPreview E");

    int err;
    cam_pixel_format captureFormat = CAM_PIXEL_FORMAT_YUV422I;

    err = mFimc1.startCapture(&mPictureSize, captureFormat, kBufferZSLCount);
    CHECK_ERR_N(err, ("nativeStartFullPreview: error, mFimc1.start"));

    mFimc1.querybuf(&mFullPreviewFrameSize);
    if (mFullPreviewFrameSize == 0) {
        ALOGE("nativeStartFullPreview: error, mFimc1.querybuf");
        return UNKNOWN_ERROR;
    }

    if (!allocateFullPreviewHeap()) {
        ALOGE("nativeStartFullPreview: error, allocateFullPreviewHeap");
        return NO_MEMORY;
    }

    for (int i = 0; i < kBufferZSLCount; i++) {
        err = mFimc1.qbuf(i);
        CHECK_ERR_N(err, ("nativeStartFullPreview: error, mFimc1.qbuf(%d)", i));
    }

    rawImageMem = new MemoryHeapBase(mFullPreviewFrameSize);

    err = mFimc1.stream(true);
    CHECK_ERR_N(err, ("nativeStartFullPreview: error, mFimc1.stream"));

    ALOGD("nativeStartFullPreview X");
    return NO_ERROR;
}

int SecCameraHardware::nativeGetFullPreview()
{
    int index;
    phyaddr_t y, cbcr;
    int err;

    err = mFimc1.polling();
    CHECK_ERR_N(err, ("nativeGetFullPreview: error, mFimc1.polling"));

    index = mFimc1.dqbuf();
    CHECK_ERR_N(index, ("nativeGetFullPreview: error, mFimc1.dqbuf"));

    mJpegIndex = index;

    return index;
}

int SecCameraHardware::nativeReleaseFullPreviewFrame(int index)
{
    return mFimc1.qbuf(index);
}

void SecCameraHardware::nativeStopFullPreview()
{
    if (mFimc1.stream(false) < 0)
        ALOGE("nativeStopFullPreview X: error, mFimc1.stream(0)");

    if (mFullPreviewHeap) {
        mFullPreviewHeap->release(mFullPreviewHeap);
        mFullPreviewHeap = NULL;
    }

    rawImageMem.clear();

    ALOGD("nativeStopFullPreview EX");
}

void SecCameraHardware::nativeForceStopFullPreview()
{
    mFimc1.forceStop();
}

bool SecCameraHardware::getZSLJpeg()
{
    int ret;

    ALOGE("%s:: mJpegIndex : %d", __func__, mJpegIndex);

    memcpy( (unsigned char *)rawImageMem->base(),
            (unsigned char *)((unsigned int)mFullPreviewHeap->data + mJpegIndex * mFullPreviewFrameSize),
            mFullPreviewFrameSize );

    sp<MemoryHeapBase> thumbnailJpeg;
    sp<MemoryHeapBase> rawThumbnail;

    unsigned char *thumb;
    int thumbSize = 0;

    bool thumbnail = false;
    /* Thumbnail */
    LOG_PERFORMANCE_START(1);

    if (mThumbnailSize.width == 0 || mThumbnailSize.height == 0)
        goto encode_jpeg;

    rawThumbnail = new MemoryHeapBase(mThumbnailSize.width * mThumbnailSize.height * 2);

    LOG_PERFORMANCE_START(3);
#ifdef USE_HW_SCALER
    ret = scaleDownYUVByFIMC((unsigned char *)rawImageMem->base(),
                        (int)mPictureSize.width,
                        (int)mPictureSize.height,
                        (unsigned char *)rawThumbnail->base(),
                        (int)mThumbnailSize.width,
                        (int)mThumbnailSize.height,
                        CAM_PIXEL_FORMAT_YUV422I);

    if (!ret) {
        ALOGE("Fail to scale down YUV data for thumbnail!\n");
        goto encode_jpeg;
    }
#else
    ret = scaleDownYuv422((unsigned char *)rawImageMem->base(),
                        (int)mPictureSize.width,
                        (int)mPictureSize.height,
                        (unsigned char *)rawThumbnail->base(),
                        (int)mThumbnailSize.width,
                        (int)mThumbnailSize.height);

    if (!ret) {
        ALOGE("Fail to scale down YUV data for thumbnail!\n");
        goto encode_jpeg;
    }
#endif
    LOG_PERFORMANCE_END(3, "scaleDownYuv422");

    thumbnailJpeg = new MemoryHeapBase(mThumbnailSize.width * mThumbnailSize.height * 2);

#ifdef CHG_ENCODE_JPEG
    ret = EncodeToJpeg((unsigned char*)rawThumbnail->base(),
                        mThumbnailSize.width,
                        mThumbnailSize.height,
                        CAM_PIXEL_FORMAT_YUV422I,
                        (unsigned char*)thumbnailJpeg->base(),
                        &thumbSize,
                        90);

    if (ret != NO_ERROR) {
        ALOGE("thumbnail:EncodeToJpeg failed\n");
        goto encode_jpeg;
    }
#endif
    if (thumbSize > MAX_THUMBNAIL_SIZE) {
        ALOGE("thumbnail size is over limit\n");
        goto encode_jpeg;
    }

    thumb = (unsigned char *)thumbnailJpeg->base();
    thumbnail = true;

    LOG_PERFORMANCE_END(1, "encode thumbnail");

encode_jpeg:

    /* EXIF */
    setExifChangedAttribute();

    Exif exif(mCameraId);
    uint32_t exifSize;

    unsigned char *jpeg;
    int jpegSize = 0;
    int jpegQuality = mParameters.getInt(CameraParameters::KEY_JPEG_QUALITY);
    	
    gprintf("CameraParameters::KEY_JPEG_QUALITY = %d\n", jpegQuality);
    	

    sp<MemoryHeapBase> JpegHeap = new MemoryHeapBase(mPictureSize.width * mPictureSize.height * 2);
    sp<MemoryHeapBase> exifHeap = new MemoryHeapBase(EXIF_MAX_LEN);
    if (!initialized(exifHeap)) {
        ALOGE("getJpeg: error, could not initialize Camera exif heap");
        return false;
    }

    if (!thumbnail)
        exifSize = exif.make(exifHeap->base(), &mExifInfo);
    else
        exifSize = exif.make(exifHeap->base(), &mExifInfo, exifHeap->getSize(), thumb, thumbSize);

	gprintf("============> jpegQuality = %d\n", jpegQuality);
    
#ifdef CHG_ENCODE_JPEG
    ret = EncodeToJpeg((unsigned char*)rawImageMem->base(),
                        mPictureSize.width,
                        mPictureSize.height,
                        CAM_PIXEL_FORMAT_YUV422I,
                        (unsigned char*)JpegHeap->base(),
                        &jpegSize,
                        jpegQuality);

    if (ret != NO_ERROR) {
        ALOGE("EncodeToJpeg failed\n");
        return false;
    }
#endif
    mPictureFrameSize = jpegSize + exifSize;

    /* picture frame size is should be calculated before call allocateSnapshotHeap */
    if (!allocateSnapshotHeap()) {
        ALOGE("getJpeg: error, allocateSnapshotHeap");
        return false;
    }

    jpeg = (unsigned char *)JpegHeap->base();
    memcpy((unsigned char *)mJpegHeap->data, jpeg, 2);
    memcpy((unsigned char *)mJpegHeap->data + 2, exifHeap->base(), exifSize);
    memcpy((unsigned char *)mJpegHeap->data + 2 + exifSize, jpeg + 2, jpegSize - 2);

    return true;
}
#endif

bool SecCameraHardware::allocateSnapshotHeap()
{
    /* init jpeg heap */
    if (mJpegHeap) {
        mJpegHeap->release(mJpegHeap);
        mJpegHeap = 0;
    }

    mJpegHeap = mGetMemoryCb(-1, mPictureFrameSize, 1, 0);

    ALOGD("allocateSnapshotHeap: jpeg %dx%d, size %d",
         mPictureSize.width, mPictureSize.height, mPictureFrameSize);

    /* RAW_IMAGE or POSTVIEW_FRAME heap */
    if ((mMsgEnabled & CAMERA_MSG_RAW_IMAGE) || (mMsgEnabled & CAMERA_MSG_POSTVIEW_FRAME)) {
        mRawFrameSize = getAlignedYUVSize(mFliteFormat, mRawSize.width, mRawSize.height, NULL);
        mRawHeap = mGetMemoryCb(-1, mRawFrameSize, 1, 0);
        ALOGD("allocateSnapshotHeap: postview %dx%d, frame %d",
            mRawSize.width, mRawSize.height, mRawFrameSize);
    }

    return true;
}

void SecCameraHardware::nativeMakeJpegDump()
{
    int postviewOffset;
    ALOGV("DEBUG(%s): (%d)", __FUNCTION__, __LINE__);
    ExynosBuffer jpegBuf;
    ALOGV("DEBUG(%s): (%d)", __FUNCTION__, __LINE__);
    if (getJpegOnBack(&postviewOffset) >= 0) {
        ALOGV("DEBUG(%s): (%d)", __FUNCTION__, __LINE__);
        jpegBuf.size.extS[0] = mPictureFrameSize;
        jpegBuf.virt.extP[0] = (char *)mJpegHeap->data;
        ALOGV("DEBUG(%s): (%d)", __FUNCTION__, __LINE__);
        mSaveDump("/data/camera_%d.jpeg", &jpegBuf, 0);
        ALOGV("DEBUG(%s): (%d)", __FUNCTION__, __LINE__);
    } else {
        ALOGV("DEBUG(%s): (%d) fail!!!!", __FUNCTION__, __LINE__);
    }
}

bool SecCameraHardware::nativeStartSnapshot()
{
    ALOGD("nativeStartSnapshot E");

    int err;
    int nBufs = 1;
    int i = 0;
    ExynosBuffer nullBuf;

#if !defined(REAR_USE_YUV_CAPTURE)
    cam_pixel_format captureFormat = ((mCameraId == CAMERA_FACING_BACK) ?
                        mPictureFormat : CAM_PIXEL_FORMAT_YUV422I);
#else
    cam_pixel_format captureFormat = CAM_PIXEL_FORMAT_YUV422I;
#endif

#if FRONT_ZSL
    if (mCameraId == CAMERA_FACING_FRONT && ISecCameraHardware::mFullPreviewRunning)
        return true;
#endif
    /* This is for VT test mode (SelfTest Mode) */
    if ((mVtMode > 0) && mFlite.mIsInternalISP && !mFullPreviewRunning)
        nativeSetParameters(CAM_CID_VT_MODE, mVtMode);

    err = mFlite.startCapture(&mFLiteCaptureSize, captureFormat, nBufs);
    CHECK_ERR(err, ("nativeStartSnapshot: error, mFlite.start"));

    /* For picture buffer */
    getAlignedYUVSize(captureFormat, mPictureSize.width, mPictureSize.height, &mPictureBuf);
    if (allocMem(mIonCameraClient, &mPictureBuf, 1 << 1) == false) {
        ALOGE("ERR(%s):mPictureBuf allocMem() fail", __func__);
        return UNKNOWN_ERROR;
    } else {
        ALOGV("DEBUG(%s): mPictureBuf allocMem adr(%p), size(%d), ion(%d) w/h(%d/%d)", __FUNCTION__,
            mPictureBuf.virt.extP[0], mPictureBuf.size.extS[0], mIonCameraClient,
            mPictureSize.width, mPictureSize.height);
        memset(mPictureBuf.virt.extP[0], 0, mPictureBuf.size.extS[0]);
    }

#ifdef USE_USERPTR
    for (i = 0; i < SKIP_CAPTURE_CNT; i++) {
        getAlignedYUVSize(captureFormat, mFLiteCaptureSize.width, mFLiteCaptureSize.height, &mPictureBufDummy[i]);
        if (allocMem(mIonCameraClient, &mPictureBufDummy[i], 1 << 1) == false) {
            ALOGE("ERR(%s):mPictureBuf dummy allocMem() fail", __func__);
            return UNKNOWN_ERROR;
        } else {
            ALOGV("DEBUG(%s): mPictureBuf dummy allocMem adr(%p), size(%d), ion(%d) w/h(%d/%d)", __FUNCTION__,
                mPictureBufDummy[i].virt.extP[0], mPictureBufDummy[i].size.extS[0], mIonCameraClient,
                mFLiteCaptureSize.width, mFLiteCaptureSize.height);
            memset(mPictureBufDummy[i].virt.extP[0], 0, mPictureBufDummy[i].size.extS[0]);
        }
    }
#else
    for (i = 0; i < SKIP_CAPTURE_CNT; i++) {
        err = mFlite.querybuf2(i, mFliteNode.planes, &mPictureBufDummy[i]);
        CHECK_ERR_N(err, ("nativeStartPreviewZoom: error, mFlite.querybuf2"));
    }
#endif

    /* qbuf dummy buffer for skip */
    for (i = 0; i < SKIP_CAPTURE_CNT; i++) {
        err = mFlite.qbufForCapture(&mPictureBufDummy[i], i);
        CHECK_ERR(err, ("nativeStartSnapshot: error, mFlite.qbuf(%d)", i));
    }

#if !defined(USE_USERPTR)
    /* export FD */
    for (int i = 0; i < SKIP_CAPTURE_CNT; i++) {
        err = mFlite.expBuf(i, mFliteNode.planes, &mPictureBufDummy[i]);
        CHECK_ERR_N(err, ("nativeStartSnapshot: error, mFlite.expBuf"));
    }
#endif

    err = mFlite.stream(true);
    CHECK_ERR(err, ("nativeStartSnapshot: error, mFlite.stream"));

    ALOGD("nativeStartSnapshot X");
    return true;
}

bool SecCameraHardware::nativeGetSnapshot(int *postviewOffset)
{
    ALOGD("nativeGetSnapshot E");

    int err;
    bool retryDone = false;
    int i = 0;

#if FRONT_ZSL
    if (mCameraId == CAMERA_FACING_FRONT && ISecCameraHardware::mFullPreviewRunning)
        return getZSLJpeg();
#endif

#if NOTDEFINED
retry:
    /*
     * Put here if Capture Start Command code is additionally needed
     * in case of ISP.
     */

    /*
     * Waiting for frame(stream) to be input.
     * ex) poll()
     */
    err = mFlite.polling();
    if (CC_UNLIKELY(err <= 0)) {
#ifdef DEBUG_CAPTURE_RETRY
    LOG_FATAL("nativeGetSnapshot: fail to get a frame!");
#else
        if (!retryDone) {
            ALOGW("nativeGetSnapshot: warning. Reset the camera device");
            mFlite.stream(false);
            nativeStopSnapshot();
            mFlite.reset();
            nativeStartSnapshot();
            retryDone = true;
            goto retry;
        }
    ALOGE("nativeGetSnapshot: error, mFlite.polling");
#endif
        return false;
    }
#endif
    ALOGV("DEBUG(%s): (%d) nativeGetSnapshot dq start", __FUNCTION__, __LINE__);

    for (i = 0; i < SKIP_CAPTURE_CNT; i++) {
        int ret = mFlite.dqbufForCapture(&mPictureBufDummy[i]);
        ALOGV("DEBUG(%s) (%d): dqbufForCapture dq(%d)", __FUNCTION__, __LINE__, i);
    }

    /* last capture was stored dummy buffer due to zoom.
     * and zoom applied to capture image by fimc */
    err = nativeCSCCapture(&mPictureBufDummy[i-1], &mPictureBuf);
    CHECK_ERR_N(err, ("nativeGetSnapshot: error, nativeCSCCapture"));

    /* Stop capturing stream(or frame) data. */
    err = mFlite.stream(false);
    CHECK_ERR(err, ("nativeGetSnapshot: error, mFlite.stream"));

    /* Get Jpeg image including EXIF. */
    if (mCameraId == CAMERA_FACING_BACK)
        err = getJpegOnBack(postviewOffset);
    else
        err = getJpegOnFront(postviewOffset);

    CHECK_ERR(err, ("nativeGetSnapshot: error, getJpeg"));

    /* if RAW_IMAGE or POSTVIEW_FRAME msg is enable */
    if ((mMsgEnabled & CAMERA_MSG_RAW_IMAGE) || (mMsgEnabled & CAMERA_MSG_POSTVIEW_FRAME)) {
        memcpy((char *)mRawHeap->data, mPictureBuf.virt.extP[0], mRawFrameSize);
        ALOGV("DEBUG(%s): (%d) copied mRawHeap", __FUNCTION__, __LINE__);
    }

#ifdef DUMP_JPEG_FILE
    ExynosBuffer jpegBuf;
    ALOGV("DEBUG(%s): (%d) %d", __FUNCTION__, __LINE__, mPictureFrameSize);
    jpegBuf.size.extS[0] = mPictureFrameSize;
    jpegBuf.virt.extP[0] = (char *)mJpegHeap->data;
    ALOGV("DEBUG(%s): (%d)", __FUNCTION__, __LINE__);
    mSaveDump("/data/camera_capture%d.yuv", &jpegBuf, 1);
    ALOGV("DEBUG(%s): (%d)", __FUNCTION__, __LINE__);
#endif

    ALOGD("nativeGetSnapshot X");
    return true;
}

inline int SecCameraHardware::getJpegOnBack(int *postviewOffset)
{
    status_t ret;

#if !defined(REAR_USE_YUV_CAPTURE)
    return internalGetJpegForSocJpeg(postviewOffset);
#else
    if (mEnableDZoom)
        ret = internalGetJpegForSocYuvWithZoom(postviewOffset);
    else
        ret = internalGetJpegForSocYuv(postviewOffset);

    return ret;
#endif
}

inline int SecCameraHardware::getJpegOnFront(int *postviewOffset)
{
    status_t ret;
    ret = internalGetJpegForSocYuvWithZoom(postviewOffset);
    return ret;
}

void SecCameraHardware::nativeStopSnapshot()
{
    ExynosBuffer nullBuf;
    int i = 0;

    if (mRawHeap != NULL) {
        mRawHeap->release(mRawHeap);
        mRawHeap = 0;
    }

    freeMemSinglePlane(&mPictureBuf, 0);

#ifdef USE_USERPTR
    /* capture buffer free */
    for (i = 0; i < SKIP_CAPTURE_CNT; i++) {
        freeMemSinglePlane(&mPictureBufDummy[i], 0);
        mPictureBufDummy[i] = nullBuf;
    }
#else
    for (i = 0; i < SKIP_CAPTURE_CNT; i++) {
        for (int j = 0; j < mFliteNode.planes; j++) {
            munmap((void *)mPictureBufDummy[i].virt.extP[j],
                mPictureBufDummy[i].size.extS[j]);
            ion_free(mPictureBufDummy[i].fd.extFd[j]);
        }
        mPictureBufDummy[i] = nullBuf;
    }

    if (mFlite.reqBufZero(&mFliteNode) < 0)
        ALOGE("ERR(%s): mFlite.reqBufZero() fail", __func__);
#endif


    mPictureBuf = nullBuf;
    ALOGD("nativeStopSnapshot EX");
}

bool SecCameraHardware::nativeSetAutoFocus()
{
    ALOGV("nativeSetAutofocus E");

    int i, waitUs = 10000, tryCount = (900 * 1000) / waitUs;
    for (i = 1; i <= tryCount; i++) {
        if (mPreviewInitialized)
            break;
        else
            usleep(waitUs);

        if (!(i % 40))
            ALOGD("AF: waiting for preview\n");
    }

    if (CC_UNLIKELY(i > tryCount))
        ALOGI("cancelAutoFocus: cancel timeout");

    int err = mFlite.sctrl(V4L2_CID_CAM_SINGLE_AUTO_FOCUS, AUTO_FOCUS_ON);
    CHECK_ERR(err, ("nativeSetAutofocus X: error, mFlite.sctrl"))

    ALOGV("nativeSetAutofocus X");

    return true;
}

int SecCameraHardware::nativeGetAutoFocus()
{
    ALOGV("nativeGetAutofocus E");
    int status, i, err;
    /* AF completion takes more much time in case of night mode.
     So be careful if you modify tryCount. */
    const int tryCount = 300;

    for (i = 0; i < tryCount; i ++) {
        usleep(20000);
        err = mFlite.gctrl(V4L2_CID_CAM_AUTO_FOCUS_RESULT, &status);
        CHECK_ERR_N(err, ("nativeGetAutofocus: error %d", err));
        if (status != 0x08) break;
    }

    if (i == tryCount)
        ALOGE("nativeGetAutoFocus: error, AF hasn't been finished yet.");

    ALOGV("nativeGetAutofocus X");
    return status;
}

status_t SecCameraHardware::nativeCancelAutoFocus()
{
    ALOGV("nativeCancelAutofocus E1");
#if NOTDEFINED
    int err = mFlite.sctrl(V4L2_CID_CAMERA_SET_AUTO_FOCUS, AUTO_FOCUS_OFF);
    CHECK_ERR(err, ("nativeCancelAutofocus: error, mFlite.sctrl"))
#endif
    ALOGV("nativeCancelAutofocus X2");
    return NO_ERROR;
}

inline status_t SecCameraHardware::nativeSetParameters(cam_control_id id, int value, bool recordingMode)
{
    int err = NO_ERROR;

    if (CC_LIKELY(!recordingMode))
        err = mFlite.sctrl(id, value);
    else {
        if (mCameraId == CAMERA_FACING_FRONT)
            err = mFlite.sctrl(id, value);
    }

#if IS_FW_DEBUG
    if (err < 0 && mCameraId == CAMERA_FACING_FRONT)
        ISecCameraHardware::printDebugFirmware();
#endif

    CHECK_ERR_N(err, ("nativeSetParameters X: error %d", err))

    return NO_ERROR;
}

void SecCameraHardware::setExifFixedAttribute()
{
    CLEAR(mExifInfo);
    /* 0th IFD TIFF Tags */
    /* Maker */
    strncpy((char *)mExifInfo.maker, Exif::DEFAULT_MAKER, sizeof(mExifInfo.maker) - 1);

    /* Model */
    property_get("ro.product.model", (char *)mExifInfo.model, Exif::DEFAULT_MODEL);
    /* Software */
    property_get("ro.build.PDA", (char *)mExifInfo.software, Exif::DEFAULT_SOFTWARE);

    /* YCbCr Positioning */
    mExifInfo.ycbcr_positioning = Exif::DEFAULT_YCBCR_POSITIONING;

    /* 0th IFD Exif Private Tags */
    /* F Number */
    if (mCameraId == CAMERA_FACING_BACK) {
        mExifInfo.fnumber.num = Exif::DEFAULT_BACK_FNUMBER_NUM;
        mExifInfo.fnumber.den = Exif::DEFAULT_BACK_FNUMBER_DEN;
    } else {
        mExifInfo.fnumber.num = Exif::DEFAULT_FRONT_FNUMBER_NUM;
        mExifInfo.fnumber.den = Exif::DEFAULT_FRONT_FNUMBER_DEN;
    }
    /* Exposure Program */
    mExifInfo.exposure_program = Exif::DEFAULT_EXPOSURE_PROGRAM;
    /* Exif Version */
    memcpy(mExifInfo.exif_version, Exif::DEFAULT_EXIF_VERSION, sizeof(mExifInfo.exif_version));
    /* Aperture */
    double av = APEX_FNUM_TO_APERTURE((double)mExifInfo.fnumber.num/mExifInfo.fnumber.den);
    mExifInfo.aperture.num = av*Exif::DEFAULT_APEX_DEN;
    mExifInfo.aperture.den = Exif::DEFAULT_APEX_DEN;
    /* Maximum lens aperture */
    mExifInfo.max_aperture.num = mExifInfo.aperture.num;
    mExifInfo.max_aperture.den = mExifInfo.aperture.den;
    /* Lens Focal Length */
    if (mCameraId == CAMERA_FACING_BACK) {
        mExifInfo.focal_length.num = Exif::DEFAULT_BACK_FOCAL_LEN_NUM;
        mExifInfo.focal_length.den = Exif::DEFAULT_BACK_FOCAL_LEN_DEN;
    } else {
        mExifInfo.focal_length.num = Exif::DEFAULT_FRONT_FOCAL_LEN_NUM;
        mExifInfo.focal_length.den = Exif::DEFAULT_FRONT_FOCAL_LEN_DEN;
    }
    /* Color Space information */
    mExifInfo.color_space = Exif::DEFAULT_COLOR_SPACE;
    /* Exposure Mode */
    mExifInfo.exposure_mode = Exif::DEFAULT_EXPOSURE_MODE;

    /* 0th IFD GPS Info Tags */
    unsigned char gps_version[4] = {0x02, 0x02, 0x00, 0x00};
    memcpy(mExifInfo.gps_version_id, gps_version, sizeof(gps_version));

    /* 1th IFD TIFF Tags */
    mExifInfo.compression_scheme = Exif::DEFAULT_COMPRESSION;
    mExifInfo.x_resolution.num = Exif::DEFAULT_RESOLUTION_NUM;
    mExifInfo.x_resolution.den = Exif::DEFAULT_RESOLUTION_DEN;
    mExifInfo.y_resolution.num = Exif::DEFAULT_RESOLUTION_NUM;
    mExifInfo.y_resolution.den = Exif::DEFAULT_RESOLUTION_DEN;
    mExifInfo.resolution_unit = Exif::DEFAULT_RESOLUTION_UNIT;
}

void SecCameraHardware::setExifChangedAttribute()
{
    /* 0th IFD TIFF Tags */
    /* Width, Height */
    if (!mMovieMode) {
        mExifInfo.width = mPictureSize.width;
        mExifInfo.height = mPictureSize.height;
    } else {
        mExifInfo.width = mVideoSize.width;
        mExifInfo.height = mVideoSize.height;
    }

    /* Orientation */
    switch (mParameters.getInt(CameraParameters::KEY_ROTATION)) {
    case 0:
        mExifInfo.orientation = EXIF_ORIENTATION_UP;
        break;
    case 90:
        mExifInfo.orientation = EXIF_ORIENTATION_90;
        break;
    case 180:
        mExifInfo.orientation = EXIF_ORIENTATION_180;
        break;
    case 270:
        mExifInfo.orientation = EXIF_ORIENTATION_270;
        break;
    default:
        mExifInfo.orientation = EXIF_ORIENTATION_UP;
        break;
    }
    /* Date time */
    time_t rawtime;
    struct tm *timeinfo;
    time(&rawtime);
    timeinfo = localtime(&rawtime);
    strftime((char *)mExifInfo.date_time, 20, "%Y:%m:%d %H:%M:%S", timeinfo);

    /* 0th IFD Exif Private Tags */
    /* Exposure Time */
    int err;
    int tv;
#ifdef NOTDEFINED
    err = mFlite.gctrl(V4L2_CID_CAMERA_EXIF_TV, &tv);
    mExifInfo.exposure_time.num = 1;
    if (mCameraId == CAMERA_FACING_BACK)
        mExifInfo.exposure_time.den = APEX_SHUTTER_TO_EXPOSURE((double)tv/Exif::DEFAULT_APEX_DEN);
    else
        mExifInfo.exposure_time.den = tv;
#else

    if (mFlite.mIsInternalISP) {
        err = mFlite.gctrl(V4L2_CID_IS_CAMERA_EXIF_SHUTTERSPEED, &tv);
        mExifInfo.exposure_time.den = tv;
    } else {
        err = mFlite.gctrl(V4L2_CID_CAMERA_EXIF_EXPTIME, (int32_t *)&mExifInfo.exposure_time.den);
    }

    mExifInfo.exposure_time.num = 1;
#endif

    /* Flash */
    if (mCameraId == CAMERA_FACING_BACK)
        err = mFlite.gctrl(V4L2_CID_CAMERA_EXIF_FLASH, (unsigned short *)&mExifInfo.flash);

     /* Color Space information */
    mExifInfo.color_space = Exif::DEFAULT_COLOR_SPACE;

    /* User Comments */
    strncpy((char *)mExifInfo.user_comment, Exif::DEFAULT_USERCOMMENTS, sizeof(mExifInfo.user_comment) - 1);
    /* ISO Speed Rating */
    err = mFlite.gctrl(V4L2_CID_CAMERA_EXIF_ISO, (unsigned short *)&mExifInfo.iso_speed_rating);
    if (mCameraId == CAMERA_FACING_BACK) {
        /* Shutter Speed */
#if NOTDEFINED
        mExifInfo.shutter_speed.num = tv;
#else
        mExifInfo.shutter_speed.num = APEX_EXPOSURE_TO_SHUTTER((double)mExifInfo.exposure_time.den) * Exif::DEFAULT_APEX_DEN;
#endif
        mExifInfo.shutter_speed.den = Exif::DEFAULT_APEX_DEN;
        /* Brightness */
        int bv;
        err = mFlite.gctrl(V4L2_CID_CAMERA_EXIF_BV, &bv);
        mExifInfo.brightness.num = bv;
        mExifInfo.brightness.den = Exif::DEFAULT_APEX_DEN;
        /* Exposure Bias */
        float exposure = mParameters.getInt(CameraParameters::KEY_EXPOSURE_COMPENSATION)*
            mParameters.getFloat(CameraParameters::KEY_EXPOSURE_COMPENSATION_STEP);
        mExifInfo.exposure_bias.num = (exposure*Exif::DEFAULT_APEX_DEN);
        mExifInfo.exposure_bias.den = Exif::DEFAULT_APEX_DEN;
        /* Metering Mode */
        if (mFlite.mIsInternalISP) {
            mExifInfo.metering_mode = EXIF_METERING_AVERAGE;
        } else {
            const char *metering = mParameters.get("metering");
            if (!metering || !strcmp(metering, "center"))
                mExifInfo.metering_mode = EXIF_METERING_CENTER;
            else if (!strcmp(metering, "spot"))
                mExifInfo.metering_mode = EXIF_METERING_SPOT;
            else if (!strcmp(metering, "matrix"))
                mExifInfo.metering_mode = EXIF_METERING_AVERAGE;
        }
    }
    /* White Balance */
    const char *wb = mParameters.get(CameraParameters::KEY_WHITE_BALANCE);
    if (!wb || !strcmp(wb, CameraParameters::WHITE_BALANCE_AUTO))
        mExifInfo.white_balance = EXIF_WB_AUTO;
    else
        mExifInfo.white_balance = EXIF_WB_MANUAL;
    if (mCameraId == CAMERA_FACING_BACK) {
        /* Scene Capture Type */
        switch (mSceneMode) {
        case SCENE_MODE_PORTRAIT:
            mExifInfo.scene_capture_type = EXIF_SCENE_PORTRAIT;
            break;
        case SCENE_MODE_LANDSCAPE:
            mExifInfo.scene_capture_type = EXIF_SCENE_LANDSCAPE;
            break;
        case SCENE_MODE_NIGHTSHOT:
            mExifInfo.scene_capture_type = EXIF_SCENE_NIGHT;
            break;
        default:
            mExifInfo.scene_capture_type = EXIF_SCENE_STANDARD;
            break;
        }
    }
    /* 0th IFD GPS Info Tags */
    const char *strLatitude = mParameters.get(CameraParameters::KEY_GPS_LATITUDE);
    const char *strLogitude = mParameters.get(CameraParameters::KEY_GPS_LONGITUDE);
    const char *strAltitude = mParameters.get(CameraParameters::KEY_GPS_ALTITUDE);

    if (strLatitude != NULL && strLogitude != NULL && strAltitude != NULL) {
        if (atof(strLatitude) > 0)
            strncpy((char *)mExifInfo.gps_latitude_ref, "N", sizeof(mExifInfo.gps_latitude_ref));
        else
            strncpy((char *)mExifInfo.gps_latitude_ref, "S", sizeof(mExifInfo.gps_latitude_ref));

        if (atof(strLogitude) > 0)
            strncpy((char *)mExifInfo.gps_longitude_ref, "E", sizeof(mExifInfo.gps_longitude_ref));
        else
            strncpy((char *)mExifInfo.gps_longitude_ref, "W", sizeof(mExifInfo.gps_longitude_ref));

        if (atof(strAltitude) > 0)
            mExifInfo.gps_altitude_ref = 0;
        else
            mExifInfo.gps_altitude_ref = 1;

        double latitude = fabs(atof(strLatitude));
        double longitude = fabs(atof(strLogitude));
        double altitude = fabs(atof(strAltitude));

        mExifInfo.gps_latitude[0].num = (uint32_t)latitude;
        mExifInfo.gps_latitude[0].den = 1;
        mExifInfo.gps_latitude[1].num = (uint32_t)((latitude - mExifInfo.gps_latitude[0].num) * 60);
        mExifInfo.gps_latitude[1].den = 1;
        mExifInfo.gps_latitude[2].num = (uint32_t)(round((((latitude - mExifInfo.gps_latitude[0].num) * 60) -
                    mExifInfo.gps_latitude[1].num) * 60));
        mExifInfo.gps_latitude[2].den = 1;

        mExifInfo.gps_longitude[0].num = (uint32_t)longitude;
        mExifInfo.gps_longitude[0].den = 1;
        mExifInfo.gps_longitude[1].num = (uint32_t)((longitude - mExifInfo.gps_longitude[0].num) * 60);
        mExifInfo.gps_longitude[1].den = 1;
        mExifInfo.gps_longitude[2].num = (uint32_t)(round((((longitude - mExifInfo.gps_longitude[0].num) * 60) -
                    mExifInfo.gps_longitude[1].num) * 60));
        mExifInfo.gps_longitude[2].den = 1;

        mExifInfo.gps_altitude.num = (uint32_t)altitude;
        mExifInfo.gps_altitude.den = 1;

        const char *strTimestamp = mParameters.get(CameraParameters::KEY_GPS_TIMESTAMP);
        long timestamp = 0;
        if (strTimestamp)
            timestamp = atol(strTimestamp);

        struct tm tm_data;
        gmtime_r(&timestamp, &tm_data);
        mExifInfo.gps_timestamp[0].num = tm_data.tm_hour;
        mExifInfo.gps_timestamp[0].den = 1;
        mExifInfo.gps_timestamp[1].num = tm_data.tm_min;
        mExifInfo.gps_timestamp[1].den = 1;
        mExifInfo.gps_timestamp[2].num = tm_data.tm_sec;
        mExifInfo.gps_timestamp[2].den = 1;
        strftime((char *)mExifInfo.gps_datestamp, sizeof(mExifInfo.gps_datestamp), "%Y:%m:%d", &tm_data);

        const char *progressingMethod = mParameters.get(CameraParameters::KEY_GPS_PROCESSING_METHOD);
        if (progressingMethod) {
            size_t len = strlen(progressingMethod);
            if (len >= sizeof(mExifInfo.gps_processing_method))
                len = sizeof(mExifInfo.gps_processing_method) - 1;

            CLEAR(mExifInfo.gps_processing_method);
            strncpy((char *)mExifInfo.gps_processing_method, progressingMethod, len);
        }

        mExifInfo.enableGps = true;
    } else {
        mExifInfo.enableGps = false;
    }

    /* 1th IFD TIFF Tags */
    mExifInfo.widthThumb = mThumbnailSize.width;
    mExifInfo.heightThumb = mThumbnailSize.height;

}

int SecCameraHardware::internalGetJpegForISP(int *postviewOffset)
{
    int jpegSize, jpegOffset, thumbSize, thumbOffset;
    int err;

    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_MAIN_SIZE, &jpegSize);
    CHECK_ERR(err, ("getJpeg: error %d, jpeg size", err));

    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_MAIN_OFFSET, &jpegOffset);
    CHECK_ERR(err, ("getJpeg: error %d, jpeg offset", err));

    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_THUMB_SIZE, &thumbSize);
    CHECK_ERR(err, ("getJpeg: error %d, thumb size", err));

    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_THUMB_OFFSET, &thumbOffset);
    CHECK_ERR(err, ("getJpeg: error %d, thumb offset", err));

     /* EXIF */
    LOG_PERFORMANCE_START(1);

    setExifChangedAttribute();

    sp<MemoryHeapBase> exifHeap = new MemoryHeapBase(EXIF_MAX_LEN);
    if (!initialized(exifHeap)) {
        ALOGE("getJpeg: error, could not initialize Camera exif heap");
        return UNKNOWN_ERROR;
    }

    Exif exif(mCameraId);
    uint32_t exifSize;
    uint8_t *thumb = (uint8_t *)mRawHeap->data + thumbOffset;
    if (mThumbnailSize.width == 0 || mThumbnailSize.height == 0)
        exifSize = exif.make(exifHeap->base(), &mExifInfo);
    else
        exifSize = exif.make(exifHeap->base(), &mExifInfo, exifHeap->getSize(),thumb, thumbSize);
    if (CC_UNLIKELY(!exifSize)) {
        ALOGE("getJpeg: error, fail to make EXIF");
        return UNKNOWN_ERROR;
    }

    /* Jpeg */
    mPictureFrameSize = jpegSize + exifSize;
    /* picture frame size is should be calculated before call allocateSnapshotHeap */
    if (!allocateSnapshotHeap()) {
        ALOGE("getJpeg: error, allocateSnapshotHeap");
        return UNKNOWN_ERROR;
    }

    uint8_t *jpeg = (uint8_t *)mRawHeap->data + jpegOffset;
    memcpy(mJpegHeap->data, jpeg, 2);
    memcpy((uint8_t *)mJpegHeap->data + 2, exifHeap->base(), exifSize);
    memcpy((uint8_t *)mJpegHeap->data + 2 + exifSize, jpeg + 2, jpegSize - 2);

    LOG_PERFORMANCE_END(1, "jpeg + exif");

    /* Postview */
    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET, postviewOffset);
    CHECK_ERR(err, ("getJpeg: error %d, postview offset", err));

    return 0;
}

#if !defined(REAR_USE_YUV_CAPTURE)
int SecCameraHardware::internalGetJpegForSocJpeg(int *postviewOffset)
{
    int32_t maxJpegSize, jpegOffset, rawThumbSize, rawThumbOffset;

    int err;

    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_MAIN_SIZE, &maxJpegSize);
    CHECK_ERR(err, ("getJpeg: error %d, jpeg size", err));

    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_MAIN_OFFSET, &jpegOffset);
    CHECK_ERR(err, ("getJpeg: error %d, jpeg offset", err));

    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_THUMB_SIZE, &rawThumbSize);
    CHECK_ERR(err, ("getJpeg: error %d, thumb size", err));

    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_THUMB_OFFSET, &rawThumbOffset);
    CHECK_ERR(err, ("getJpeg: error %d, thumb offset", err));

    ALOGV("mRawHeap addr=0x%x size=%d, maxJpegSize=%d, rawThumbSize=%d", (uint32_t)(mRawHeap->data), mRawHeap->getSize(),
            maxJpegSize, rawThumbSize);
    sp<MemoryHeapBase> jpegHeap = new MemoryHeapBase(maxJpegSize);
    sp<MemoryHeapBase> rawThumbnailHeap = new MemoryHeapBase(rawThumbSize);

    uint32_t jpegSize = maxJpegSize;
    /* mRawHeap->getSize(); */
    err = extractJpegAndRawThumb((uint32_t)jpegOffset, (uint32_t *)&rawThumbSize, (uint32_t)rawThumbOffset,
        mRawHeap->data, &jpegSize, jpegHeap->base(), rawThumbnailHeap->base());
    if (err == false) {
        ALOGE("getJpeg: error, could not extract Jpeg and YUV");
        return UNKNOWN_ERROR;
    }

    struct jpeg_encode_param encodeThumbParam;
    CLEAR(encodeThumbParam);
    if (!jpegOpenEncoding(encodeThumbParam)) {
        ALOGE("internalGetJpegForSocJpeg: error, jpegInitEncoding");
        return UNKNOWN_ERROR;
    }

    encodeThumbParam.srcBuf = rawThumbnailHeap->base();
    encodeThumbParam.srcWidth = mThumbnailSize.width;
    encodeThumbParam.srcHeight = mThumbnailSize.height;
    encodeThumbParam.srcBufSize = rawThumbSize;
    encodeThumbParam.srcFormat = V4L2_PIX_FMT_UYVY;
    encodeThumbParam.destJpegQuality = 45;

    if (!jpegExecuteEncoding(encodeThumbParam)) {
        ALOGE("internalGetJpegForSocJpeg: error, jpegStartEncoding");
        return UNKNOWN_ERROR;
    }

    /* EXIF */
    LOG_PERFORMANCE_START(1);

    setExifChangedAttribute();

    sp<MemoryHeapBase> exifHeap = new MemoryHeapBase(EXIF_MAX_LEN);
    if (!initialized(exifHeap)) {
        ALOGE("getJpeg: error, could not initialize Camera exif heap");
        return UNKNOWN_ERROR;
    }

    Exif exif(mCameraId);
    uint32_t exifSize;
    if (mThumbnailSize.width == 0 || mThumbnailSize.height == 0)
        exifSize = exif.make(exifHeap->base(), &mExifInfo);
    else
        exifSize = exif.make(exifHeap->base(), &mExifInfo, exifHeap->getSize(),
                (uint8_t *)encodeThumbParam.destJpegBuf, encodeThumbParam.destJpegSize);
    if (CC_UNLIKELY(!exifSize)) {
        ALOGE("getJpeg: error, fail to make EXIF");
        return UNKNOWN_ERROR;
    }

    jpegCloseEncoding(encodeThumbParam);

    /* Jpeg */
    mPictureFrameSize = jpegSize + exifSize;
    /* picture frame size is should be calculated before call allocateSnapshotHeap */
    if (!allocateSnapshotHeap()) {
        ALOGE("getJpeg: error, allocateSnapshotHeap");
        return UNKNOWN_ERROR;
    }

    memcpy(mJpegHeap->data, jpegHeap->base(), 2);
    memcpy((uint8_t *)(mJpegHeap->data) + 2, exifHeap->base(), exifSize);
    memcpy((uint8_t *)(mJpegHeap->data) + 2 + exifSize, (uint8_t *)(jpegHeap->base()) + 2, jpegSize - 2);

    LOG_PERFORMANCE_END(1, "jpeg + exif");

    /* Postview */
#ifdef NOTDEFINED
    err = mFlite.gctrl(V4L2_CID_CAM_JPEG_POSTVIEW_OFFSET, postviewOffset);
    CHECK_ERR(err, ("getJpeg: error %d, postview offset", err));
#else
    *postviewOffset = 0;
#endif

    return 0;
}
#endif

#ifdef SAMSUNG_EXYNOS4210
int SecCameraHardware::internalGetJpegForSocYuv(int *postviewOffset)
{
    int jpegFd = api_jpeg_encode_init();
    CHECK_ERR_N(jpegFd, ("getEncodedJpeg: error %d, api_jpeg_encode_init", jpegFd));

    sp<MemoryHeapBase> rawThumbnail;

    int jpegSrcFrameSize;
    uint8_t *inBuf, *outBuf, *jpeg, *thumb;
    int jpegSize = 0;
    int thumbSize = 0;
    struct jpeg_enc_param jpegParam;

    Exif exif(mCameraId);
    uint32_t exifSize;

    bool thumbnail = false;

    /* Thumbnail */
    LOG_PERFORMANCE_START(1);

    if (mThumbnailSize.width == 0 || mThumbnailSize.height == 0)
        goto encode_jpeg;

    jpegSrcFrameSize = mThumbnailSize.width*mThumbnailSize.height * 2;
    rawThumbnail = new MemoryHeapBase(jpegSrcFrameSize);
    LOG_PERFORMANCE_START(3);
    scaleDownYuv422((uint8_t *)mRawHeap->data, (int)mPictureSize.width, (int)mPictureSize.height,
        (uint8_t *)rawThumbnail->base(), (int)mThumbnailSize.width, (int)mThumbnailSize.height);
    LOG_PERFORMANCE_END(3, "scaleDownYuv422");
    CLEAR(jpegParam);
    jpegParam.width = mThumbnailSize.width;
    jpegParam.height = mThumbnailSize.height;
    jpegParam.in_fmt = YUV_422;
    jpegParam.out_fmt = JPEG_422;
    jpegParam.quality = 42;
    api_jpeg_set_encode_param(&jpegParam);

    inBuf = (uint8_t *)api_jpeg_get_encode_in_buf(jpegFd, jpegSrcFrameSize);
    if (inBuf == NULL) {
        ALOGW("getEncodedJpeg: error, thumbnail api_jpeg_get_encode_in_buf");
        goto encode_jpeg;
    }
    memcpy(inBuf, rawThumbnail->base(), jpegSrcFrameSize);

    outBuf = (uint8_t *)api_jpeg_get_encode_out_buf(jpegFd);
    if (outBuf == NULL) {
        ALOGW("getEncodedJpeg: error, thumbnail api_jpeg_get_encode_out_buf");
        goto encode_jpeg;
    }

    if (api_jpeg_encode_exe(jpegFd, &jpegParam) != JPEG_ENCODE_OK) {
        ALOGE("getEncodedJpeg: error, thumbnail api_jpeg_get_encode_in_buf");
        goto encode_jpeg;
    }
    thumbSize = jpegParam.size;
    thumbnail = true;

    LOG_PERFORMANCE_END(1, "encode thumbnail");

encode_jpeg:
    /* EXIF */
    setExifChangedAttribute();

    sp<MemoryHeapBase> exifHeap = new MemoryHeapBase(EXIF_MAX_LEN);
    if (!initialized(exifHeap)) {
        ALOGE("getJpeg: error, could not initialize Camera exif heap");
        return UNKNOWN_ERROR;
    }

    if (!thumbnail)
        exifSize = exif.make(exifHeap->base(), &mExifInfo);
    else
        exifSize = exif.make(exifHeap->base(), &mExifInfo, exifHeap->getSize(), outBuf, thumbSize);
    if (CC_UNLIKELY(!exifSize)) {
        ALOGE("getJpeg: error, fail to make EXIF");
        return UNKNOWN_ERROR;
    }

    /* Jpeg */
    LOG_PERFORMANCE_START(2);

    CLEAR(jpegParam);
    jpegParam.width = mPictureSize.width;
    jpegParam.height = mPictureSize.height;
    jpegParam.in_fmt = YUV_422;
    jpegParam.out_fmt = JPEG_422;
    jpegParam.quality = 90;
    api_jpeg_set_encode_param(&jpegParam);

    jpegSrcFrameSize = mPictureSize.width * mPictureSize.height * 2;
    inBuf = (uint8_t *)api_jpeg_get_encode_in_buf(jpegFd, jpegSrcFrameSize);
    if (inBuf == NULL) {
        ALOGW("getEncodedJpeg: error, api_jpeg_get_encode_in_buf");
        goto out;
    }
    LOG_PERFORMANCE_START(5);
    memcpy(inBuf, mRawHeap->data, jpegSrcFrameSize);
    LOG_PERFORMANCE_END(5, "memcpy jpeg");
    outBuf = (uint8_t *)api_jpeg_get_encode_out_buf(jpegFd);
    if (outBuf == NULL) {
        ALOGW("getEncodedJpeg: error, api_jpeg_get_encode_out_buf");
        goto out;
    }

    if (api_jpeg_encode_exe(jpegFd, &jpegParam) != JPEG_ENCODE_OK) {
        ALOGE("getEncodedJpeg: error, api_jpeg_get_encode_in_buf");
        goto out;
    }
    jpegSize = jpegParam.size;

    mPictureFrameSize = jpegSize + exifSize;
    /* picture frame size is should be calculated before call allocateSnapshotHeap */
    if (!allocateSnapshotHeap())
        ALOGE("getJpeg: error, allocateSnapshotHeap");
        return UNKNOWN_ERROR;

    LOG_PERFORMANCE_END(2, "encode jpeg");

    LOG_PERFORMANCE_START(4);
    jpeg = outBuf;
    memcpy(mJpegHeap->data, jpeg, 2);
    memcpy((uint8_t *)mJpegHeap->data + 2, exifHeap->base(), exifSize);
    memcpy((uint8_t *)mJpegHeap->data + 2 + exifSize, jpeg + 2, jpegSize - 2);
    LOG_PERFORMANCE_END(4, "jpeg + exif");

    /* Postview */
    *postviewOffset = 0;

    api_jpeg_encode_deinit(jpegFd);
    return 0;

out:
    api_jpeg_encode_deinit(jpegFd);
    return -1;
}
#else /* !SAMSUNG_EXYNOS4210 */

int SecCameraHardware::internalGetJpegForSocYuvWithZoom(int *postviewOffset)
{
    ExynosBuffer thumbnailJpeg;
    ExynosBuffer rawThumbnail;
    ExynosBuffer jpegOutBuf;
    ExynosBuffer exifOutBuf;
    ExynosBuffer nullBuf;
    Exif exif(mCameraId);
    int i;

    uint8_t *thumb;
    int32_t thumbSize;
    bool thumbnail = false;
    int err = -1;
    int ret = UNKNOWN_ERROR;

    s5p_rect zoomRect;
    ALOGV("DEBUG(%s)(%d): picture size(%d/%d)", __FUNCTION__, __LINE__, mPictureSize.width, mPictureSize.height);
    ALOGV("DEBUG(%s)(%d): thumbnail size(%d/%d)", __FUNCTION__, __LINE__, mThumbnailSize.width, mThumbnailSize.height);

    zoomRect.w = mPictureSize.width * 10 / mZoomValue;
    zoomRect.h = mPictureSize.height * 10 / mZoomValue;

    if (zoomRect.w % 2)
        zoomRect.w -= 1;

    if (zoomRect.h % 2)
        zoomRect.h -= 1;

    zoomRect.x = (mPictureSize.width - zoomRect.w) / 2;
    zoomRect.y = (mPictureSize.height - zoomRect.h) / 2;

    if (zoomRect.x % 2)
        zoomRect.x -= 1;

    if (zoomRect.y % 2)
        zoomRect.y -= 1;

    /* Making thumbnail image */
    if (mThumbnailSize.width == 0 || mThumbnailSize.height == 0)
        goto encodeJpeg;

    /* alloc rawThumbnail */
    rawThumbnail.size.extS[0] = mThumbnailSize.width * mThumbnailSize.height * 2;
    if (allocMem(mIonCameraClient, &rawThumbnail, 1 << 1) == false) {
        ALOGE("ERR(%s): rawThumbnail allocMem() fail", __func__);
        goto destroyMem;
    } else {
        ALOGV("DEBUG(%s): rawThumbnail allocMem adr(%p), size(%d), ion(%d)", __FUNCTION__,
            rawThumbnail.virt.extP[0], rawThumbnail.size.extS[0], mIonCameraClient);
        memset(rawThumbnail.virt.extP[0], 0, rawThumbnail.size.extS[0]);
    }

    if (mPictureBuf.size.extS[0] <= 0)
        return UNKNOWN_ERROR;

    LOG_PERFORMANCE_START(3);

    scaleDownYuv422((unsigned char *)mPictureBuf.virt.extP[0],
                    (int)mPictureSize.width,
                    (int)mPictureSize.height,
                    (unsigned char *)rawThumbnail.virt.extP[0],
                    (int)mThumbnailSize.width,
                    (int)mThumbnailSize.height);
    LOG_PERFORMANCE_END(3, "scaleDownYuv422");

    /* alloc thumbnailJpeg */
    thumbnailJpeg.size.extS[0] = mThumbnailSize.width * mThumbnailSize.height * 2;
    if (allocMem(mIonCameraClient, &thumbnailJpeg, 1 << 1) == false) {
        ALOGE("ERR(%s): thumbnailJpeg allocMem() fail", __func__);
        goto destroyMem;
    } else {
        ALOGV("DEBUG(%s): thumbnailJpeg allocMem adr(%p), size(%d), ion(%d)", __FUNCTION__,
            thumbnailJpeg.virt.extP[0], thumbnailJpeg.size.extS[0], mIonCameraClient);
        memset(thumbnailJpeg.virt.extP[0], 0, thumbnailJpeg.size.extS[0]);
    }

    err = EncodeToJpeg(&rawThumbnail,
                       &thumbnailJpeg,
                        mThumbnailSize.width,
                        mThumbnailSize.height,
                        CAM_PIXEL_FORMAT_YUV422I,
                        &thumbSize,
                        30);
    CHECK_ERR_GOTO(encodeJpeg, err, ("getJpeg: error, EncodeToJpeg(thumbnail)"));

    thumb = (uint8_t *)thumbnailJpeg.virt.extP[0];
    thumbnail = true;

    LOG_PERFORMANCE_END(1, "encode thumbnail");

encodeJpeg:
    /* Making EXIF header */

    setExifChangedAttribute();

    uint32_t exifSize;
    int32_t jpegSize;
    uint8_t *jpeg;

    /* alloc exifOutBuf */
    exifOutBuf.size.extS[0] = EXIF_MAX_LEN;
    if (allocMem(mIonCameraClient, &exifOutBuf, 1 << 1) == false) {
        ALOGE("ERR(%s): exifTmpBuf allocMem() fail", __func__);
        goto destroyMem;
    } else {
        ALOGV("DEBUG(%s): exifTmpBuf allocMem adr(%p), size(%d), ion(%d)", __FUNCTION__,
            exifOutBuf.virt.extP[0], exifOutBuf.size.extS[0], mIonCameraClient);
        memset(exifOutBuf.virt.extP[0], 0, exifOutBuf.size.extS[0]);
    }

    if (!thumbnail)
        exifSize = exif.make((void *)exifOutBuf.virt.extP[0], &mExifInfo);
    else
        exifSize = exif.make((void *)exifOutBuf.virt.extP[0], &mExifInfo, exifOutBuf.size.extS[0], thumb, thumbSize);
    if (CC_UNLIKELY(!exifSize)) {
        ALOGE("ERR(%s): getJpeg: error, fail to make EXIF", __FUNCTION__);
        goto destroyMem;
    }

    /* alloc jpegOutBuf */
    jpegOutBuf.size.extS[0] = mPictureSize.width * mPictureSize.height * 2;
    if (allocMem(mIonCameraClient, &jpegOutBuf, 1 << 1) == false) {
        ALOGE("ERR(%s): jpegOutBuf allocMem() fail", __func__);
        goto destroyMem;
    } else {
        ALOGV("DEBUG(%s): jpegOutBuf allocMem adr(%p), size(%d), ion(%d)", __FUNCTION__,
            jpegOutBuf.virt.extP[0], jpegOutBuf.size.extS[0], mIonCameraClient);
        memset(jpegOutBuf.virt.extP[0], 0, jpegOutBuf.size.extS[0]);
    }

    /* Making main jpeg image */
    err = EncodeToJpeg(&mPictureBuf,
                       &jpegOutBuf,
                        mPictureSize.width,
                        mPictureSize.height,
                        CAM_PIXEL_FORMAT_YUV422I,
                        &jpegSize,
                        mJpegQuality);

    CHECK_ERR_GOTO(destroyMem, err, ("getJpeg: error, EncodeToJpeg(Main)"));

    /* Finally, Creating Jpeg image file including EXIF */
    mPictureFrameSize = jpegSize + exifSize;

    /*note that picture frame size is should be calculated before call allocateSnapshotHeap */
    if (!allocateSnapshotHeap()) {
        ALOGE("getEncodedJpeg: error, allocateSnapshotHeap");
        goto destroyMem;
    }

    jpeg = (unsigned char *)jpegOutBuf.virt.extP[0];
    memcpy((unsigned char *)mJpegHeap->data, jpeg, 2);
    memcpy((unsigned char *)mJpegHeap->data + 2, exifOutBuf.virt.extP[0], exifSize);
    memcpy((unsigned char *)mJpegHeap->data + 2 + exifSize, jpeg + 2, jpegSize - 2);
    ALOGD("DEBUG(%s): success making jpeg(%d) and exif(%d)", __FUNCTION__, jpegSize, exifSize);

    ret = NO_ERROR;

destroyMem:
    freeMemSinglePlane(&thumbnailJpeg, 0);
    freeMemSinglePlane(&rawThumbnail, 0);
    freeMemSinglePlane(&jpegOutBuf, 0);
    freeMemSinglePlane(&exifOutBuf, 0);

    return ret;
}

int SecCameraHardware::internalGetJpegForSocYuv(int *postviewOffset)
{
    ALOGD("internalGetJpegForSocYuv: E");
    sp<MemoryHeapBase> rawThumbnail;
    sp<MemoryHeapBase> thumbnailJpeg;
    uint8_t *thumb;
    int32_t thumbSize = 0;
    bool thumbnail = false;
    int err;

    LOG_PERFORMANCE_START(1);

    /* Making thumbnail jpeg image */

    if (mThumbnailSize.width == 0 || mThumbnailSize.height == 0)
        goto encodeJpeg;

    rawThumbnail = new MemoryHeapBase(mThumbnailSize.width * mThumbnailSize.height * 2);
    LOG_PERFORMANCE_START(3);
    scaleDownYuv422((uint8_t *)mRawHeap->data, (int)mPictureSize.width, (int)mPictureSize.height,
        (uint8_t *)rawThumbnail->base(), (int)mThumbnailSize.width, (int)mThumbnailSize.height);
    LOG_PERFORMANCE_END(3, "scaleDownYuv422");

    thumbnailJpeg = new MemoryHeapBase(mThumbnailSize.width * mThumbnailSize.height * 2);

#ifdef CHG_ENCODE_JPEG
    err = EncodeToJpeg((unsigned char*)rawThumbnail->base(),
                        mThumbnailSize.width,
                        mThumbnailSize.height,
                        CAM_PIXEL_FORMAT_YUV422I,
                        (unsigned char*)thumbnailJpeg->base(),
                        &thumbSize,
                        30); /* DSLIM fix lelvel*/
    if (CC_UNLIKELY(err)) {
        ALOGE("getJpeg: error, EncodeToJpeg(thumbnail)");
        goto encodeJpeg;
    }
#endif

    thumb = (uint8_t *)thumbnailJpeg->base();
    thumbnail = true;

    LOG_PERFORMANCE_END(1, "encode thumbnail");

encodeJpeg:
    /* Making EXIF header */

    setExifChangedAttribute();

    Exif exif(mCameraId);
    uint32_t exifSize;
    int32_t jpegSize = 0;
    uint8_t *jpeg;

    sp<MemoryHeapBase> jpegHeap = new MemoryHeapBase(mPictureSize.width * mPictureSize.height * 2);
    sp<MemoryHeapBase> exifHeap = new MemoryHeapBase(EXIF_MAX_LEN);
    if (!initialized(exifHeap) || !initialized(jpegHeap)) {
        ALOGE("getJpeg: error, could not initialize jpeg, exif heap");
        return UNKNOWN_ERROR;
    }

    if (!thumbnail)
        exifSize = exif.make(exifHeap->base(), &mExifInfo);
    else
        exifSize = exif.make(exifHeap->base(), &mExifInfo, exifHeap->getSize(), thumb, thumbSize);
    if (CC_UNLIKELY(!exifSize)) {
        ALOGE("getJpeg: error, fail to make EXIF");
        return UNKNOWN_ERROR;
    }

    /* Making main jpeg image */
#ifdef CHG_ENCODE_JPEG
    err = EncodeToJpeg((unsigned char *)mRawHeap->data,
                        mPictureSize.width,
                        mPictureSize.height,
                        CAM_PIXEL_FORMAT_YUV422I,
                        (unsigned char *)jpegHeap->base(),
                        &jpegSize,
                        mJpegQuality);
    CHECK_ERR_N(err, ("getJpeg: error, EncodeToJpeg(Main)"));
#endif
    /* Finally, Creating Jpeg image file including EXIF */
    mPictureFrameSize = jpegSize + exifSize;

    /* note that picture frame size is should be calculated before call allocateSnapshotHeap */
    if (!allocateSnapshotHeap()) {
        ALOGE("getJpeg: error, allocateSnapshotHeap");
        return UNKNOWN_ERROR;
    }

    jpeg = (uint8_t *)jpegHeap->base();
    memcpy(mJpegHeap->data, jpeg, 2);
    memcpy((uint8_t *)mJpegHeap->data + 2, exifHeap->base(), exifSize);
    memcpy((uint8_t *)mJpegHeap->data + 2 + exifSize, jpeg + 2, jpegSize - 2);

    ALOGD("internalGetJpegForSocYuv: X");
    return NO_ERROR;
}

int SecCameraHardware::EncodeToJpeg(ExynosBuffer *yuvBuf, ExynosBuffer *jpegBuf,
    int width, int height, cam_pixel_format srcformat, int* jpegSize, int quality)
{
    ExynosJpegEncoder *jpegEnc = NULL;
    int ret;

    /* 1. ExynosJpegEncoder create */
    jpegEnc = new ExynosJpegEncoder();

    if (jpegEnc == NULL) {
        ALOGE("ERR(%s) (%d):  jpegEnc is null", __FUNCTION__, __LINE__);
        return UNKNOWN_ERROR;
    }

    ret = jpegEnc->create();

    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.create(%d) fail", __func__, ret);
        goto jpeg_encode_done;
    }

    /* 2. cache on */
    ret = jpegEnc->setCache(JPEG_CACHE_ON);
    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.setCache(%d) fail", __func__, ret);
        goto jpeg_encode_done;
    }

    /* 3. set quality */
    gprintf("=====================> quality = %d\n", quality); // ghcstop_caution.....This value must be 0 ~ 99
    ret = jpegEnc->setQuality(quality);
    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.setQuality(%d) fail", __func__, ret);
        goto jpeg_encode_done;
    }

    ret = jpegEnc->setSize(width, height);
    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.setSize() fail", __func__);
        goto jpeg_encode_done;
    }

    /* 4. set yuv format */
    ret = jpegEnc->setColorFormat(srcformat);
    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.setColorFormat(%d) fail", __func__, ret);
        goto jpeg_encode_done;
    }

    /* 5. set jpeg format */
    ret = jpegEnc->setJpegFormat(V4L2_PIX_FMT_JPEG_422);
    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.setJpegFormat(%d) fail", __func__, ret);
        goto jpeg_encode_done;
    }

    /* src and dst buffer alloc */
    /* 6. set yuv format(src) from FLITE */
    ret = jpegEnc->setInBuf(((char **)&(yuvBuf->virt.extP[0])), (int *)yuvBuf->size.extS);
    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.setInBuf(%d) fail", __func__, ret);
        goto jpeg_encode_done;
    }

    /* 6. set jpeg format(dest) from FLITE */
    ret = jpegEnc->setOutBuf((char *)jpegBuf->virt.extP[0], jpegBuf->size.extS[0]);
    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.setOutBuf(%d) fail", __func__, ret);
        goto jpeg_encode_done;
    }

    ret = jpegEnc->updateConfig();
    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.updateConfig(%d) fail", __func__, ret);
        goto jpeg_encode_done;
    }

    /* start encoding */
    ret = jpegEnc->encode();
    if (ret < 0) {
        ALOGE("ERR(%s):jpegEnc.encode(%d) fail", __func__, ret);
        goto jpeg_encode_done;
    }

    /* start encoding */
    *jpegSize = jpegEnc->getJpegSize();
    if ((*jpegSize) <= 0) {
        ALOGE("ERR(%s):jpegEnc.getJpegSize(%d) is too small", __func__, *jpegSize);
        goto jpeg_encode_done;
    }
    ALOGD("DEBUG(%s): jpegEnc success!! size(%d)", __func__, *jpegSize);
    ret = true;

jpeg_encode_done:
    jpegEnc->destroy();
    delete jpegEnc;

    if (ret == false) {
        ALOGE("ERR(%s): (%d) [yuvBuf->fd.extFd[0] %d][yuvSize[0] %d]", __FUNCTION__, __LINE__,
            yuvBuf->fd.extFd[0], yuvBuf->size.extS[0]);
        ALOGE("ERR(%s): (%d) [jpegBuf->fd.extFd %d][jpegBuf->size.extS %d]", __FUNCTION__, __LINE__,
            jpegBuf->fd.extFd[0], jpegBuf->size.extS[0]);
        ALOGE("ERR(%s): (%d) [w %d][h %d][colorFormat %d]", __FUNCTION__, __LINE__,
            width, height, srcformat);
        return UNKNOWN_ERROR;
    }
    return NO_ERROR;
}

#endif /* SAMSUNG_EXYNOS4210 */

#ifdef SAMSUNG_EXYNOS4210
bool SecCameraHardware::internalEncodingJpeg(struct jpeg_encode_param *encodeParam)
{
    struct jpeg_enc_param   jpegParam;
    jpeg_stream_format  outputFormat = JPEG_422;
    jpeg_frame_format   inputFormat = YUV_422;
    uint8_t *inBuf = NULL;
    uint8_t *outBuf = NULL;

    if (!(encodeParam->srcWidth && encodeParam->srcHeight && encodeParam->srcBuf  && encodeParam->srcBufSize)) {
        ALOGE("encodingToJpeg: error, srcWidth=%d, srcHeight=%d, srcBuf=0x%X, srcBufSize=0x%X",
                        encodeParam->srcWidth, encodeParam->srcHeight,
                        (uint32_t)(encodeParam->srcBuf), encodeParam->srcBufSize);
        return false;
    }

    int jpegFd = api_jpeg_encode_init();
    CHECK_ERR(jpegFd, ("encodingToJpeg: error %d, api_jpeg_encode_init", jpegFd));

    /* Fix below code */
    switch (encodeParam->srcFormat) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV12T:
    case V4L2_PIX_FMT_YUV420:
        outputFormat = JPEG_420;
        ALOGW("encodingToJpeg, warning, srcFormat: %c%c%c%c", (uint8_t)(encodeParam->srcFormat),
            (uint8_t)(encodeParam->srcFormat >> 8), (uint8_t)(encodeParam->srcFormat >> 16),
            (uint8_t)(encodeParam->srcFormat >> 24));
        break;
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_YUV422P:
        inputFormat = YUV_422;
        outputFormat = JPEG_422;
        break;
    }

    /* set encode parameters */
    CLEAR(jpegParam);
    jpegParam.width = encodeParam->srcWidth;
    jpegParam.height = encodeParam->srcHeight;
    jpegParam.in_fmt = inputFormat; /* YCBYCR Only */
    jpegParam.out_fmt = outputFormat;
    jpegParam.quality = encodeParam->destJpegQuality;
    api_jpeg_set_encode_param(&jpegParam);

    /* get Input buffer address */
    inBuf = (uint8_t *)api_jpeg_get_encode_in_buf(jpegFd, encodeParam->srcBufSize);
    if (inBuf == NULL) {
        ALOGE("encodingToJpeg: error, api_jpeg_get_encode_in_buf");
        goto out;
    }

    ALOGV("Step 2: memcpy(inBuf(%p), srcBuf(%p), srcBufSize(%d)",
                        inBuf, encodeParam->srcBuf, encodeParam->srcBufSize);
    memcpy(inBuf, encodeParam->srcBuf, encodeParam->srcBufSize);

    /* get output buffer address */
    ALOGV("Step 3: get outBuf (%p)\n", outBuf);

    outBuf = (uint8_t *)api_jpeg_get_encode_out_buf(jpegFd);
    if (outBuf == NULL) {
        ALOGE("encodingToJpeg: error, thumbnail api_jpeg_get_encode_out_buf");
        goto out;
    }

    ALOGV("Step 4: excute jpeg encode\n");

    if (api_jpeg_encode_exe(jpegFd, &jpegParam) != JPEG_ENCODE_OK) {
        ALOGE("encodingToJpeg: error, thumbnail api_jpeg_get_encode_in_buf");
        goto out;
    }

    ALOGV("Step 6: Done");

    encodeParam->destJpegSize = jpegParam.size;
    memcpy(encodeParam->destJpegBuf, outBuf, jpegParam.size);

    api_jpeg_encode_deinit(jpegFd);
    return true;

out:
    api_jpeg_encode_deinit(jpegFd);
    return false;
}

inline bool SecCameraHardware::jpegOpenEncoding(struct jpeg_encode_param &encodeParam)
{
    encodeParam.jpegFd = api_jpeg_encode_init();
    CHECK_ERR(encodeParam.jpegFd, ("jpegInitEncoding: error %d, api_jpeg_encode_init", encodeParam.jpegFd));

    return true;
}

bool SecCameraHardware::jpegExecuteEncoding(struct jpeg_encode_param &encodeParam)
{
    int         jpegFd = encodeParam.jpegFd;
    uint8_t     *inBuf = NULL;
    uint8_t     *outBuf = NULL;
    jpeg_stream_format      outputFormat = JPEG_422;
    jpeg_frame_format       inputFormat = YUV_422;
    struct jpeg_enc_param   jpegParam;

    if (!(encodeParam.srcWidth && encodeParam.srcHeight && encodeParam.srcBuf  && encodeParam.srcBufSize)) {
        ALOGE("jpegStartEncoding: error, srcWidth=%d, srcHeight=%d, srcBuf=0x%X, srcBufSize=0x%X",
                        encodeParam.srcWidth, encodeParam.srcHeight,
                        (uint32_t)(encodeParam.srcBuf), encodeParam.srcBufSize);
        goto out;
    }

    /* Fix below code */
    switch (encodeParam.srcFormat) {
    case V4L2_PIX_FMT_NV12:
    case V4L2_PIX_FMT_NV21:
    case V4L2_PIX_FMT_NV12T:
    case V4L2_PIX_FMT_YUV420:
        outputFormat = JPEG_420;
        ALOGW("jpegStartEncoding, warning, srcFormat: %c%c%c%c", (uint8_t)(encodeParam.srcFormat),
            (uint8_t)(encodeParam.srcFormat >> 8), (uint8_t)(encodeParam.srcFormat >> 16),
            (uint8_t)(encodeParam.srcFormat >> 24));
        break;
    case V4L2_PIX_FMT_YUYV:
    case V4L2_PIX_FMT_UYVY:
    case V4L2_PIX_FMT_YUV422P:
        inputFormat = YUV_422;
        outputFormat = JPEG_422;
        break;
    }

    /* set encode parameters */
    CLEAR(jpegParam);
    jpegParam.width = encodeParam.srcWidth;
    jpegParam.height = encodeParam.srcHeight;
    jpegParam.in_fmt = inputFormat; /* YCBYCR Only */
    jpegParam.out_fmt = outputFormat;
    jpegParam.quality = encodeParam.destJpegQuality;
    api_jpeg_set_encode_param(&jpegParam);

    /* get Input buffer address */
    inBuf = (uint8_t *)api_jpeg_get_encode_in_buf(jpegFd, encodeParam.srcBufSize);
    if (inBuf == NULL) {
        ALOGE("jpegStartEncoding: error, api_jpeg_get_encode_in_buf");
        goto out;
    }

    ALOGV("Step 2: memcpy(inBuf(%p), srcBuf(%p), srcBufSize(%d)",
                        inBuf, encodeParam.srcBuf, encodeParam.srcBufSize);
    memcpy(inBuf, encodeParam.srcBuf, encodeParam.srcBufSize);

    /* get output buffer address */
    outBuf = (uint8_t *)api_jpeg_get_encode_out_buf(jpegFd);
    if (outBuf == NULL) {
        ALOGE("jpegStartEncoding: error, thumbnail api_jpeg_get_encode_out_buf");
        goto out;
    }
    ALOGV("Step 3: get outBuf (%p)\n", outBuf);

    ALOGV("Step 4: excute jpeg encode\n");

    if (api_jpeg_encode_exe(jpegFd, &jpegParam) != JPEG_ENCODE_OK) {
        ALOGE("jpegStartEncoding: error, thumbnail api_jpeg_get_encode_in_buf");
        goto out;
    }

    ALOGV("Step 6: Done");

    encodeParam.destJpegSize = jpegParam.size;
    encodeParam.destJpegBuf = outBuf;

    return true;

out:
    encodeParam.destJpegSize = 0;
    encodeParam.destJpegBuf = NULL;
    encodeParam.jpegFd = -1;
    api_jpeg_encode_deinit(jpegFd);
    return false;
}

inline bool SecCameraHardware::jpegCloseEncoding(struct jpeg_encode_param &encodeParam)
{
    if (encodeParam.jpegFd >= 0) {
        api_jpeg_encode_deinit(encodeParam.jpegFd);
        encodeParam.jpegFd = -1;
    }

    return true;
}
#else /* !SAMSUNG_EXYNOS4210 */
bool SecCameraHardware::internalEncodingJpeg(struct jpeg_encode_param *encodeParam)
{
    return true;
}

inline bool SecCameraHardware::jpegOpenEncoding(struct jpeg_encode_param &encodeParam)
{
    return true;
}

bool SecCameraHardware::jpegExecuteEncoding(struct jpeg_encode_param &encodeParam)
{
    return true;
}

inline bool SecCameraHardware::jpegCloseEncoding(struct jpeg_encode_param &encodeParam)
{
    return true;
}
#endif /* SAMSUNG_EXYNOS4210 */

#if !defined(REAR_USE_YUV_CAPTURE)
/*
 * rawThumbOffset - not used.
 */
/* #define J_DEBUG */
bool SecCameraHardware::extractJpegAndRawThumb(uint32_t jpegOffset, uint32_t *rawThumbSize,
                            uint32_t rawThumbOffset, void *srcInterleavedDataBuf,
                            uint32_t *jpegSize, void *dstJpegBuf, void *dstRawThumbBuf)
{
    const uint8_t *src = (const uint8_t *)srcInterleavedDataBuf;
    uint8_t *dstJpeg = (uint8_t *)dstJpegBuf;
    uint8_t *dstRawThumb = (uint8_t *)dstRawThumbBuf;
    uint32_t dstJpegSize = 0;
    uint32_t dstRawThumbSize = 0;
    uint32_t dstThumbHeight = 0;
    const uint32_t srcBufSize = *jpegSize;
    uint8_t * const endOfSrcBuf = (uint8_t *)srcInterleavedDataBuf + srcBufSize;
    uint8_t * const endOfDstThumbBuf = (uint8_t *)dstRawThumbBuf + *rawThumbSize;
    jpegHeader_t *jpegHeader = (jpegHeader_t *)srcInterleavedDataBuf;
    const uint32_t jpegHeaderSize = sizeof(jpegHeader_t);
    uint32_t yuvDataLength = __swap16gen(jpegHeader->resInfo.thumbWidth) * 2;

    const uint16_t JPEG_SOI = 0xD8FF; /* FF D8 */
    const uint16_t JPEG_EOI = 0xD9FF; /* FF D9 */
    const uint16_t YUV_SOI = 0xBEFF; /* FF BE */
    const uint16_t YUV_EOI = 0xBFFF; /* FF BF */
    const uint8_t a_JPEG_SOI[] = {0xFF, 0xD8};
    const uint8_t a_JPEG_EOI[] = {0xFF, 0xD9};
    const uint8_t a_YUV_SOI[] = {0xFF, 0xBE};
    const uint8_t a_YUV_EOI[] = {0xFF, 0xBF};
    bool isJpegEoi = false;
    bool isYuvEoi = false;
#ifdef J_DEBUG
    bool isFristJpegData = true;
    bool isFoundJpegEOI = false;
    uint8_t *src2 = endOfSrcBuf - sizeof(a_JPEG_EOI);
    jpegInfo_t *jpegInform = NULL;
#endif
/*
    ALOGV("%s: SrcBuf addr=0x%X, size=0x%X, DstJpegBuf addr=0x%X, size=0x%X, DstThumbBuf addr=0x%X, size=0x%X",
            __FUNCTION__, srcInterleavedDataBuf, mRawHeap->getSize(), dstJpegBuf, *jpegSize,
            dstRawThumbBuf, *rawThumbSize);
*/

    if (jpegHeader->soi != JPEG_SOI) {
        ALOGE("ERROR(%s): Not Jpeg file", __FUNCTION__);
        goto out;
    }

    if ( (mPictureSize.width != __swap16gen(jpegHeader->resInfo.jpegWidth)) ||
            (mPictureSize.height != __swap16gen(jpegHeader->resInfo.jpegHeight)) ||
            (mThumbnailSize.width != __swap16gen(jpegHeader->resInfo.thumbWidth)) ||
            (mThumbnailSize.height != __swap16gen(jpegHeader->resInfo.thumbHeight)) ) {
        ALOGE("extractJpegAndRawThumb: error, Invalid Jpeg width=0x%04X height=0x%04X, Thumnail width=0x%04X, height=0x%04X",
                __swap16gen(jpegHeader->resInfo.jpegWidth), __swap16gen(jpegHeader->resInfo.jpegHeight),
                __swap16gen(jpegHeader->resInfo.thumbWidth), __swap16gen(jpegHeader->resInfo.thumbHeight));
        goto out;
    }

#ifdef J_DEBUG
    while (src2 >= srcInterleavedDataBuf) {
        if ((*src2 == 0xFF) && (*(src2+1) == a_JPEG_EOI[1])) {
            /* ALOGV("%s: found Jpeg EOI(0x%X), addr=0x%X, ", __FUNCTION__, *(uint16_t *)src2, src2); */
            isFoundJpegEOI = true;
            break;
        } else if ((*src2 == a_JPEG_EOI[1]) && (*(src2-1) == 0xFF) ) {
            isFoundJpegEOI = true;
            src2 -= 1;
            break;
        }

        src2 -= sizeof(a_JPEG_EOI);
    }
#endif

#ifdef J_DEBUG
    if (jpegHeaderSize != 0x27C) {
        ALOGE("ERROR(%s): Jpeg Header size = 0x%x", __FUNCTION__, jpegHeaderSize );
        return false;
    }
#endif

    /* Copy Jpeg Header */
    memcpy(dstJpeg, src, jpegHeaderSize);
    dstJpeg += jpegHeaderSize;
    dstJpegSize += jpegHeaderSize;
    src += jpegHeaderSize;

    /* Check First YUV SOI */
    if (CC_UNLIKELY(*(uint16_t *)src != YUV_SOI)) {
        ALOGE("ERROR(%s, %d): Jpeg file is corrupted", __FUNCTION__, __LINE__);
        goto out;
    } else {
        src += sizeof(YUV_SOI);
    }

    while (src < endOfSrcBuf - 1) {
        /* Copy YUV Data */
        if (CC_UNLIKELY(src + yuvDataLength + sizeof(YUV_EOI) >= endOfSrcBuf)) {
            ALOGE("ERROR(%s, %d): Jpeg file is corrupted(0x0A)", __FUNCTION__, __LINE__);
            goto out;
        }
#ifdef J_DEBUG
        /* ALOGV("2. Copy YUV Segment"); */
        if (CC_UNLIKELY(dstRawThumb+yuvDataLength > endOfDstThumbBuf)) {
            ALOGE("ERROR: dstRawThumb+yuvDataLength = 0x%X, endOfDstThumbBuf=0x%X",
                                        (uint32_t)(dstRawThumb+yuvDataLength), (uint32_t)endOfDstThumbBuf);
            ALOGE("yuvDataLength=0x%X, dstRawThumbSize=0x%X, dstThumbHeight=0x%X",
                                        yuvDataLength, dstRawThumbSize, dstThumbHeight);
            goto out;
        }
#endif
        memcpy(dstRawThumb, src, yuvDataLength);
        dstRawThumb += yuvDataLength;
        dstRawThumbSize += yuvDataLength;
        src += yuvDataLength;
        dstThumbHeight++;

        /* Check YUV EOI */
        if (CC_UNLIKELY(*(uint16_t *)src != YUV_EOI)) {
            ALOGE("ERROR(%s, %d): Jpeg file is corrupted(0x0B)", __FUNCTION__, __LINE__);
            ALOGE("YUV_EOI = 0x%02X %02X, yuvDataLength=0x%X", *src, *(src+1), yuvDataLength);
            goto out;
        }
        src += sizeof(YUV_EOI);

#ifdef J_DEBUG
        /* ALOGV("3. Succes to check YUV EOI"); */
        if (isFristJpegData) {
            if ((src - (uint8_t *)srcInterleavedDataBuf) != jpegOffset)
                ALOGE("ERROR(%s)First Jpeg offset(0x%X) is innormal(0x%X)", __FUNCTION__, jpegOffset, src - (uint8_t *)srcInterleavedDataBuf);
            isFristJpegData = false;
        }
        /* ALOGV("4. Copy JPEG Segment"); */
#endif

#ifdef NOTDEFINED
        /*
         * Copy Jpeg Data.
         * We need to improve data copy speed(from 1byte to 4byte) */
        while((*(uint16_t *)src != YUV_SOI) && (*(uint16_t *)src != JPEG_EOI)) {
            if (CC_LIKELY(src < endOfSrcBuf)) {
                *dstJpeg++ = *src++;
                dstJpegSize++;
            } else {
                ALOGE("ERROR(%s, %d): Jpeg file is corrupted(0x0C)", __FUNCTION__, __LINE__);
                return false;
            }
        }

        /* Check whether YUV Start or Jpeg end */
        if (*(uint16_t *)src == JPEG_EOI) {
            *(uint16_t *)dstJpeg = *(uint16_t *)src;
            ALOGV("Coping Jpeg data is completed!(EOI=0x%X)", *(uint16_t *)dstJpeg);
            dstJpegSize += sizeof(JPEG_EOI);
            break;
        } else
            src += sizeof(YUV_SOI);
#else
        /*
         * Copy Jpeg Data.
         * We need to improve data copy speed(from 1byte to 4byte) */
        while(src < endOfSrcBuf - 1) {
            if ((*src == 0xFF) && (*(src+1) == a_YUV_SOI[1])) {
                src += sizeof(a_YUV_SOI);
                isYuvEoi = true;
                break;
            } else if ((*src == 0xFF) && (*(src+1) == a_JPEG_EOI[1])) {
                /* *(uint16_t *)dstJpeg = *(uint16_t *)src; */
                *dstJpeg++ = *src++;
                *dstJpeg++ = *src++;
                dstJpegSize += sizeof(a_JPEG_EOI);
                isJpegEoi = true;
                break;
            } else if (*(src+1) == 0xFF) {
                *dstJpeg++ = *src++;
                dstJpegSize++;
                continue;
            }

            *dstJpeg++ = *src++;
            *dstJpeg++ = *src++;
            dstJpegSize += 2;
        }

        /* Check whether YUV Start or Jpeg end */
        if (isJpegEoi) {
            ALOGV("Coping Jpeg data is completed!!!");
            break;
        } else if (!isYuvEoi) {
#ifdef J_DEBUG
            ALOGE("ERROR(%s, %d): Jpeg file is corrupted(0x0C), isFoundJpegEOI=%d",
                                __FUNCTION__, __LINE__, isFoundJpegEOI);
#endif
            goto out;
        }

        isYuvEoi = false;
#endif
    } /* while (src < endOfSrcBuf) */

#ifdef J_DEBUG
    if (!(isFoundJpegEOI && isJpegEoi)) {
        /* You should not be here */
        ALOGE("ERROR(%s): isFoundJpegEOI=%d, isJpegEoi=%d", __FUNCTION__, isFoundJpegEOI, isJpegEoi);
        goto out;
    }
#endif

    ALOGV("JpegSize=%d, rawThumbSize=%d", *jpegSize, *rawThumbSize);
    ALOGV("dstJpegSize=%d, dstRawThumbSize=%d, dstThumbHeight = %d", dstJpegSize, dstRawThumbSize, dstThumbHeight);

    if (CC_UNLIKELY(*rawThumbSize != dstRawThumbSize)) {
        ALOGE("ERROR(%s): Raw thumb size= %d", __FUNCTION__, *rawThumbSize);
        goto out;
    }

    *jpegSize = dstJpegSize;
    *rawThumbSize = dstRawThumbSize;

    ALOGI("Extracting Jpeg,YUV data is completed!");

    return true;

out:
    ALOGE("extractJpegAndRawThumb: error, dstJpegSize=%d, dstRawThumbSize=%d, dstThumbHeight = %d",
                                            dstJpegSize, dstRawThumbSize, dstThumbHeight);
    if (dstJpegSize > jpegHeaderSize) {
        *dstJpeg++ = a_JPEG_EOI[0];
        *dstJpeg = a_JPEG_EOI[1];
        dstJpegSize += sizeof(a_JPEG_EOI);
    }

    return false;
}
#endif

bool SecCameraHardware::scaleDownYuv422(uint8_t *srcBuf, int srcW, int srcH,
                                        uint8_t *dstBuf, int dstW, int dstH)

{
    int step_x, step_y;
    int src_y_start_pos, dst_pos, src_pos;
    char *src_buf = (char *)srcBuf;
    char *dst_buf = (char *)dstBuf;

    if (dstW & 0x01 || dstH & 0x01) {
        ALOGE("ERROR(%s): (%d) width or height invalid(%d/%d)", __FUNCTION__, __LINE__,
                dstW & 0x01, dstH & 0x01);
        return false;
    }

    step_x = srcW / dstW;
    step_y = srcH / dstH;

    unsigned int srcWStride = srcW * 2;
    unsigned int stepXStride = step_x * 2;

    dst_pos = 0;
    for (int y = 0; y < dstH; y++) {
        src_y_start_pos = srcWStride * step_y * y;
        for (int x = 0; x < dstW; x += 2) {
            src_pos = src_y_start_pos + (stepXStride * x);
            dst_buf[dst_pos++] = src_buf[src_pos];
            dst_buf[dst_pos++] = src_buf[src_pos + 1];
            dst_buf[dst_pos++] = src_buf[src_pos + 2];
            dst_buf[dst_pos++] = src_buf[src_pos + 3];
        }
    }

    return true;
}

#ifdef RECORDING_CAPTURE
bool SecCameraHardware::conversion420to422(uint8_t *srcBuf, uint8_t *dstBuf, int width, int height)
{
    int i, j, k;
    int vPos1, vPos2, uPos1, uPos2;

    /* copy y table */
    for (i = 0; i < width * height; i++) {
        j = i * 2;
        dstBuf[j] = srcBuf[i];
    }

    for (i = 0; i < width * height / 2; i += 2) {
        j = width * height;
        k = width * 2;
        uPos1 = (i / width) * k * 2 + (i % width) * 2 + 1;
        uPos2 = uPos1 + k;
        vPos1 = uPos1 + 2;
        vPos2 = uPos2 + 2;

        if (uPos1 >= 0) {
            dstBuf[vPos1] = srcBuf[j + i]; /* V table */
            dstBuf[vPos2] = srcBuf[j + i]; /* V table */
            dstBuf[uPos1] = srcBuf[j + i + 1]; /* U table */
            dstBuf[uPos2] = srcBuf[j + i + 1]; /* U table */
        }
    }

    return true;
}

bool SecCameraHardware::conversion420Tto422(uint8_t *srcBuf, uint8_t *dstBuf, int width, int height)
{
    int i, j, k;
    int vPos1, vPos2, uPos1, uPos2;

    /* copy y table */
    for (i = 0; i < width * height; i++) {
        j = i * 2;
        dstBuf[j] = srcBuf[i];
    }
    for (i = 0; i < width * height / 2; i+=2) {
        j = width * height;
        k = width * 2;
        uPos1 = (i / width) * k * 2 + (i % width) * 2 + 1;
        uPos2 = uPos1 + k;
        vPos1 = uPos1 + 2;
        vPos2 = uPos2 + 2;

        if (uPos1 >= 0) {
            dstBuf[uPos1] = srcBuf[j + i]; /* V table */
            dstBuf[uPos2] = srcBuf[j + i]; /* V table */
            dstBuf[vPos1] = srcBuf[j + i + 1]; /* U table */
            dstBuf[vPos2] = srcBuf[j + i + 1]; /* U table */
        }
    }

    return true;
}
#endif

#if IS_FW_DEBUG
void SecCameraHardware::dump_is_fw_log(const char *fname, int8_t *buf, uint32_t size)
{
    int nw, cnt = 0;
    uint32_t written = 0;
    char fileName[50];

    sprintf(fileName, "/sdcard/log/IS%s.log", fname);
    /* ALOGD("opening file [%s]\n", fileName); */
    int fd = open(fileName, O_RDWR | O_CREAT, 0664);
    if (fd < 0) {
        system("mkdir sdcard/log");

        fd = open(fileName, O_RDWR | O_CREAT, 0664);
        if (fd < 0) {
            ALOGE("failed to create file [%s]: %s", fileName, strerror(errno));
            return;
        }
    }

    /* ALOGD("writing %d bytes to file [%s]\n", size, fileName); */
    while (written < size) {
        nw = ::write(fd,
        buf + written,
        size - written);
        if (nw < 0) {
            ALOGE("failed to write to file [%s]: %s",
            fname, strerror(errno));
            break;
        }
        written += nw;
        cnt++;
    }
    /* ALOGD("done writing %d bytes to file [%s] in %d passes\n", size, fileName, cnt); */
    ::close(fd);
}
#endif

int SecCameraHardware::mSaveDump(const char *filepath, ExynosBuffer *dstBuf, int index)
{
    int i = 0;
    bool useFd = false;
    unsigned int chkPlane = 0;
    unsigned int chkVadr = 0;
    unsigned int chkFd = 0;
    char *vadr[3];
    FILE *yuv_fp = NULL;
    char filename[100];
    char *buffer = NULL;
    CLEAR(filename);

    /* verify plane's size, virtual adr, ion fd */
    for (i = 0; i < 3; i++) {
        chkPlane |= (dstBuf->size.extS[i] > 0) ? 1 << i : 0;
        chkVadr |= (dstBuf->virt.extP[i] != NULL) ? 1 << i : 0;
        chkFd |= (dstBuf->fd.extFd[i] > 0) ? 1 << i : 0;
    }

    /* if bad vadr exists, try to dump by fd */
    if(chkPlane != chkVadr) {
        ALOGW("WARN(%s): [%s] chkPlane:0x%x, chkVadr:0x%x, chkFd:0x%x",
                __FUNCTION__, filepath, chkPlane, chkVadr, chkFd);
        ALOGW("WARN(%s): [%s] dump by fd!!", __FUNCTION__, filepath);
        if (chkFd != chkPlane) {
            ALOGW("ERR(%s): [%s] can't dump due to bad buffers !!", __FUNCTION__, filepath);
            return -1;
        }
        useFd = true;
    }

    /* prepare virtual address */
    for (i = 0; i < 3; i++) {
        if (dstBuf->size.extS[i] > 0)
            if (useFd)
                vadr[i] = (char *)ion_map(dstBuf->fd.extFd[i], dstBuf->size.extS[i], 0);
            else
                vadr[i] = dstBuf->virt.extP[i];
        else
            vadr[i] = NULL;

        ALOGD("DEBUG(%s): vadr[%d][0x%x] size=%d", __FUNCTION__, i, (unsigned int)vadr[i], dstBuf->size.extS[i]);
    }

    /* file create/open, note to "wb" */
    snprintf(filename, 100, filepath, index);
    yuv_fp = fopen(filename, "wb");
    if (yuv_fp == NULL) {
        ALOGE("Save jpeg file open error[%s]", filename);
        return -1;
    }

    int yuv_size = dstBuf->size.extS[0] + dstBuf->size.extS[1] + dstBuf->size.extS[2];

    buffer = (char *) malloc(yuv_size);
    if (buffer == NULL) {
        ALOGE("Save YUV] buffer alloc failed");
        if (yuv_fp)
            fclose(yuv_fp);

        return -1;
    }

    memcpy(buffer, vadr[0], dstBuf->size.extS[0]);
    if (dstBuf->size.extS[1] > 0) {
        memcpy(buffer + dstBuf->size.extS[0],
                vadr[1], dstBuf->size.extS[1]);
        if (dstBuf->size.extS[2] > 0) {
            memcpy(buffer + dstBuf->size.extS[0] + dstBuf->size.extS[1],
                    vadr[2], dstBuf->size.extS[2]);
        }
    }

    fflush(stdout);

    fwrite(buffer, 1, yuv_size, yuv_fp);

    fflush(yuv_fp);

    if (yuv_fp)
            fclose(yuv_fp);
    if (buffer)
            free(buffer);

    return 0;
}


int SecCameraHardware::createInstance(int cameraId)
{
    if (!init()) {
        ALOGE("createInstance: error, camera cannot be initialiezed");
        mInitialized = false;
        return NO_INIT;
    }

    initDefaultParameters();
    ALOGD("createInstance: %s camera, created ",
        cameraId == CAMERA_FACING_BACK ? "Back" : "Front");

    mInitialized = true;
    return NO_ERROR;
}

const static CameraInfo sCameraInfo[] = SEC_CAMERA_INFO;

/* Close this device */

static camera_device_t *g_cam_device;

}; /* namespace android */

#endif /* ANDROID_HARDWARE_SECCAMERAHARDWARE_CPP */