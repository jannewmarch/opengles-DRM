
//
// Book:      OpenGL(R) ES 2.0 Programming Guide
// Authors:   Aaftab Munshi, Dan Ginsburg, Dave Shreiner
// ISBN-10:   0321502795
// ISBN-13:   9780321502797
// Publisher: Addison-Wesley Professional
// URLs:      http://safari.informit.com/9780321563835
//            http://www.opengles-book.com
//

// esUtil_DRM.c
//
//    This file contains the Linux DRM implementation of the windowing functions. 
/* This covers the code taken from the kmscube project
 *
 * Copyright (c) 2012 Arvin Schnell <arvin.schnell@gmail.com>
 * Copyright (c) 2012 Rob Clark <rob@ti.com>
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sub license,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial portions
 * of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
 * DEALINGS IN THE SOFTWARE.
 */

/*
 * Copyright (c) 2020 Jan Newmarch <jan@newmarch.name>
 *
 * Same license conditions as the above
 */

///
// Includes
//
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>
#include <sys/time.h>
#include "esUtil.h"



// from drm-common.c
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include "drm-common.h"

static uint32_t find_crtc_for_encoder(const drmModeRes *resources,
				      const drmModeEncoder *encoder) {
    int i;

    for (i = 0; i < resources->count_crtcs; i++) {
	/* possible_crtcs is a bitmask as described here:
	 * https://dvdhrm.wordpress.com/2012/09/13/linux-drm-mode-setting-api
	 */
	const uint32_t crtc_mask = 1 << i;
	const uint32_t crtc_id = resources->crtcs[i];
	if (encoder->possible_crtcs & crtc_mask) {
	    return crtc_id;
	}
    }

    /* no match found */
    return -1;
}

static uint32_t find_crtc_for_connector(const struct drm *drm, const drmModeRes *resources,
					const drmModeConnector *connector) {
    int i;

    for (i = 0; i < connector->count_encoders; i++) {
	const uint32_t encoder_id = connector->encoders[i];
	drmModeEncoder *encoder = drmModeGetEncoder(drm->fd, encoder_id);

	if (encoder) {
	    const uint32_t crtc_id = find_crtc_for_encoder(resources, encoder);

	    drmModeFreeEncoder(encoder);
	    if (crtc_id != 0) {
		return crtc_id;
	    }
	}
    }

    /* no match found */
    return -1;
}

static int get_resources(int fd, drmModeRes **resources)
{
    *resources = drmModeGetResources(fd);
    if (*resources == NULL)
	return -1;
    return 0;
}

#define MAX_DRM_DEVICES 64

static int find_drm_device(drmModeRes **resources)
{
    drmDevicePtr devices[MAX_DRM_DEVICES] = { NULL };
    int num_devices, fd = -1;

    num_devices = drmGetDevices2(0, devices, MAX_DRM_DEVICES);
    printf("Number of devices %d\n", num_devices);
    if (num_devices < 0) {
	printf("drmGetDevices2 failed: %s\n", strerror(-num_devices));
	return -1;
    }

    for (int i = 0; i < num_devices; i++) {
	drmDevicePtr device = devices[i];
	int ret;

	if (!(device->available_nodes & (1 << DRM_NODE_PRIMARY)))
	    continue;
	/* OK, it's a primary device. If we can get the
	 * drmModeResources, it means it's also a
	 * KMS-capable device.
	 */
	fd = open(device->nodes[DRM_NODE_PRIMARY], O_RDWR);
	if (fd < 0)
	    continue;
	ret = get_resources(fd, resources);
	if (!ret)
	    break;
	close(fd);
	fd = -1;
    }
    drmFreeDevices(devices, num_devices);

    if (fd < 0)
	printf("no drm device found!\n");
    return fd;
}

