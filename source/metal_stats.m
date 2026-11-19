// Opt-in Metal call statistics (BO3MACFIX_METALSTATS=1), for finding render
// thread overhead. Counts per frame: render encoders, draws, and every state
// / binding call on the render command encoder, plus how many of those set
// exactly the value already bound on the same encoder ("redundant").
//
// Diagnostic only: it swizzles the driver's encoder class, which itself adds
// overhead, so never compare its frame rates with normal runs.
#import <Metal/Metal.h>
#include <objc/runtime.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "metal_stats.h"
#include "versioninfo.h"
#define Version_Prefix "[BO3MacFix-mawlicious " MACFIX_VERSION "]"

enum { S_ENCODER, S_DRAW, S_PIPELINE, S_DEPTH, S_VBUF, S_FBUF, S_VBUFOFF, S_FBUFOFF, S_VTEX, S_FTEX, S_VSAMP,
       S_FSAMP, S_VBYTES, S_FBYTES, S_CULL, S_VIEWPORT, S_SCISSOR, S_OTHER, S_COUNT };
static const char *names[S_COUNT] = {"encoders", "draws", "pipeline", "depthStencil", "vBuffer", "fBuffer",
                                     "vBufOffset", "fBufOffset", "vTexture", "fTexture", "vSampler",
                                     "fSampler", "vBytes", "fBytes", "cull", "viewport", "scissor", "other"};
static unsigned long calls[S_COUNT], redundant[S_COUNT];
static pthread_mutex_t stats_lock = PTHREAD_MUTEX_INITIALIZER;

/* Bound state of the encoder seen most recently (the game encodes on one
 * render thread, so tracking a single encoder is enough for estimates). */
static void *cur_encoder;
static void *pipeline, *depth;
static void *vbuf[32], *fbuf[32], *vtex[128], *ftex[128], *vsamp[16], *fsamp[16];
static NSUInteger vbufoff[32], fbufoff[32];
static NSUInteger cull = ~0ul;

static void enter(id enc) {
    if ((void *)enc == cur_encoder) return;
    cur_encoder = (void *)enc;
    pipeline = depth = NULL;
    memset(vbuf, 0, sizeof(vbuf)); memset(fbuf, 0, sizeof(fbuf));
    memset(vtex, 0, sizeof(vtex)); memset(ftex, 0, sizeof(ftex));
    memset(vsamp, 0, sizeof(vsamp)); memset(fsamp, 0, sizeof(fsamp));
    memset(vbufoff, 0xff, sizeof(vbufoff)); memset(fbufoff, 0xff, sizeof(fbufoff));
    cull = ~0ul;
    calls[S_ENCODER]++;
}
#define COUNT(kind, same) do { calls[kind]++; if (same) redundant[kind]++; } while (0)

typedef void (*obj_idx_fn)(id, SEL, id, NSUInteger);
typedef void (*buf_fn)(id, SEL, id, NSUInteger, NSUInteger);
typedef void (*off_fn)(id, SEL, NSUInteger, NSUInteger);
typedef void (*obj_fn)(id, SEL, id);
typedef void (*bytes_fn)(id, SEL, const void *, NSUInteger, NSUInteger);
typedef void (*uint_fn)(id, SEL, NSUInteger);

/* Original implementations, one slot per instrumented selector. */
enum { H_VTEX, H_FTEX, H_VSAMP, H_FSAMP, H_VBUF, H_FBUF, H_VOFF, H_FOFF, H_PIPE, H_DEPTH, H_VBYTES, H_FBYTES,
       H_CULL, H_D1, H_D2, H_D3, H_D4, H_D5, H_D6, H_COUNT };
static IMP orig[H_COUNT];
static SEL sels[H_COUNT];
static Class enc_class;
static int installed;

static void swz(int slot, SEL sel, id block) {
    Method m = class_getInstanceMethod(enc_class, sel);
    if (!m) return;
    sels[slot] = sel;
    orig[slot] = method_setImplementation(m, imp_implementationWithBlock(block));
    installed++;
}

