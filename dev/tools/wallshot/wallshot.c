// wallshot — render a driftwm background shader offscreen to a PNG.
//
// Mirrors how smithay compiles a custom pixel shader: "#version 100\n" is
// prepended to the source, the vertex stage hands it `varying vec2 v_coords`
// (0..1, top-left origin) and the program gets `size`, `alpha`, plus
// driftwm's `u_camera`, `u_zoom` and `u_time`. GLES 2 on a surfaceless EGL
// context (EGL_EXT_platform_device), so it runs without a window or a
// Wayland connection and can be used in parallel.
//
//   wallshot SHADER.glsl OUT.png [--size WxH] [--time T] [--camera X,Y]
//            [--zoom Z] [--bench N]
//
// Exit 0 on success, 2 when the shader fails to compile or link (log on
// stderr), 1 for any other error. --bench N renders N extra frames and
// prints the mean GPU-bound frame time in ms.
#define _GNU_SOURCE
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>

static const char *VERT =
    "#version 100\n"
    "attribute vec2 vert;\n"
    "varying vec2 v_coords;\n"
    "void main() {\n"
    "  v_coords = vec2(vert.x, 1.0 - vert.y);\n"
    "  gl_Position = vec4(vert * 2.0 - 1.0, 0.0, 1.0);\n"
    "}\n";

static char *slurp(const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    char *buf = malloc(n + 1);
    if (fread(buf, 1, n, f) != (size_t)n) { fclose(f); free(buf); return NULL; }
    buf[n] = 0;
    fclose(f);
    return buf;
}

static GLuint compile(GLenum type, const char *src) {
    GLuint s = glCreateShader(type);
    glShaderSource(s, 1, &src, NULL);
    glCompileShader(s);
    GLint ok = 0;
    glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetShaderInfoLog(s, sizeof log, NULL, log);
        fprintf(stderr, "compile error:\n%s\n", log);
        exit(2);
    }
    return s;
}

static void put32(uint8_t *p, uint32_t v) {
    p[0] = v >> 24; p[1] = v >> 16; p[2] = v >> 8; p[3] = v;
}

static void chunk(FILE *f, const char *tag, const uint8_t *data, uint32_t len) {
    uint8_t hdr[8];
    put32(hdr, len);
    memcpy(hdr + 4, tag, 4);
    fwrite(hdr, 1, 8, f);
    if (len) fwrite(data, 1, len, f);
    uLong crc = crc32(0, (const Bytef *)tag, 4);
    if (len) crc = crc32(crc, data, len);
    uint8_t c[4];
    put32(c, (uint32_t)crc);
    fwrite(c, 1, 4, f);
}

// rows are bottom-up (glReadPixels); the PNG is written top-down.
static int write_png(const char *path, const uint8_t *rgba, int w, int h) {
    size_t stride = (size_t)w * 3 + 1;
    uint8_t *raw = malloc(stride * h);
    for (int y = 0; y < h; y++) {
        const uint8_t *src = rgba + (size_t)(h - 1 - y) * w * 4;
        uint8_t *dst = raw + stride * y;
        dst[0] = 0;
        for (int x = 0; x < w; x++) {
            dst[1 + x * 3] = src[x * 4];
            dst[2 + x * 3] = src[x * 4 + 1];
            dst[3 + x * 3] = src[x * 4 + 2];
        }
    }
    uLongf zlen = compressBound(stride * h);
    uint8_t *z = malloc(zlen);
    if (compress2(z, &zlen, raw, stride * h, 6) != Z_OK) return -1;
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    static const uint8_t sig[8] = {137, 80, 78, 71, 13, 10, 26, 10};
    fwrite(sig, 1, 8, f);
    uint8_t ihdr[13];
    put32(ihdr, w);
    put32(ihdr + 4, h);
    ihdr[8] = 8; ihdr[9] = 2; ihdr[10] = 0; ihdr[11] = 0; ihdr[12] = 0;
    chunk(f, "IHDR", ihdr, 13);
    chunk(f, "IDAT", z, (uint32_t)zlen);
    chunk(f, "IEND", NULL, 0);
    fclose(f);
    free(raw);
    free(z);
    return 0;
}

static EGLDisplay open_display(void) {
    PFNEGLQUERYDEVICESEXTPROC query =
        (PFNEGLQUERYDEVICESEXTPROC)eglGetProcAddress("eglQueryDevicesEXT");
    PFNEGLGETPLATFORMDISPLAYEXTPROC get =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC)eglGetProcAddress("eglGetPlatformDisplayEXT");
    if (query && get) {
        EGLDeviceEXT devs[16];
        EGLint n = 0;
        if (query(16, devs, &n) && n > 0) {
            // WALLSHOT_DEVICE picks one; default to the first that initializes.
            const char *want = getenv("WALLSHOT_DEVICE");
            int start = want ? atoi(want) : 0;
            for (int i = start; i < n; i++) {
                EGLDisplay d = get(EGL_PLATFORM_DEVICE_EXT, devs[i], NULL);
                if (d != EGL_NO_DISPLAY && eglInitialize(d, NULL, NULL)) return d;
            }
        }
    }
    EGLDisplay d = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    if (d == EGL_NO_DISPLAY || !eglInitialize(d, NULL, NULL)) return EGL_NO_DISPLAY;
    return d;
}