int init_drm(struct drm *drm, const char *device, const char *mode_str, unsigned int vrefresh)
{
    drmModeRes *resources;
    drmModeConnector *connector = NULL;
    drmModeEncoder *encoder = NULL;
    int i, ret, area;

    if (device) {
	drm->fd = open(device, O_RDWR);
	ret = get_resources(drm->fd, &resources);
	if (ret < 0 && errno == EOPNOTSUPP)
	    printf("%s does not look like a modeset device\n", device);
    } else {
	drm->fd = find_drm_device(&resources);
    }

    if (drm->fd < 0) {
	printf("could not open drm device\n");
	return -1;
    }

    if (!resources) {
	printf("drmModeGetResources failed: %s\n", strerror(errno));
	return -1;
    }

    /* find a connected connector: */
    for (i = 0; i < resources->count_connectors; i++) {
	connector = drmModeGetConnector(drm->fd, resources->connectors[i]);
	if (connector->connection == DRM_MODE_CONNECTED) {
	    /* it's connected, let's use this! */
	    break;
	}
	drmModeFreeConnector(connector);
	connector = NULL;
    }

    if (!connector) {
	/* we could be fancy and listen for hotplug events and wait for
	 * a connector..
	 */
	printf("no connected connector!\n");
	return -1;
    }

    /* find user requested mode: */
    if (mode_str && *mode_str) {
	for (i = 0; i < connector->count_modes; i++) {
	    drmModeModeInfo *current_mode = &connector->modes[i];

	    if (strcmp(current_mode->name, mode_str) == 0) {
		if (vrefresh == 0 || current_mode->vrefresh == vrefresh) {
		    drm->mode = current_mode;
		    break;
		}
	    }
	}
	if (!drm->mode)
	    printf("requested mode not found, using default mode!\n");
    }

    /* find preferred mode or the highest resolution mode: */
    if (!drm->mode) {
	for (i = 0, area = 0; i < connector->count_modes; i++) {
	    drmModeModeInfo *current_mode = &connector->modes[i];

	    if (current_mode->type & DRM_MODE_TYPE_PREFERRED) {
		drm->mode = current_mode;
		break;
	    }

	    int current_area = current_mode->hdisplay * current_mode->vdisplay;
	    if (current_area > area) {
		drm->mode = current_mode;
		area = current_area;
	    }
	}
    }

    if (!drm->mode) {
	printf("could not find mode!\n");
	return -1;
    }

    /* find encoder: */
    for (i = 0; i < resources->count_encoders; i++) {
	encoder = drmModeGetEncoder(drm->fd, resources->encoders[i]);
	if (encoder->encoder_id == connector->encoder_id)
	    break;
	drmModeFreeEncoder(encoder);
	encoder = NULL;
    }

    if (encoder) {
	drm->crtc_id = encoder->crtc_id;
    } else {
	uint32_t crtc_id = find_crtc_for_connector(drm, resources, connector);
	if (crtc_id == 0) {
	    printf("no crtc found!\n");
	    return -1;
	}

	drm->crtc_id = crtc_id;
    }

    for (i = 0; i < resources->count_crtcs; i++) {
	if (resources->crtcs[i] == drm->crtc_id) {
	    drm->crtc_index = i;
	    break;
	}
    }

    drmModeFreeResources(resources);

    drm->connector_id = connector->connector_id;

    return 0;
}


// From common.c

#include "common.h"

WEAK struct gbm_surface *
gbm_surface_create_with_modifiers(struct gbm_device *gbm,
                                  uint32_t width, uint32_t height,
                                  uint32_t format,
                                  const uint64_t *modifiers,
                                  const unsigned int count);

const struct gbm * init_gbm(int drm_fd, int w, int h, uint32_t format, uint64_t modifier)
{
    static struct gbm gbm;

    gbm.dev = gbm_create_device(drm_fd);
    gbm.format = format;
    gbm.surface = NULL;

    if (gbm_surface_create_with_modifiers) {
	gbm.surface = gbm_surface_create_with_modifiers(gbm.dev, w, h,
							gbm.format,
							&modifier, 1);

    }

    if (!gbm.surface) {
	if (modifier != DRM_FORMAT_MOD_LINEAR) {
	    fprintf(stderr, "Modifiers requested but support isn't available\n");
	    return NULL;
	}
	gbm.surface = gbm_surface_create(gbm.dev, w, h,
					 gbm.format,
					 GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);

    }

    if (!gbm.surface) {
	printf("failed to create gbm surface\n");
	return NULL;
    }

    gbm.width = w;
    gbm.height = h;

    return &gbm;
}

