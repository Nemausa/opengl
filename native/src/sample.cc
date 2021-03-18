/*
 * Copyright (C) 2017-2018 Alibaba Group Holding Limited. All Rights Reserved.
 *
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
#include <assert.h>
#include <atomic>
#include <jsni.h>
#include <memory.h>
#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include "CameraOpenGLES.h"
#include <pagewindow/surface_view_context.h>

static GLuint gOffsetLoc = 0;
static uint32_t gSurfaceID = 0;
static uint32_t gWidth = 0;
static uint32_t gHeight = 0;
static pthread_t gRenderingThread;
static std::atomic_bool gRenderingFlag(true);

static GLuint createShader(const char* source, GLenum shaderType) {
    GLuint shader;
    shader = glCreateShader(shaderType);
    if (shader == 0) {
        return 0;
    }

    glShaderSource(shader, 1, static_cast<const char**>(&source), nullptr);
    glCompileShader(shader);
    GLint status;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &status);
    if (!status) {
        const static int kLogSize = 1024;
        char log[kLogSize];
        GLsizei len;
        glGetShaderInfoLog(shader, kLogSize, &len, log);
        return 0;
    }
    return shader;
}

static GLuint CreateProgram(const char* kVertexShaderText, const char* kFragmentShaderText) {
    GLuint program = glCreateProgram();
    GLuint frag = createShader(kVertexShaderText, GL_VERTEX_SHADER);
    GLuint vert = createShader(kFragmentShaderText, GL_FRAGMENT_SHADER);

    if (frag == 0 || vert == 0) {
        return 0;
    }

    glAttachShader(program, frag);
    glAttachShader(program, vert);
    glLinkProgram(program);
    GLint status;
    glGetProgramiv(program, GL_LINK_STATUS, &status);
    if (!status) {
        const static int kLogSize = 1024;
        char log[kLogSize];
        GLsizei len;
        glGetProgramInfoLog(program, kLogSize, &len, log);
        return false;
    }
    return program;
}

bool initGL() {
    if (gSurfaceID == 0) {
        return false;
    }

    static const char* kVertexShaderText =
            "attribute vec4 pos;\n"
            "uniform vec2 offset;\n"
            "void main() {\n"
            "    gl_Position = pos + vec4(offset.x, offset.y, 0, 0);\n"
            "}\n";

    static const char* kFragmentShaderText = "precision mediump float;\n"
                                             "void main() {\n"
                                             "    gl_FragColor = vec4(0.0, 1.0, 0.0, 1.0);\n"
                                             "}\n";

    static const float factor = (float)(gWidth) / (float)(gHeight);
    static const GLfloat kVertex[] = {
            -0.2f, 0.2f * factor, -0.2f, -0.2f * factor, 0.2f, -0.2f * factor, 0.2f, 0.2f * factor};
    GLuint programId = CreateProgram(kVertexShaderText, kFragmentShaderText);

    if (programId == 0) {
        return false;
    }

    glUseProgram(programId);

    GLuint posIndex = 0;
    glBindAttribLocation(programId, posIndex, "pos");
    glVertexAttribPointer(posIndex, 2, GL_FLOAT, GL_FALSE, 0, kVertex);
    glEnableVertexAttribArray(posIndex);

    gOffsetLoc = glGetUniformLocation(programId, "offset");
    glUniform2f(gOffsetLoc, .0, .0);
    return true;
}

bool drawImpl(int32_t x, int32_t y) {
    if (gSurfaceID == 0) {
        return false;
    }

    glViewport(0, 0, gWidth, gHeight);
    glClearColor(0.5, 0.5, 0.5, 1);
    glClear(GL_COLOR_BUFFER_BIT);

    glUniform2f(
            gOffsetLoc, (2 * (float)x - gWidth) / gWidth, (2 * (float)y - gHeight) / gHeight * -1);

    glDrawArrays(GL_TRIANGLE_FAN, 0, 4);

    // swap buffer
    surfaceViewSwapBuffer(gSurfaceID);
    return true;
}

void draw(JSNIEnv* env, JSNICallbackInfo info) {
    JSNISetReturnValue(env, info, JSNINewNumber(env, 0));
}

const EGLint kExpectedBufferSize = 32;
const EGLint kContextAttibutes[] = {
    EGL_CONTEXT_CLIENT_VERSION, 2,
    EGL_NONE
};

typedef bool (*FpEglConfigMatcher)(EGLDisplay eglDisplay, EGLConfig config);

bool configWithBufferSize(EGLDisplay eglDisplay, EGLConfig config) {
    EGLint size;
    eglGetConfigAttrib(eglDisplay, config, EGL_BUFFER_SIZE, &size);
    return kExpectedBufferSize == size;
}

EGLConfig findEglConfig(EGLDisplay eglDisplay, FpEglConfigMatcher matcher, EGLint* eglAttributes) {
    if(!eglAttributes) {
        return nullptr;
    }

    EGLint count;
    if (!eglGetConfigs(eglDisplay, nullptr, 0, &count) || count < 1) {
        return nullptr;
    }

    EGLConfig* configs = static_cast<EGLConfig*>(malloc(count * sizeof(EGLConfig)));
    assert(configs);
    memset(configs, 0, count * sizeof(EGLConfig));

    EGLint n;
    EGLBoolean ret = eglChooseConfig(eglDisplay, eglAttributes, configs, count, &n);
    if (!ret || n < 1) {
        return nullptr;
    }

    assert(matcher);
    EGLConfig configFound = nullptr;
    for (EGLint i = 0; i < n; i++) {
        if (matcher(eglDisplay, configs[i])) {
            configFound = configs[i];
            break;
        }
    }

    free(configs);
    configs = nullptr;

    return configFound;
}


void *thread_rendering(void *pageToken) {
    // init EGL
    void* nativeWin = surfaceViewGetNativeWindow((char *)pageToken, gSurfaceID);
    void* nativeDisplay = surfaceViewGetNativeDisplay((char *)pageToken, gSurfaceID);

    EGLint major, minor;
    void* eglDisplay = eglGetDisplay((EGLNativeDisplayType)nativeDisplay);
    if (!eglDisplay) {
        return NULL;
    }

    EGLBoolean ret = eglInitialize(eglDisplay, &major, &minor);
    if (ret != EGL_TRUE) {
        return NULL;
    }

    ret = eglBindAPI(EGL_OPENGL_ES_API);
    if (ret != EGL_TRUE) {
        return NULL;
    }

    EGLint config_attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_RED_SIZE, 1,
        EGL_GREEN_SIZE, 1,
        EGL_BLUE_SIZE, 1,
        EGL_ALPHA_SIZE, 1,
        EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
        EGL_NONE
    };

    void* eglConfig = findEglConfig(eglDisplay, configWithBufferSize, config_attribs);
    if (!eglConfig) {
        return NULL;
    }

    void* eglContext = eglCreateContext(eglDisplay, eglConfig, EGL_NO_CONTEXT, kContextAttibutes);
    if (!eglContext) {
        return NULL;
    }

    EGLSurface eglSurface = eglCreateWindowSurface(eglDisplay, eglConfig, (EGLNativeWindowType)nativeWin, nullptr);
    if (!eglSurface || EGL_TRUE != eglMakeCurrent(
            eglDisplay, eglSurface, eglSurface, eglContext)) {
        return NULL;
    }

    initGL();

    // render
    int32_t x = 0;
    int32_t y = 0;
    while (gRenderingFlag) {
        usleep(1000 * 500);
        drawImpl(x, y);
        eglSwapBuffers(eglDisplay, eglSurface);
        x = x > 300 ? 0 : x + 20;
        y = y > 300 ? 0 : y + 20;
    }

    // destroy EGL
    eglMakeCurrent(eglDisplay, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroySurface(eglDisplay, eglSurface);

    return NULL;
}

void start(JSNIEnv* env, JSNICallbackInfo info) {
    // pageToken
    size_t tokenSize = JSNIGetStringUtf8Length(env, JSNIGetArgOfCallback(env, info, 0));
    char* pageToken = new char[tokenSize];
    JSNIGetStringUtf8Chars(env, JSNIGetArgOfCallback(env, info, 0), pageToken, tokenSize);

    // surfaceID
    gSurfaceID = (uint32_t)(JSNIToCDouble(env, JSNIGetArgOfCallback(env, info, 1)));

    // width & height
    gWidth = (uint32_t)(JSNIToCDouble(env, JSNIGetArgOfCallback(env, info, 2)));
    gHeight = (uint32_t)(JSNIToCDouble(env, JSNIGetArgOfCallback(env, info, 3)));


    if (pthread_create(&gRenderingThread, NULL, thread_rendering, pageToken)) {
        JSNISetReturnValue(env, info, JSNINewNumber(env, 1));
    }
    JSNISetReturnValue(env, info, JSNINewNumber(env, 0));
}

void stop(JSNIEnv* env, JSNICallbackInfo info) {
    if (gSurfaceID == 0) {
        JSNIThrowErrorException(env, "failed, the surface id is invalid");
    }

    gRenderingFlag = false;
    JSNISetReturnValue(env, info, JSNINewNumber(env, 0));
}

int JSNIInit(JSNIEnv* env, JSValueRef exports) {
    JSNIRegisterMethod(env, exports, "startRender", start);
    JSNIRegisterMethod(env, exports, "stopRender", stop);
    JSNIRegisterMethod(env, exports, "redraw", draw);
    return JSNI_VERSION_2_1;
}