static void obj_idx(int slot, int kind, void **arr, NSUInteger lim, id self_, id o, NSUInteger i) {
    pthread_mutex_lock(&stats_lock); enter(self_);
    int same = i < lim && arr[i] == (void *)o; if (i < lim) arr[i] = (void *)o;
    COUNT(kind, same); pthread_mutex_unlock(&stats_lock);
    ((obj_idx_fn)orig[slot])(self_, sels[slot], o, i);
}
static void buf(int slot, int kind, void **arr, NSUInteger *offs, id self_, id b, NSUInteger off, NSUInteger i) {
    pthread_mutex_lock(&stats_lock); enter(self_);
    int same = i < 32 && arr[i] == (void *)b && offs[i] == off; if (i < 32) { arr[i] = (void *)b; offs[i] = off; }
    COUNT(kind, same); pthread_mutex_unlock(&stats_lock);
    ((buf_fn)orig[slot])(self_, sels[slot], b, off, i);
}
static void offset(int slot, int kind, NSUInteger *offs, id self_, NSUInteger off, NSUInteger i) {
    pthread_mutex_lock(&stats_lock); enter(self_);
    int same = i < 32 && offs[i] == off; if (i < 32) offs[i] = off;
    COUNT(kind, same); pthread_mutex_unlock(&stats_lock);
    ((off_fn)orig[slot])(self_, sels[slot], off, i);
}
static void object(int slot, int kind, void **var, id self_, id o) {
    pthread_mutex_lock(&stats_lock); enter(self_);
    int same = *var == (void *)o; *var = (void *)o;
    COUNT(kind, same); pthread_mutex_unlock(&stats_lock);
    ((obj_fn)orig[slot])(self_, sels[slot], o);
}
static void draw_counted(id e) { pthread_mutex_lock(&stats_lock); enter(e); calls[S_DRAW]++; pthread_mutex_unlock(&stats_lock); }

static void report(void) {
    static unsigned long frames;
    frames++;
    const char *interval = getenv("BO3MACFIX_PERFLOG_INTERVAL");
    unsigned long every = interval && atoi(interval) > 0 ? (unsigned long)atoi(interval) * 60 : 1800;
    if (frames % every) return;
    pthread_mutex_lock(&stats_lock);
    char line[1024];
    int n = snprintf(line, sizeof(line), Version_Prefix " [metal] per frame over %lu frames:", every);
    for (int k = 0; k < S_COUNT; k++) {
        if (!calls[k]) continue;
        n += snprintf(line + n, sizeof(line) - n, " %s=%.0f", names[k], (double)calls[k] / every);
        if (redundant[k]) n += snprintf(line + n, sizeof(line) - n, "(%.0f%% redundant)", 100.0 * redundant[k] / calls[k]);
    }
    memset(calls, 0, sizeof(calls));
    memset(redundant, 0, sizeof(redundant));
    pthread_mutex_unlock(&stats_lock);
    printf("%s\n", line);
}

void bo3_metal_stats_frame(void) { if (cur_encoder || calls[S_DRAW]) report(); }