static bool has_ext(const char *extension_list, const char *ext)
{
    const char *ptr = extension_list;
    int len = strlen(ext);

    if (ptr == NULL || *ptr == '\0')
	return false;

    while (true) {
	ptr = strstr(ptr, ext);
	if (!ptr)
	    return false;

	if (ptr[len] == ' ' || ptr[len] == '\0')
	    return true;

	ptr += len;
    }
}

static int
match_config_to_visual(EGLDisplay egl_display,
		       EGLint visual_id,
		       EGLConfig *configs,
		       int count)
{
    int i;

    for (i = 0; i < count; ++i) {
	EGLint id;

	if (!eglGetConfigAttrib(egl_display,
				configs[i], EGL_NATIVE_VISUAL_ID,
				&id))
	    continue;

	if (id == visual_id)
	    return i;
    }

    return -1;
}

static bool
egl_choose_config(EGLDisplay egl_display, const EGLint *attribs,
                  EGLint visual_id, EGLConfig *config_out)
{
    EGLint count = 0;
    EGLint matched = 0;
    EGLConfig *configs;
    int config_index = -1;

    if (!eglGetConfigs(egl_display, NULL, 0, &count) || count < 1) {
	printf("No EGL configs to choose from.\n");
	return false;
    }
    configs = malloc(count * sizeof *configs);
    if (!configs)
	return false;

    if (!eglChooseConfig(egl_display, attribs, configs,
			 count, &matched) || !matched) {
	printf("No EGL configs with appropriate attributes.\n");
	goto out;
    }

    if (!visual_id)
	config_index = 0;

    if (config_index == -1)
	config_index = match_config_to_visual(egl_display,
					      visual_id,
					      configs,
					      matched);

    if (config_index != -1)
	*config_out = configs[config_index];

out:
    free(configs);
    if (config_index == -1)
	return false;

    return true;
}

struct egl * init_egl(ESContext *esContext, const struct gbm *gbm, int samples)
{
    static struct egl static_egl;
    struct egl* egl = &static_egl;
    EGLint major, minor;

	
    static const EGLint context_attribs[] = {
	//EGL_CONTEXT_CLIENT_VERSION, 2,
	EGL_CONTEXT_CLIENT_VERSION, 3, // JN
	EGL_NONE
    };