static double now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1e3 + ts.tv_nsec / 1e6;
}

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "usage: wallshot SHADER.glsl OUT.png [--size WxH] [--time T] "
                        "[--camera X,Y] [--zoom Z] [--bench N]\n");
        return 1;
    }
    int w = 1280, h = 720, bench = 0;
    float t = 3.0f, camx = 0, camy = 0, zoom = 1.0f;
    for (int i = 3; i + 1 < argc; i += 2) {
        if (!strcmp(argv[i], "--size")) sscanf(argv[i + 1], "%dx%d", &w, &h);
        else if (!strcmp(argv[i], "--time")) t = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--camera")) sscanf(argv[i + 1], "%f,%f", &camx, &camy);
        else if (!strcmp(argv[i], "--zoom")) zoom = atof(argv[i + 1]);
        else if (!strcmp(argv[i], "--bench")) bench = atoi(argv[i + 1]);
    }
    char *body = slurp(argv[1]);
    if (!body) { fprintf(stderr, "cannot read %s\n", argv[1]); return 1; }
    size_t n = strlen(body) + 32;
    char *frag = malloc(n);
    snprintf(frag, n, "#version 100\n%s", body);

    EGLDisplay dpy = open_display();
    if (dpy == EGL_NO_DISPLAY) { fprintf(stderr, "no EGL display\n"); return 1; }
    eglBindAPI(EGL_OPENGL_ES_API);
    EGLint cfg_attr[] = {EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT, EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
                         EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_NONE};
    EGLConfig cfg;
    EGLint ncfg = 0;
    if (!eglChooseConfig(dpy, cfg_attr, &cfg, 1, &ncfg) || ncfg < 1) {
        fprintf(stderr, "no EGL config\n");
        return 1;
    }
    EGLint ctx_attr[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
    EGLContext ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, ctx_attr);
    EGLint pb_attr[] = {EGL_WIDTH, 16, EGL_HEIGHT, 16, EGL_NONE};
    EGLSurface pb = eglCreatePbufferSurface(dpy, cfg, pb_attr);
    if (ctx == EGL_NO_CONTEXT || !eglMakeCurrent(dpy, pb, pb, ctx)) {
        fprintf(stderr, "cannot make an EGL context current\n");
        return 1;
    }

    GLuint fbo, rb;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glGenRenderbuffers(1, &rb);
    glBindRenderbuffer(GL_RENDERBUFFER, rb);
    glRenderbufferStorage(GL_RENDERBUFFER, 0x8058 /* GL_RGBA8_OES */, w, h);
    glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_RENDERBUFFER, rb);
    if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
        fprintf(stderr, "framebuffer incomplete\n");
        return 1;
    }

    GLuint prog = glCreateProgram();
    glAttachShader(prog, compile(GL_VERTEX_SHADER, VERT));
    glAttachShader(prog, compile(GL_FRAGMENT_SHADER, frag));
    glBindAttribLocation(prog, 0, "vert");
    glLinkProgram(prog);
    GLint ok = 0;
    glGetProgramiv(prog, GL_LINK_STATUS, &ok);
    if (!ok) {
        char log[8192];
        glGetProgramInfoLog(prog, sizeof log, NULL, log);
        fprintf(stderr, "link error:\n%s\n", log);
        return 2;
    }
    glUseProgram(prog);
    static const float quad[] = {0, 0, 1, 0, 0, 1, 1, 1};
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
    glUniform2f(glGetUniformLocation(prog, "size"), (float)w, (float)h);
    glUniform1f(glGetUniformLocation(prog, "alpha"), 1.0f);
    glUniform2f(glGetUniformLocation(prog, "u_camera"), camx, camy);
    glUniform1f(glGetUniformLocation(prog, "u_zoom"), zoom);
    GLint ut = glGetUniformLocation(prog, "u_time");
    glViewport(0, 0, w, h);

    glUniform1f(ut, t);
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    uint8_t *px = malloc((size_t)w * h * 4);
    glReadPixels(0, 0, w, h, GL_RGBA, GL_UNSIGNED_BYTE, px);
    GLenum err = glGetError();
    if (err != GL_NO_ERROR) { fprintf(stderr, "GL error 0x%x\n", err); return 1; }
    if (write_png(argv[2], px, w, h)) { fprintf(stderr, "cannot write %s\n", argv[2]); return 1; }
    double sum[3] = {0, 0, 0}, sq = 0;
    for (size_t i = 0; i < (size_t)w * h; i++) {
        double l = 0;
        for (int c = 0; c < 3; c++) { sum[c] += px[i * 4 + c]; l += px[i * 4 + c] / 3.0; }
        sq += l * l;
    }
    double npx = (double)w * h, mean_l = (sum[0] + sum[1] + sum[2]) / 3 / npx;
    // stddev of luminance: ~0 means a flat image (all black, all one colour).
    printf("mean %.1f %.1f %.1f std %.1f\n", sum[0] / npx, sum[1] / npx, sum[2] / npx,
           sqrt(sq / npx - mean_l * mean_l > 0 ? sq / npx - mean_l * mean_l : 0));

    if (bench > 0) {
        glFinish();
        double t0 = now_ms();
        for (int i = 0; i < bench; i++) {
            glUniform1f(ut, t + i / 60.0f);
            glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
            uint8_t probe[4];
            glReadPixels(0, 0, 1, 1, GL_RGBA, GL_UNSIGNED_BYTE, probe);
        }
        double ms = (now_ms() - t0) / bench;
        printf("frame_ms %.3f at %dx%d (%s)\n", ms, w, h, (const char *)glGetString(GL_RENDERER));
    }
    return 0;
}