int bo3_install_metal_stats(void) {
    const char *on = getenv("BO3MACFIX_METALSTATS");
    if (!on || strcmp(on, "1")) return 0;
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) return -1;
        MTLTextureDescriptor *td = [MTLTextureDescriptor texture2DDescriptorWithPixelFormat:MTLPixelFormatBGRA8Unorm
                                                                                      width:4 height:4 mipmapped:NO];
        td.usage = MTLTextureUsageRenderTarget;
        id<MTLTexture> tex = [dev newTextureWithDescriptor:td];
        MTLRenderPassDescriptor *rp = [MTLRenderPassDescriptor renderPassDescriptor];
        rp.colorAttachments[0].texture = tex;
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        id<MTLRenderCommandEncoder> enc = [cb renderCommandEncoderWithDescriptor:rp];
        Class cls = object_getClass(enc);
        [enc endEncoding];
        enc_class = cls;
        swz(H_VTEX, @selector(setVertexTexture:atIndex:), ^(id e, id o, NSUInteger i) { obj_idx(H_VTEX, S_VTEX, vtex, 128, e, o, i); });
        swz(H_FTEX, @selector(setFragmentTexture:atIndex:), ^(id e, id o, NSUInteger i) { obj_idx(H_FTEX, S_FTEX, ftex, 128, e, o, i); });
        swz(H_VSAMP, @selector(setVertexSamplerState:atIndex:), ^(id e, id o, NSUInteger i) { obj_idx(H_VSAMP, S_VSAMP, vsamp, 16, e, o, i); });
        swz(H_FSAMP, @selector(setFragmentSamplerState:atIndex:), ^(id e, id o, NSUInteger i) { obj_idx(H_FSAMP, S_FSAMP, fsamp, 16, e, o, i); });
        swz(H_VBUF, @selector(setVertexBuffer:offset:atIndex:), ^(id e, id b, NSUInteger o, NSUInteger i) { buf(H_VBUF, S_VBUF, vbuf, vbufoff, e, b, o, i); });
        swz(H_FBUF, @selector(setFragmentBuffer:offset:atIndex:), ^(id e, id b, NSUInteger o, NSUInteger i) { buf(H_FBUF, S_FBUF, fbuf, fbufoff, e, b, o, i); });
        swz(H_VOFF, @selector(setVertexBufferOffset:atIndex:), ^(id e, NSUInteger o, NSUInteger i) { offset(H_VOFF, S_VBUFOFF, vbufoff, e, o, i); });
        swz(H_FOFF, @selector(setFragmentBufferOffset:atIndex:), ^(id e, NSUInteger o, NSUInteger i) { offset(H_FOFF, S_FBUFOFF, fbufoff, e, o, i); });
        swz(H_PIPE, @selector(setRenderPipelineState:), ^(id e, id o) { object(H_PIPE, S_PIPELINE, &pipeline, e, o); });
        swz(H_DEPTH, @selector(setDepthStencilState:), ^(id e, id o) { object(H_DEPTH, S_DEPTH, &depth, e, o); });
        swz(H_VBYTES, @selector(setVertexBytes:length:atIndex:), ^(id e, const void *p, NSUInteger l, NSUInteger i) {
            pthread_mutex_lock(&stats_lock); enter(e); COUNT(S_VBYTES, 0); pthread_mutex_unlock(&stats_lock);
            ((bytes_fn)orig[H_VBYTES])(e, sels[H_VBYTES], p, l, i); });
        swz(H_FBYTES, @selector(setFragmentBytes:length:atIndex:), ^(id e, const void *p, NSUInteger l, NSUInteger i) {
            pthread_mutex_lock(&stats_lock); enter(e); COUNT(S_FBYTES, 0); pthread_mutex_unlock(&stats_lock);
            ((bytes_fn)orig[H_FBYTES])(e, sels[H_FBYTES], p, l, i); });
        swz(H_CULL, @selector(setCullMode:), ^(id e, NSUInteger m) {
            pthread_mutex_lock(&stats_lock); enter(e); int same = cull == m; cull = m; COUNT(S_CULL, same); pthread_mutex_unlock(&stats_lock);
            ((uint_fn)orig[H_CULL])(e, sels[H_CULL], m); });
        typedef void (*draw_ix)(id, SEL, MTLPrimitiveType, NSUInteger, MTLIndexType, id, NSUInteger, NSUInteger);
        typedef void (*draw_ix_bv)(id, SEL, MTLPrimitiveType, NSUInteger, MTLIndexType, id, NSUInteger, NSUInteger, NSInteger, NSUInteger);
        typedef void (*draw_ix1)(id, SEL, MTLPrimitiveType, NSUInteger, MTLIndexType, id, NSUInteger);
        typedef void (*draw_p)(id, SEL, MTLPrimitiveType, NSUInteger, NSUInteger, NSUInteger);
        typedef void (*draw_p1)(id, SEL, MTLPrimitiveType, NSUInteger, NSUInteger);
        typedef void (*draw_pb)(id, SEL, MTLPrimitiveType, NSUInteger, NSUInteger, NSUInteger, NSUInteger);
        swz(H_D1, @selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:),
            ^(id e, MTLPrimitiveType t, NSUInteger c, MTLIndexType it, id b, NSUInteger o, NSUInteger n) {
                draw_counted(e); ((draw_ix)orig[H_D1])(e, sels[H_D1], t, c, it, b, o, n); });
        swz(H_D2, @selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:instanceCount:baseVertex:baseInstance:),
            ^(id e, MTLPrimitiveType t, NSUInteger c, MTLIndexType it, id b, NSUInteger o, NSUInteger n, NSInteger bv, NSUInteger bi) {
                draw_counted(e); ((draw_ix_bv)orig[H_D2])(e, sels[H_D2], t, c, it, b, o, n, bv, bi); });
        swz(H_D3, @selector(drawIndexedPrimitives:indexCount:indexType:indexBuffer:indexBufferOffset:),
            ^(id e, MTLPrimitiveType t, NSUInteger c, MTLIndexType it, id b, NSUInteger o) {
                draw_counted(e); ((draw_ix1)orig[H_D3])(e, sels[H_D3], t, c, it, b, o); });
        swz(H_D4, @selector(drawPrimitives:vertexStart:vertexCount:instanceCount:),
            ^(id e, MTLPrimitiveType t, NSUInteger vs, NSUInteger vc, NSUInteger n) {
                draw_counted(e); ((draw_p)orig[H_D4])(e, sels[H_D4], t, vs, vc, n); });
        swz(H_D5, @selector(drawPrimitives:vertexStart:vertexCount:),
            ^(id e, MTLPrimitiveType t, NSUInteger vs, NSUInteger vc) {
                draw_counted(e); ((draw_p1)orig[H_D5])(e, sels[H_D5], t, vs, vc); });
        swz(H_D6, @selector(drawPrimitives:vertexStart:vertexCount:instanceCount:baseInstance:),
            ^(id e, MTLPrimitiveType t, NSUInteger vs, NSUInteger vc, NSUInteger n, NSUInteger bi) {
                draw_counted(e); ((draw_pb)orig[H_D6])(e, sels[H_D6], t, vs, vc, n, bi); });
        printf(Version_Prefix " Metal call statistics on %s: %d methods instrumented.\n", class_getName(cls), installed);
        return installed;
    }
}

/* QoS experiment: the heavy D3D11->Metal render thread is the one that
 * creates render command encoders (Aspyr's 0x31fd6). Promote it once. */
#include "cpu_sched.h"
static IMP orig_rce;
typedef id (*rce_fn)(id, SEL, id);
static id promoting_rce(id self, SEL cmd, id desc) {
    static __thread int done;
    if (!done) { done = 1; bo3_promote_current_thread(); }
    return ((rce_fn)orig_rce)(self, cmd, desc);
}

int bo3_install_render_thread_qos(void) {
    if (!bo3_qos_enabled()) return 0;
    @autoreleasepool {
        id<MTLDevice> dev = MTLCreateSystemDefaultDevice();
        if (!dev) return -1;
        id<MTLCommandQueue> q = [dev newCommandQueue];
        id<MTLCommandBuffer> cb = [q commandBuffer];
        Method m = class_getInstanceMethod(object_getClass(cb), @selector(renderCommandEncoderWithDescriptor:));
        if (!m) return -2;
        orig_rce = method_setImplementation(m, (IMP)promoting_rce);
        return 1;
    }
}
