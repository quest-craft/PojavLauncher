#include <jni.h>
#include <assert.h>
#include <dlfcn.h>

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <unistd.h>

#include <EGL/egl.h>

#ifdef GLES_TEST
#include <GLES2/gl2.h>
#endif

#include <GLES3/gl32.h>

#include <android/native_window.h>
#include <android/native_window_jni.h>

#include "utils.h"

#include "OpenCompositeStub/oc_stub.h"
#include "log.h"

static void GL_APIENTRY glDebugOutput(GLenum source,
                               GLenum type,
                               unsigned int id,
                               GLenum severity,
                               GLsizei length,
                               const char *message,
                               const void *userParam);

struct PotatoBridge {
	/* ANativeWindow */ void* androidWindow;

	/* EGLContext */ void* eglContextOld;
	/* EGLContext */ void* eglContext;
	/* EGLDisplay */ void* eglDisplay;
	/* EGLSurface */ void* eglSurface;
/*
	void* eglSurfaceRead;
	void* eglSurfaceDraw;
*/
};
struct PotatoBridge potatoBridge;
EGLConfig config;

typedef jint RegalMakeCurrent_func(EGLContext context);

// Called from JNI_OnLoad of liblwjgl_opengl
void pojav_openGLOnLoad() {

}
void pojav_openGLOnUnload() {

}