    const EGLint config_attribs[] = {
	EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
	EGL_RED_SIZE, 1,
	EGL_GREEN_SIZE, 1,
	EGL_BLUE_SIZE, 1,
	EGL_ALPHA_SIZE, 0,
	//EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
	EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT_KHR, // JN
	// JN EGL_SAMPLES, samples,
	EGL_NONE
    };
    const char *egl_exts_client, *egl_exts_dpy, *gl_exts;

#define get_proc_client(ext, name) do {				\
	if (has_ext(egl_exts_client, #ext))			\
	    egl->name = (void *)eglGetProcAddress(#name);	\
    } while (0)
#define get_proc_dpy(ext, name) do {				\
	if (has_ext(egl_exts_dpy, #ext))			\
	    egl->name = (void *)eglGetProcAddress(#name);	\
    } while (0)

#define get_proc_gl(ext, name) do {				\
	if (has_ext(gl_exts, #ext))				\
	    egl->name = (void *)eglGetProcAddress(#name);	\
    } while (0)

    egl_exts_client = eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
    get_proc_client(EGL_EXT_platform_base, eglGetPlatformDisplayEXT);

    // Ensure we get DRM platform, and not say X11 or Wayland
    if (egl->eglGetPlatformDisplayEXT) {
	egl->display = egl->eglGetPlatformDisplayEXT(EGL_PLATFORM_GBM_KHR,
						     gbm->dev, NULL);
    } else {
	egl->display = eglGetDisplay((void *)gbm->dev);
    }
    esContext->eglDisplay = egl->display;
	
    if (!eglInitialize(egl->display, &major, &minor)) {
	printf("failed to initialize\n");
	return NULL;
    }

    egl_exts_dpy = eglQueryString(egl->display, EGL_EXTENSIONS);
    get_proc_dpy(EGL_KHR_image_base, eglCreateImageKHR);
    get_proc_dpy(EGL_KHR_image_base, eglDestroyImageKHR);
    get_proc_dpy(EGL_KHR_fence_sync, eglCreateSyncKHR);
    get_proc_dpy(EGL_KHR_fence_sync, eglDestroySyncKHR);
    get_proc_dpy(EGL_KHR_fence_sync, eglWaitSyncKHR);
    get_proc_dpy(EGL_KHR_fence_sync, eglClientWaitSyncKHR);
    get_proc_dpy(EGL_ANDROID_native_fence_sync, eglDupNativeFenceFDANDROID);

    egl->modifiers_supported = has_ext(egl_exts_dpy,
				       "EGL_EXT_image_dma_buf_import_modifiers");

    printf("Using display %p with EGL version %d.%d\n",
	   egl->display, major, minor);

    printf("===================================\n");
    printf("EGL information:\n");
    printf("  version: \"%s\"\n", eglQueryString(egl->display, EGL_VERSION));
    printf("  vendor: \"%s\"\n", eglQueryString(egl->display, EGL_VENDOR));
    printf("  client extensions: \"%s\"\n", egl_exts_client);
    printf("  display extensions: \"%s\"\n", egl_exts_dpy);
    printf("===================================\n");

    if (!eglBindAPI(EGL_OPENGL_ES_API)) {
	printf("failed to bind api EGL_OPENGL_ES_API\n");
	return NULL;
    }

    if (!egl_choose_config(egl->display, config_attribs, gbm->format,
			   &egl->config)) {
	printf("failed to choose config\n");
	return NULL;
    }

    egl->context = eglCreateContext(egl->display, egl->config,
				    EGL_NO_CONTEXT, context_attribs);
    if (egl->context == NULL) {
	printf("failed to create context\n");
	return NULL;
    }
    esContext->eglContext = egl->context;
	
    egl->surface = eglCreateWindowSurface(egl->display, egl->config,
					  (EGLNativeWindowType)gbm->surface, NULL);
    esContext->eglSurface = egl->surface;
	
    if (egl->surface == EGL_NO_SURFACE) {
	printf("failed to create egl surface\n");
	return NULL;
    }
	
    /* connect the context to the surface */
    eglMakeCurrent(egl->display, egl->surface, egl->surface, egl->context);

    gl_exts = (char *) glGetString(GL_EXTENSIONS);
    printf("OpenGL ES information:\n");
    printf("  version: \"%s\"\n", glGetString(GL_VERSION));
    printf("  shading language version: \"%s\"\n", glGetString(GL_SHADING_LANGUAGE_VERSION));
    printf("  vendor: \"%s\"\n", glGetString(GL_VENDOR));
    printf("  renderer: \"%s\"\n", glGetString(GL_RENDERER));
    printf("  extensions: \"%s\"\n", gl_exts);
    printf("===================================\n");

    get_proc_gl(GL_OES_EGL_image, glEGLImageTargetTexture2DOES);

    return egl;
}

// from drm-legacy
static struct drm drm_static;

const struct drm * init_drm_legacy(const char *device, const char *mode_str, unsigned int vrefresh)
{
    int ret;

    ret = init_drm(&drm_static, device, mode_str, vrefresh);
    if (ret)
	return NULL;

    return &drm_static;
}


// From kmscube.c

#include "drm-common.h"

static const struct egl *egl;
static const struct gbm *gbm;
static const struct drm *drm;

///
//  WinCreate()
//
//      This function initialized the native X11 display and window for EGL
//
EGLBoolean WinCreate(ESContext *esContext, const char *title)
{
    const char *device = NULL;
    const char *video = NULL;
    char mode_str[DRM_DISPLAY_MODE_LEN] = "";
    char *p;
    enum mode mode = SMOOTH;
    uint32_t format = DRM_FORMAT_XRGB8888;
    uint64_t modifier = DRM_FORMAT_MOD_LINEAR;
    int samples = 0;
    int atomic = 0;
    int opt;
    unsigned int len;
    unsigned int vrefresh = 0;

    if (atomic)
	//drm = init_drm_atomic(device, mode_str, vrefresh);
	1; // JN - don't know what atomic does, ignore for now
    else
	drm = init_drm_legacy(device, mode_str, vrefresh);
    if (!drm) {
	printf("failed to initialize %s DRM\n", atomic ? "atomic" : "legacy");
	return -1;
    }

    gbm = init_gbm(drm->fd, drm->mode->hdisplay, drm->mode->vdisplay,
		   format, modifier);
    if (!gbm) {
	printf("failed to initialize GBM\n");
	return -1;
    }
    esContext->platformData = (void *) gbm;
	
    egl = init_egl(esContext, gbm, 0); // JN lose 0 later

    esContext->eglNativeDisplay = (EGLNativeDisplayType) gbm->dev;
    return EGL_TRUE;
}

// from drm-common.c
static void
drm_fb_destroy_callback(struct gbm_bo *bo, void *data)
{
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    struct drm_fb *fb = data;

    if (fb->fb_id)
	drmModeRmFB(drm_fd, fb->fb_id);

    free(fb);
}

struct drm_fb * drm_fb_get_from_bo(struct gbm_bo *bo)
{
    int drm_fd = gbm_device_get_fd(gbm_bo_get_device(bo));
    struct drm_fb *fb = gbm_bo_get_user_data(bo);
    uint32_t width, height, format,
	strides[4] = {0}, handles[4] = {0},
	offsets[4] = {0}, flags = 0;
    int ret = -1;

    if (fb)
	return fb;

    fb = calloc(1, sizeof *fb);
    fb->bo = bo;

    width = gbm_bo_get_width(bo);
    height = gbm_bo_get_height(bo);
    format = gbm_bo_get_format(bo);

    if (gbm_bo_get_modifier && gbm_bo_get_plane_count &&
	gbm_bo_get_stride_for_plane && gbm_bo_get_offset) {

	uint64_t modifiers[4] = {0};
	modifiers[0] = gbm_bo_get_modifier(bo);
	const int num_planes = gbm_bo_get_plane_count(bo);
	for (int i = 0; i < num_planes; i++) {
	    strides[i] = gbm_bo_get_stride_for_plane(bo, i);
	    handles[i] = gbm_bo_get_handle(bo).u32;
	    offsets[i] = gbm_bo_get_offset(bo, i);
	    modifiers[i] = modifiers[0];
	}

	if (modifiers[0]) {
	    flags = DRM_MODE_FB_MODIFIERS;
	    printf("Using modifier % PRIx64 \n", modifiers[0]);
	}

	ret = drmModeAddFB2WithModifiers(drm_fd, width, height,
					 format, handles, strides, offsets,
					 modifiers, &fb->fb_id, flags);
    }

    if (ret) {
	if (flags)
	    fprintf(stderr, "Modifiers failed!\n");

	memcpy(handles, (uint32_t [4]){gbm_bo_get_handle(bo).u32,0,0,0}, 16);
	memcpy(strides, (uint32_t [4]){gbm_bo_get_stride(bo),0,0,0}, 16);
	memset(offsets, 0, 16);
	ret = drmModeAddFB2(drm_fd, width, height, format,
			    handles, strides, offsets, &fb->fb_id, 0);
    }

    if (ret) {
	printf("failed to create fb: %s\n", strerror(errno));
	free(fb);
	return NULL;
    }

    gbm_bo_set_user_data(bo, fb, drm_fb_destroy_callback);

    return fb;
}

// from drm-legacy.c

static void page_flip_handler(int fd, unsigned int frame,
			      unsigned int sec, unsigned int usec, void *data)
{
    /* suppress 'unused parameter' warnings */
    (void)fd, (void)frame, (void)sec, (void)usec;

    int *waiting_for_flip = data;
    *waiting_for_flip = 0;
}

///
//  WinLoop()
//
//      Start main windows loop
//
void WinLoop ( ESContext *esContext )
{
    struct timeval t1, t2;
    struct timezone tz;
    float deltatime;

    // from drm-legacy.c, legacy-run()

    struct gbm *gbm = (struct gbm *) esContext->platformData;
  
    fd_set fds;
    drmEventContext evctx = {
	.version = 2,
	.page_flip_handler = page_flip_handler,
    };
    struct gbm_bo *bo;
    struct drm_fb *fb;
    uint32_t i = 0;
    int ret;
  
    eglSwapBuffers(esContext->eglDisplay, esContext->eglSurface);
    bo = gbm_surface_lock_front_buffer(gbm->surface);
    fb = drm_fb_get_from_bo(bo);
    if (!fb) {
	fprintf(stderr, "Failed to get a new framebuffer BO\n");
	return;
    }
  
    /* set mode: */
    ret = drmModeSetCrtc(drm_static.fd, drm_static.crtc_id, fb->fb_id, 0, 0,
			 &drm_static.connector_id, 1, drm_static.mode);
    if (ret) {
	printf("failed to set mode: %s\n", strerror(errno));
	return;
    }

    gettimeofday ( &t1 , &tz );

    while (1) {
	struct gbm_bo *next_bo;
	int waiting_for_flip = 1;

	gettimeofday(&t2, &tz);
        deltatime = (float)(t2.tv_sec - t1.tv_sec + (t2.tv_usec - t1.tv_usec) * 1e-6);

	if (esContext->updateFunc != NULL)
            esContext->updateFunc(esContext, deltatime);

	if (esContext->drawFunc != NULL)
	    esContext->drawFunc(esContext);
    
	eglSwapBuffers(esContext->eglDisplay, esContext->eglSurface);
	next_bo = gbm_surface_lock_front_buffer(gbm->surface);
	fb = drm_fb_get_from_bo(next_bo);
	if (!fb) {
	    fprintf(stderr, "Failed to get a new framebuffer BO\n");
	    return;
	}
    
	/*
	 * Here you could also update drm plane layers if you want
	 * hw composition
	 */
    
	ret = drmModePageFlip(drm_static.fd, drm_static.crtc_id, fb->fb_id,
			      DRM_MODE_PAGE_FLIP_EVENT, &waiting_for_flip);
	if (ret) {
	    printf("failed to queue page flip: %s\n", strerror(errno));
	    return;
	}
    
	while (waiting_for_flip) {
	    FD_ZERO(&fds);
	    FD_SET(0, &fds);
	    FD_SET(drm_static.fd, &fds);
      
	    ret = select(drm_static.fd + 1, &fds, NULL, NULL, NULL);
	    if (ret < 0) {
		printf("select err: %s\n", strerror(errno));
		return;
	    } else if (ret == 0) {
		printf("select timeout!\n");
		return;
	    } else if (FD_ISSET(0, &fds)) {
		printf("user interrupted!\n");
		return;
	    }
	    drmHandleEvent(drm_static.fd, &evctx);
	}
    
	/* release last buffer to render on again: */
	gbm_surface_release_buffer(gbm->surface, bo);
	bo = next_bo;
    }
}

///
//  Global extern.  The application must declsare this function
//  that runs the application.
//
extern int esMain( ESContext *esContext );

///
//  main()
//
//      Main entrypoint for application
//

int main ( int argc, char *argv[] )
{
    ESContext esContext;
   
    memset ( &esContext, 0, sizeof( esContext ) );


    if ( esMain ( &esContext ) != GL_TRUE )
	return 1;   
 
    WinLoop ( &esContext );

    if ( esContext.shutdownFunc != NULL )
	esContext.shutdownFunc ( &esContext );

    if ( esContext.userData != NULL )
	free ( esContext.userData );

    return 0;
}