void terminateEgl() {
    printf("EGLBridge: Terminating\n");
    eglMakeCurrent(potatoBridge.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(potatoBridge.eglDisplay, potatoBridge.eglSurface);
    eglDestroyContext(potatoBridge.eglDisplay, potatoBridge.eglContext);
    eglTerminate(potatoBridge.eglDisplay);
    eglReleaseThread();

    potatoBridge.eglContext = EGL_NO_CONTEXT;
    potatoBridge.eglDisplay = EGL_NO_DISPLAY;
    potatoBridge.eglSurface = EGL_NO_SURFACE;
}

JNIEXPORT void JNICALL Java_net_kdt_pojavlaunch_utils_JREUtils_setupBridgeWindow(JNIEnv* env, jclass clazz, jobject surface) {
    potatoBridge.androidWindow = ANativeWindow_fromSurface(env, surface);
}

JNIEXPORT jlong JNICALL Java_org_lwjgl_glfw_GLFW_nativeEglGetCurrentContext(JNIEnv* env, jclass clazz) {
    return (jlong) eglGetCurrentContext();
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_glfw_GLFW_nativeEglInit(JNIEnv* env, jclass clazz) {
    if (potatoBridge.eglDisplay == NULL || potatoBridge.eglDisplay == EGL_NO_DISPLAY) {
        potatoBridge.eglDisplay = eglGetDisplay(EGL_DEFAULT_DISPLAY);
        if (potatoBridge.eglDisplay == EGL_NO_DISPLAY) {
            printf("EGLBridge: Error eglGetDefaultDisplay() failed: %p\n", eglGetError());
            return JNI_FALSE;
        }
    }

    printf("EGLBridge: Initializing\n");
    // printf("EGLBridge: ANativeWindow pointer = %p\n", potatoBridge.androidWindow);
    //(*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/Exception"),"Trace exception");
    if (!eglInitialize(potatoBridge.eglDisplay, NULL, NULL)) {
        printf("EGLBridge: Error eglInitialize() failed: %d\n", eglGetError());
        return JNI_FALSE;
    }

    static const EGLint attribs[] = {
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
            // Minecraft required on initial 24
            EGL_DEPTH_SIZE, 24,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_NONE
    };

    EGLint num_configs;
    EGLint vid;

    if (!eglChooseConfig(potatoBridge.eglDisplay, attribs, &config, 1, &num_configs)) {
        printf("EGLBridge: Error couldn't get an EGL visual config: %d\n", eglGetError());
        return JNI_FALSE;
    }

    assert(config);
    assert(num_configs > 0);

    if (!eglGetConfigAttrib(potatoBridge.eglDisplay, config, EGL_NATIVE_VISUAL_ID, &vid)) {
        printf("EGLBridge: Error eglGetConfigAttrib() failed: %d\n", eglGetError());
        return JNI_FALSE;
    }

    eglBindAPI(EGL_OPENGL_ES_API);

    // If we're in VR there's no active window
    if (!potatoBridge.androidWindow) {
        // Tell OpenComposite about this EGL context
        OCWrapper_EGLInitInfo ocInfo;
        ocInfo.context = potatoBridge.eglContext;
        ocInfo.config = config;
        ocInfo.display = potatoBridge.eglDisplay;
        OCWrapper_InitEGL(&ocInfo);

        potatoBridge.eglSurface = eglCreatePbufferSurface(potatoBridge.eglDisplay, config, NULL);
        if (!potatoBridge.eglSurface) {
            printf("EGLBridge: Error eglCreatePbufferSurface failed: %d\n", eglGetError());
            return JNI_FALSE;
        }

        printf("Created pbuffersurface\n");
    } else {
        ANativeWindow_setBuffersGeometry(potatoBridge.androidWindow, 0, 0, vid);

        potatoBridge.eglSurface = eglCreateWindowSurface(potatoBridge.eglDisplay, config,
                                                         potatoBridge.androidWindow, NULL);

        if (!potatoBridge.eglSurface) {
            printf("EGLBridge: Error eglCreateWindowSurface failed: %d\n", eglGetError());
            //(*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/Exception"),"Trace exception");
            return JNI_FALSE;
        }

        // sanity checks
        {
            EGLint val;
            assert(eglGetConfigAttrib(potatoBridge.eglDisplay, config, EGL_SURFACE_TYPE, &val));
            assert(val & EGL_WINDOW_BIT);
        }
    }

    printf("EGLBridge: Initialized!\n");
    printf("EGLBridge: ThreadID=%d\n", gettid());
    printf("EGLBridge: EGLDisplay=%p, EGLSurface=%p\n",
/* window==0 ? EGL_NO_CONTEXT : */
           potatoBridge.eglDisplay,
           potatoBridge.eglSurface
    );
    return JNI_TRUE;
}


JNIEXPORT jboolean JNICALL Java_org_lwjgl_glfw_GLFW_nativeEglMakeCurrent(JNIEnv* env, jclass clazz, jlong window) {
    EGLContext *currCtx = eglGetCurrentContext();
    printf("EGLBridge: Comparing: thr=%d, this=%p, curr=%p\n", gettid(), window, currCtx);
    if (currCtx == NULL || window == 0) {
        /*if (window != 0x0 && potatoBridge.eglContextOld != NULL && potatoBridge.eglContextOld != (void *) window) {
            // Create new pbuffer per thread
            // TODO get window size for 2nd+ window!
            int surfaceWidth, surfaceHeight;
            eglQuerySurface(potatoBridge.eglDisplay, potatoBridge.eglSurface, EGL_WIDTH, &surfaceWidth);
            eglQuerySurface(potatoBridge.eglDisplay, potatoBridge.eglSurface, EGL_HEIGHT, &surfaceHeight);
            int surfaceAttr[] = {
                EGL_WIDTH, surfaceWidth,
                EGL_HEIGHT, surfaceHeight,
                EGL_NONE
            };
            potatoBridge.eglSurface = eglCreatePbufferSurface(potatoBridge.eglDisplay, config, surfaceAttr);
            printf("EGLBridge: created pbuffer surface %p for context %p\n", potatoBridge.eglSurface, window);
        }*/
        //potatoBridge.eglContextOld = (void *) window;
        // eglMakeCurrent(potatoBridge.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
        printf("EGLBridge: Making current on window %p on thread %d\n", window, gettid());
        EGLBoolean success = eglMakeCurrent(
            potatoBridge.eglDisplay,
            window==0 ? (EGLSurface *) 0 : potatoBridge.eglSurface,
            window==0 ? (EGLSurface *) 0 : potatoBridge.eglSurface,
            /* window==0 ? EGL_NO_CONTEXT : */ (EGLContext *) window
        );
        if (success == EGL_FALSE) {
            printf("EGLBridge: Error: eglMakeCurrent() failed: %p\n", eglGetError());
        } else {
            printf("EGLBridge: eglMakeCurrent() succeed!\n");
        }

    // Test
#ifdef GLES_TEST
        glClearColor(0.4f, 0.4f, 0.4f, 1.0f);
        glClear(GL_COLOR_BUFFER_BIT);
        eglSwapBuffers(potatoBridge.eglDisplay, potatoBridge.eglSurface);
        printf("First frame error: %p\n", eglGetError());
#endif

        // idk this should convert or just `return success;`...
        return success == EGL_TRUE ? JNI_TRUE : JNI_FALSE;
    } else {
        // (*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/Exception"),"Trace exception");
        return JNI_FALSE;
    }
}

JNIEXPORT void JNICALL
Java_org_lwjgl_glfw_GLFW_nativeEglDetachOnCurrentThread(JNIEnv *env, jclass clazz) {
    //Obstruct the context on the current thread
    eglMakeCurrent(potatoBridge.eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
}

JNIEXPORT jlong JNICALL
Java_org_lwjgl_glfw_GLFW_nativeEglCreateContext(JNIEnv *env, jclass clazz, jlong contextSrc) {
    const EGLint ctx_attribs[] = {
        EGL_CONTEXT_CLIENT_VERSION, atoi(getenv("LIBGL_ES")),
        // EGL_CONTEXT_OPENGL_DEBUG, EGL_TRUE,
        EGL_NONE
    };
    EGLContext* ctx = eglCreateContext(potatoBridge.eglDisplay, config, (void*)contextSrc, ctx_attribs);

    potatoBridge.eglContext = ctx;

    // glEnable(GL_DEBUG_OUTPUT);
    // glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    // glDebugMessageCallback(glDebugOutput, NULL);
    // glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, NULL, GL_TRUE);

    printf("EGLBridge: Created CTX pointer = %p\n",ctx);
    //(*env)->ThrowNew(env,(*env)->FindClass(env,"java/lang/Exception"),"Trace exception");
    return (long)ctx;
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_glfw_GLFW_nativeEglTerminate(JNIEnv* env, jclass clazz) {
    terminateEgl();
    //OCWrapper_Cleanup();
    return JNI_TRUE;
}

JNIEXPORT void JNICALL Java_org_lwjgl_opengl_GL_nativeRegalMakeCurrent(JNIEnv *env, jclass clazz) {
    printf("Regal: making current");

    RegalMakeCurrent_func *RegalMakeCurrent = (RegalMakeCurrent_func *) dlsym(RTLD_DEFAULT, "RegalMakeCurrent");
    RegalMakeCurrent(potatoBridge.eglContext);
}

bool stopSwapBuffers;
JNIEXPORT jboolean JNICALL Java_org_lwjgl_glfw_GLFW_nativeEglSwapBuffers(JNIEnv *env, jclass clazz) {
    if (stopSwapBuffers) {
        return JNI_FALSE;
    }

    jboolean result = (jboolean) eglSwapBuffers(potatoBridge.eglDisplay, eglGetCurrentSurface(EGL_DRAW));
    if (!result) {
        if (eglGetError() == EGL_BAD_SURFACE) {
            stopSwapBuffers = true;
            closeGLFWWindow();
        }
    }
    return result;
}

JNIEXPORT jboolean JNICALL Java_org_lwjgl_glfw_GLFW_nativeEglSwapInterval(JNIEnv *env, jclass clazz, jint interval) {
    return eglSwapInterval(potatoBridge.eglDisplay, interval);
}

static void GL_APIENTRY glDebugOutput(GLenum source,
                               GLenum type,
                               unsigned int id,
                               GLenum severity,
                               GLsizei length,
                               const char *message,
                               const void *userParam) {
// ignore non-significant error/warning codes
    if (id == 131169 || id == 131185 || id == 131218 || id == 131204) return;

    printf("Debug message (%d): %s", id, message);

    const char *srcString = NULL;
    switch (source) {
        case GL_DEBUG_SOURCE_API:
            srcString = "Source: API";
            break;
        case GL_DEBUG_SOURCE_WINDOW_SYSTEM:
            srcString = "Source: Window System";
            break;
        case GL_DEBUG_SOURCE_SHADER_COMPILER:
            srcString = "Source: Shader Compiler";
            break;
        case GL_DEBUG_SOURCE_THIRD_PARTY:
            srcString = "Source: Third Party";
            break;
        case GL_DEBUG_SOURCE_APPLICATION:
            srcString = "Source: Application";
            break;
        case GL_DEBUG_SOURCE_OTHER:
            srcString = "Source: Other";
            break;
        default:
            srcString = "Source: <invalid enum>";
            break;
    }
    printf("%s\n", srcString);

    const char *typeStr;
    switch (type) {
        case GL_DEBUG_TYPE_ERROR:
            typeStr = "Type: Error";
            break;
        case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
            typeStr = "Type: Deprecated Behaviour";
            break;
        case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
            typeStr = "Type: Undefined Behaviour";
            break;
        case GL_DEBUG_TYPE_PORTABILITY:
            typeStr = "Type: Portability";
            break;
        case GL_DEBUG_TYPE_PERFORMANCE:
            typeStr = "Type: Performance";
            break;
        case GL_DEBUG_TYPE_MARKER:
            typeStr = "Type: Marker";
            break;
        case GL_DEBUG_TYPE_PUSH_GROUP:
            typeStr = "Type: Push Group";
            break;
        case GL_DEBUG_TYPE_POP_GROUP:
            typeStr = "Type: Pop Group";
            break;
        case GL_DEBUG_TYPE_OTHER:
            typeStr = "Type: Other";
            break;
        default:
            typeStr = "Type: <invalid enum>";
            break;
    }
    printf("%s\n", typeStr);

    const char *severityStr;
    switch (severity) {
        case GL_DEBUG_SEVERITY_HIGH:
            severityStr = "Severity: high";
            break;
        case GL_DEBUG_SEVERITY_MEDIUM:
            severityStr = "Severity: medium";
            break;
        case GL_DEBUG_SEVERITY_LOW:
            severityStr = "Severity: low";
            break;
        case GL_DEBUG_SEVERITY_NOTIFICATION:
            severityStr = "Severity: notification";
            break;
        default:
            severityStr = "Severity: <invalid enum>";
            break;
    }
    printf("%s\n", severityStr);
}
