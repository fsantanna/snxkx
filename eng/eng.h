// The C side of the engine of the port.
//
// It is included by `eng/eng.atm`, in a prelude, after the header of the
// runtime of Atmos. It holds what is not game logic:
//
//   - the driver: window, the loop of ticks, the events of SDL, and the
//     test mode with no screen;
//   - drawing, as the original does with LÖVE: canvases of 480x270
//     scaled to the window, a stack of transforms, and shapes (rounded
//     rectangles, circles, arcs, polygons, lines), images, and text;
//   - sounds.
//
// Every shape becomes triangles, appended to the list of the current
// canvas, and the lists are rendered at the end of the frame, one call
// per texture. The shadow of the original (the main canvas drawn again
// in dark gray, half transparent, 1.5 pixels down and right) replays
// the list of the main canvas with the color of every vertex replaced.
//
// Colors cross as integers 0xRRGGBBAA. Images and fonts cross as
// indexes in tables of this file, which live until the program ends.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include <SDL2/SDL.h>
#include <SDL2/SDL_image.h>
#include <SDL2/SDL_ttf.h>
#include <SDL2/SDL_mixer.h>

#define EG_W 480                        // the size of the game, in pixels
#define EG_H 270

static SDL_Window*   EG_WIN  = NULL;
static SDL_Renderer* EG_REN  = NULL;
static int           EG_SCALE = 2;      // window pixels per game pixel
static int           EG_SND  = 0;       // audio is open
static int           EG_TEST = 0;       // test mode: fixed ticks, no cap
static int           EG_TICK = 0;       // the number of the current tick
static double        EG_MX = 0, EG_MY = 0;  // the mouse, in game pixels
static double        EG_SLOW = 1.0;     // the scale of the game time
static double        EG_GAME = 0.0;     // game time not emitted yet, in us

static int eg_int (AVal v) {
    return a_isnum(v) ? (int) a_dbl(v) : 0;
}

static double eg_dbl (AVal v) {
    return a_isnum(v) ? a_dbl(v) : 0.0;
}

static const char* eg_cstr (AVal v) {
    return (v.type == A_STR) ? A_STR_OF(v)->s : NULL;
}

static SDL_Color eg_rgba (int64_t c) {
    SDL_Color r = {
        (Uint8)((c >> 24) & 0xFF), (Uint8)((c >> 16) & 0xFF),
        (Uint8)((c >>  8) & 0xFF), (Uint8)((c >>  0) & 0xFF)
    };
    return r;
}

static SDL_Color eg_color (AVal v) {
    return eg_rgba(a_isnum(v) ? (int64_t) a_dbl(v) : 0xFFFFFFFF);
}

///////////////////////////////////////////////////////////////////////////////
// TRANSFORMS
///////////////////////////////////////////////////////////////////////////////

// An affine transform: (x,y) -> (a*x + c*y + e, b*x + d*y + f).
typedef struct {
    double a, b, c, d, e, f;
} EgMat;

#define EG_STACK 32
static EgMat EG_MATS[EG_STACK] = { {1,0,0,1,0,0} };
static int   EG_TOP = 0;

static void eg_mul (EgMat* m, EgMat n) {    // m = m * n
    EgMat r = {
        m->a*n.a + m->c*n.b,       m->b*n.a + m->d*n.b,
        m->a*n.c + m->c*n.d,       m->b*n.c + m->d*n.d,
        m->a*n.e + m->c*n.f + m->e, m->b*n.e + m->d*n.f + m->f,
    };
    *m = r;
}

static void eg_translate (double x, double y) {
    EgMat t = { 1, 0, 0, 1, x, y };
    eg_mul(&EG_MATS[EG_TOP], t);
}

static void eg_scale (double sx, double sy) {
    EgMat t = { sx, 0, 0, sy, 0, 0 };
    eg_mul(&EG_MATS[EG_TOP], t);
}

static void eg_rotate (double r) {
    double c = cos(r), s = sin(r);
    EgMat t = { c, s, -s, c, 0, 0 };
    eg_mul(&EG_MATS[EG_TOP], t);
}

// As `graphics.push` of the original: around (x,y), scales and rotates.
static void eg_push (double x, double y, double r, double sx, double sy) {
    if (EG_TOP+1 < EG_STACK) {
        EG_MATS[EG_TOP+1] = EG_MATS[EG_TOP];
        EG_TOP++;
    }
    eg_translate(x, y);
    eg_scale(sx, sy);
    eg_rotate(r);
    eg_translate(-x, -y);
}

static void eg_pop (void) {
    if (EG_TOP > 0) {
        EG_TOP--;
    }
}

static SDL_FPoint eg_xy (double x, double y) {
    EgMat* m = &EG_MATS[EG_TOP];
    SDL_FPoint p = {
        (float)(m->a*x + m->c*y + m->e),
        (float)(m->b*x + m->d*y + m->f)
    };
    return p;
}

///////////////////////////////////////////////////////////////////////////////
// CANVASES
///////////////////////////////////////////////////////////////////////////////

// A run of vertices of a list with the same texture.
typedef struct {
    SDL_Texture* tex;
    int          i0, n;
} EgRun;

typedef struct {
    SDL_Texture* tex;           // the target, EG_W x EG_H
    SDL_Texture* out;           // what is drawn of it (see `eg_canvas_out`)
    SDL_Vertex*  vs;            // the triangles of this frame
    int          vs_n, vs_max;
    EgRun*       rs;
    int          rs_n, rs_max;
    int          screen;        // composed to the window, in order
    int          shadow;        // composed with its shadow under it
} EgCanvas;

#define EG_CANVASES 8
static EgCanvas EG_CVS[EG_CANVASES];
static int      EG_CVS_N = 0;
static int      EG_CUR   = 0;   // the canvas that receives the shapes
static SDL_Texture* EG_SHADOW = NULL;

// LÖVE draws a canvas premultiplied: its content went through the alpha
// blending already. SDL blends it again, unless the renderer has a
// premultiplied blend mode (OpenGL has, the software renderer has not).
// Then the content is read back and divided by its alpha, into a
// streaming texture that SDL blends as usual.
static SDL_BlendMode EG_PREMUL;
static int           EG_PREMUL_OK = -1;     // not checked yet
static SDL_Color EG_BG = { 0x30, 0x30, 0x30, 0xFF };

static int eg_canvas_new (int screen, int shadow) {
    if (EG_CVS_N == EG_CANVASES) {
        return -1;
    }
    EgCanvas* c = &EG_CVS[EG_CVS_N];
    memset(c, 0, sizeof(*c));
    c->tex = SDL_CreateTexture(EG_REN, SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_TARGET, EG_W, EG_H);
    if (EG_PREMUL_OK == -1) {
        EG_PREMUL = SDL_ComposeCustomBlendMode(
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD,
            SDL_BLENDFACTOR_ONE, SDL_BLENDFACTOR_ONE_MINUS_SRC_ALPHA, SDL_BLENDOPERATION_ADD);
        EG_PREMUL_OK = (SDL_SetTextureBlendMode(c->tex, EG_PREMUL) == 0)
                    && getenv("SNX_NO_PREMUL") == NULL;
    }
    if (EG_PREMUL_OK) {
        SDL_SetTextureBlendMode(c->tex, EG_PREMUL);
        c->out = c->tex;
    } else {
        SDL_SetTextureBlendMode(c->tex, SDL_BLENDMODE_NONE);
        c->out = SDL_CreateTexture(EG_REN, SDL_PIXELFORMAT_RGBA32,
                                   SDL_TEXTUREACCESS_STREAMING, EG_W, EG_H);
        SDL_SetTextureBlendMode(c->out, SDL_BLENDMODE_BLEND);
    }
    c->screen = screen;
    c->shadow = shadow;
    if (shadow && EG_SHADOW==NULL) {
        EG_SHADOW = SDL_CreateTexture(EG_REN, SDL_PIXELFORMAT_RGBA8888,
                                      SDL_TEXTUREACCESS_TARGET, EG_W, EG_H);
        SDL_SetTextureBlendMode(EG_SHADOW, SDL_BLENDMODE_BLEND);
        SDL_SetTextureAlphaMod(EG_SHADOW, 128);
    }
    return EG_CVS_N++;
}

static void eg_canvas_set (int i) {
    if (i>=0 && i<EG_CVS_N) {
        EG_CUR = i;
    }
}

// The offset of every vertex, so that the renderer covers the pixels
// that LÖVE covers (measured against LÖVE 11.5 under Xvfb): a pixel
// whose center is inside a shape (an edge at e starts at pixel
// ceil(e - 0.5)), and text as if 0.001 further (at 10.4999 it starts at
// pixel 11). OpenGL covers pixels by their centers too, so it only
// needs the 0.001 of text. The software renderer floors positions, so
// it needs 0.499 more. The values are set when the renderer is created
// (see `main`).
static float EG_OFF_SHAPE = 0.0f;
static float EG_OFF_TEXT  = 0.001f;
static float EG_OFF       = 0.0f;

// Appends a vertex to the current canvas, in a run of the texture.
static void eg_vertex (SDL_Texture* tex, SDL_FPoint p, SDL_Color c, float u, float v) {
    if (EG_CVS_N == 0) {
        return;
    }
    p.x += EG_OFF;
    p.y += EG_OFF;
    EgCanvas* cv = &EG_CVS[EG_CUR];
    if (cv->vs_n == cv->vs_max) {
        cv->vs_max = cv->vs_max ? cv->vs_max*2 : 4096;
        cv->vs = realloc(cv->vs, cv->vs_max * sizeof(SDL_Vertex));
    }
    if (cv->rs_n==0 || cv->rs[cv->rs_n-1].tex!=tex) {
        if (cv->rs_n == cv->rs_max) {
            cv->rs_max = cv->rs_max ? cv->rs_max*2 : 256;
            cv->rs = realloc(cv->rs, cv->rs_max * sizeof(EgRun));
        }
        EgRun r = { tex, cv->vs_n, 0 };
        cv->rs[cv->rs_n++] = r;
    }
    SDL_Vertex vx = { p, c, { u, v } };
    cv->vs[cv->vs_n++] = vx;
    cv->rs[cv->rs_n-1].n++;
}

// A triangle in game coordinates, in the current transform.
static void eg_tri (double x1, double y1, double x2, double y2, double x3, double y3, SDL_Color c) {
    eg_vertex(NULL, eg_xy(x1,y1), c, 0, 0);
    eg_vertex(NULL, eg_xy(x2,y2), c, 0, 0);
    eg_vertex(NULL, eg_xy(x3,y3), c, 0, 0);
}

// Renders the list of a canvas into a target. With `shadow`, every
// vertex has the color of the shadow of the original (0.1 gray), with
// its own alpha.
static void eg_render (EgCanvas* cv, SDL_Texture* dst, int shadow) {
    SDL_SetRenderTarget(EG_REN, dst);
    SDL_SetRenderDrawColor(EG_REN, shadow?25:0, shadow?25:0, shadow?25:0, 0);
    SDL_RenderClear(EG_REN);
    if (shadow) {
        for (int i=0; i<cv->vs_n; i++) {
            cv->vs[i].color.r = cv->vs[i].color.g = cv->vs[i].color.b = 25;
        }
    }
    for (int i=0; i<cv->rs_n; i++) {
        EgRun* r = &cv->rs[i];
        SDL_RenderGeometry(EG_REN, r->tex, &cv->vs[r->i0], r->n, NULL, 0);
    }
}

// The end of a frame: renders the canvases, in the order of creation
// (a canvas may draw an earlier one), and composes them to the window.
// Makes `out` of a canvas from its target, when the renderer has no
// premultiplied blend mode: each color divided by its alpha.
static void eg_canvas_out (EgCanvas* cv) {
    if (EG_PREMUL_OK) {
        return;
    }
    static Uint8 px[EG_W*EG_H*4];
    SDL_RenderReadPixels(EG_REN, NULL, SDL_PIXELFORMAT_RGBA32, px, EG_W*4);
    for (int i=0; i<EG_W*EG_H; i++) {
        Uint8* p = &px[i*4];
        int a = p[3];
        if (a!=0 && a!=255) {
            for (int k=0; k<3; k++) {
                int v = (p[k]*255 + a/2) / a;
                p[k] = (v > 255) ? 255 : v;
            }
        }
    }
    SDL_UpdateTexture(cv->out, NULL, px, EG_W*4);
}

static void eg_frame (void) {
    for (int i=0; i<EG_CVS_N; i++) {
        eg_render(&EG_CVS[i], EG_CVS[i].tex, 0);
        eg_canvas_out(&EG_CVS[i]);
    }
    SDL_SetRenderTarget(EG_REN, NULL);
    SDL_SetRenderDrawColor(EG_REN, EG_BG.r, EG_BG.g, EG_BG.b, 255);
    SDL_RenderClear(EG_REN);
    SDL_FRect r = { 0, 0, EG_W*EG_SCALE, EG_H*EG_SCALE };
    for (int i=0; i<EG_CVS_N; i++) {
        EgCanvas* cv = &EG_CVS[i];
        if (!cv->screen) {
            continue;
        }
        if (cv->shadow) {
            eg_render(cv, EG_SHADOW, 1);
            SDL_SetRenderTarget(EG_REN, NULL);
            SDL_FRect s = { 1.5f*EG_SCALE, 1.5f*EG_SCALE, r.w, r.h };
            SDL_RenderCopyF(EG_REN, EG_SHADOW, NULL, &s);
        }
        SDL_RenderCopyF(EG_REN, cv->out, NULL, &r);
    }
    for (int i=0; i<EG_CVS_N; i++) {
        EG_CVS[i].vs_n = 0;
        EG_CVS[i].rs_n = 0;
    }
    EG_TOP = 0;
}

// Draws canvas `i` into the current one, at (x,y), with a color.
static void eg_canvas_draw (int i, double x, double y, SDL_Color c) {
    if (i<0 || i>=EG_CVS_N || i==EG_CUR) {
        return;
    }
    SDL_Texture* t = EG_CVS[i].out;
    SDL_FPoint p0 = eg_xy(x, y),       p1 = eg_xy(x+EG_W, y);
    SDL_FPoint p2 = eg_xy(x+EG_W, y+EG_H), p3 = eg_xy(x, y+EG_H);
    eg_vertex(t, p0, c, 0, 0); eg_vertex(t, p1, c, 1, 0); eg_vertex(t, p2, c, 1, 1);
    eg_vertex(t, p0, c, 0, 0); eg_vertex(t, p2, c, 1, 1); eg_vertex(t, p3, c, 0, 1);
}

// Draws canvas `i` into the current one, except inside the rectangle
// (x1,y1)-(x2,y2) in canvas coordinates: as `graphics.draw_with_mask`
// of the original with an inverted rectangular mask, which the menu and
// the arena use for the stars. The rectangle is in the current transform.
static void eg_canvas_draw_outside (int i, double x1, double y1, double x2, double y2, SDL_Color c) {
    if (i<0 || i>=EG_CVS_N || i==EG_CUR) {
        return;
    }
    SDL_Texture* t = EG_CVS[i].out;
    double xs[4] = { 0, x1, x2, EG_W };
    double ys[4] = { 0, y1, y2, EG_H };
    for (int a=0; a<3; a++) {
        for (int b=0; b<3; b++) {
            if (a==1 && b==1) {
                continue;               // the hole
            }
            double qx0 = xs[a], qx1 = xs[a+1], qy0 = ys[b], qy1 = ys[b+1];
            if (qx1<=qx0 || qy1<=qy0) {
                continue;
            }
            float u0 = qx0/EG_W, u1 = qx1/EG_W, v0 = qy0/EG_H, v1 = qy1/EG_H;
            SDL_FPoint p0 = eg_xy(qx0, qy0), p1 = eg_xy(qx1, qy0);
            SDL_FPoint p2 = eg_xy(qx1, qy1), p3 = eg_xy(qx0, qy1);
            eg_vertex(t, p0, c, u0, v0); eg_vertex(t, p1, c, u1, v0); eg_vertex(t, p2, c, u1, v1);
            eg_vertex(t, p0, c, u0, v0); eg_vertex(t, p2, c, u1, v1); eg_vertex(t, p3, c, u0, v1);
        }
    }
}

///////////////////////////////////////////////////////////////////////////////
// SHAPES
///////////////////////////////////////////////////////////////////////////////

// The number of segments of an ellipse, as LÖVE 11.
static int eg_points (double rx, double ry) {
    int n = (int) sqrt(((rx + ry) / 2.0) * 20.0);
    return (n < 8) ? 8 : n;
}

#define EG_PTS 1024
static double EG_PX[EG_PTS], EG_PY[EG_PTS];

// A convex polygon of the points in EG_PX/EG_PY, as a fan.
static void eg_fill (int n, SDL_Color c) {
    for (int i=1; i+1<n; i++) {
        eg_tri(EG_PX[0],EG_PY[0], EG_PX[i],EG_PY[i], EG_PX[i+1],EG_PY[i+1], c);
    }
}

// A line through the points in EG_PX/EG_PY, closed or not, with a
// width, as quads joined by miters (as LÖVE).
static void eg_stroke (int n, int closed, double w, SDL_Color c) {
    if (n < 2) {
        return;
    }
    static double LX[EG_PTS], LY[EG_PTS], RX[EG_PTS], RY[EG_PTS];
    double h = w / 2;
    for (int i=0; i<n; i++) {
        int ip = closed ? (i+n-1)%n : (i>0   ? i-1 : i);
        int in = closed ? (i+1)%n   : (i<n-1 ? i+1 : i);
        // the normals of the segments before and after the point
        double ax = EG_PX[i]-EG_PX[ip], ay = EG_PY[i]-EG_PY[ip];
        double bx = EG_PX[in]-EG_PX[i], by = EG_PY[in]-EG_PY[i];
        double la = sqrt(ax*ax+ay*ay), lb = sqrt(bx*bx+by*by);
        if (la == 0) { ax=bx; ay=by; la=lb; }
        if (lb == 0) { bx=ax; by=ay; lb=la; }
        if (la == 0) { la = lb = 1; }
        double nax = -ay/la, nay = ax/la, nbx = -by/lb, nby = bx/lb;
        double mx = nax+nbx, my = nay+nby;
        double ml = sqrt(mx*mx+my*my);
        double k = h;
        if (ml < 1e-6) {
            mx = nax; my = nay; ml = 1;
        } else {
            double cs = (mx*nax + my*nay) / ml;     // cos of half the turn
            k = (cs > 0.25) ? h/cs : h*4;
        }
        mx = mx/ml*k; my = my/ml*k;
        LX[i] = EG_PX[i]+mx; LY[i] = EG_PY[i]+my;
        RX[i] = EG_PX[i]-mx; RY[i] = EG_PY[i]-my;
    }
    int segs = closed ? n : n-1;
    for (int i=0; i<segs; i++) {
        int j = (i+1) % n;
        eg_tri(LX[i],LY[i], LX[j],LY[j], RX[j],RY[j], c);
        eg_tri(LX[i],LY[i], RX[j],RY[j], RX[i],RY[i], c);
    }
}

// Appends the arc of an ellipse to the points, from angle a1 to a2,
// with `n` segments (n+1 points).
static int eg_arc_pts (int k, double x, double y, double rx, double ry,
                       double a1, double a2, int n) {
    for (int i=0; i<=n && k<EG_PTS; i++) {
        double a = a1 + (a2-a1)*i/n;
        EG_PX[k] = x + rx*cos(a);
        EG_PY[k] = y + ry*sin(a);
        k++;
    }
    return k;
}

// A rectangle at (x,y) top-left, size (w,h), with rounded corners of
// radius (rx,ry), filled or with a line of width `lw` (when lw > 0).
static void eg_rect (double x, double y, double w, double h, double rx, double ry,
                     SDL_Color c, double lw) {
    int n = 0;
    if (rx<=0 && ry<=0) {
        EG_PX[0]=x;   EG_PY[0]=y;
        EG_PX[1]=x+w; EG_PY[1]=y;
        EG_PX[2]=x+w; EG_PY[2]=y+h;
        EG_PX[3]=x;   EG_PY[3]=y+h;
        n = 4;
    } else {
        if (rx > w/2) rx = w/2;
        if (ry > h/2) ry = h/2;
        if (ry <= 0) ry = rx;
        int p = eg_points(rx, ry) / 4;
        if (p < 1) p = 1;
        double hp = M_PI/2;
        n = eg_arc_pts(n, x+w-rx, y+h-ry, rx, ry, 0,    hp,    p);
        n = eg_arc_pts(n, x+rx,   y+h-ry, rx, ry, hp,   2*hp,  p);
        n = eg_arc_pts(n, x+rx,   y+ry,   rx, ry, 2*hp, 3*hp,  p);
        n = eg_arc_pts(n, x+w-rx, y+ry,   rx, ry, 3*hp, 4*hp,  p);
    }
    if (lw > 0) {
        eg_stroke(n, 1, lw, c);
    } else {
        eg_fill(n, c);
    }
}

static void eg_ellipse (double x, double y, double rx, double ry, SDL_Color c, double lw) {
    int p = eg_points(rx, ry);
    int n = eg_arc_pts(0, x, y, rx, ry, 0, 2*M_PI, p) - 1;
    if (lw > 0) {
        eg_stroke(n, 1, lw, c);
    } else {
        eg_fill(n, c);
    }
}

// An arc from angle a1 to a2: type 0 pie, 1 open, 2 closed.
static void eg_arc (int type, double x, double y, double r, double a1, double a2,
                    SDL_Color c, double lw) {
    int p = (int)(eg_points(r, r) * fabs(a2-a1) / (2*M_PI));
    if (p < 1) p = 1;
    int n = 0;
    if (type == 0) {
        EG_PX[0] = x; EG_PY[0] = y; n = 1;
    }
    n = eg_arc_pts(n, x, y, r, r, a1, a2, p);
    if (lw > 0) {
        eg_stroke(n, type!=1, lw, c);
    } else {
        eg_fill(n, c);
    }
}

// A polygon or a polyline of the points in a vector [x1,y1,x2,y2,...].
static int eg_pts (AVal t) {
    int n = 0;
    if (t.type != A_TAB) {
        return 0;
    }
    for (int i=0; n<EG_PTS; i+=2) {
        AVal kx = a_int(i), ky = a_int(i+1);
        AVal vx = a_tab_get(A_TAB_OF(t), kx);
        AVal vy = a_tab_get(A_TAB_OF(t), ky);
        if (!a_isnum(vx) || !a_isnum(vy)) {
            break;
        }
        EG_PX[n] = a_dbl(vx);
        EG_PY[n] = a_dbl(vy);
        n++;
    }
    return n;
}

static void eg_polygon (AVal t, SDL_Color c, double lw) {
    int n = eg_pts(t);
    if (lw > 0) {
        eg_stroke(n, 1, lw, c);
    } else {
        eg_fill(n, c);
    }
}

static void eg_polyline (AVal t, SDL_Color c, double lw) {
    eg_stroke(eg_pts(t), 0, lw>0 ? lw : 1, c);
}

static void eg_line (double x1, double y1, double x2, double y2, SDL_Color c, double lw) {
    EG_PX[0] = x1; EG_PY[0] = y1;
    EG_PX[1] = x2; EG_PY[1] = y2;
    eg_stroke(2, 0, lw>0 ? lw : 1, c);
}

// A quad with a color per corner: (x,y) top-left, (w,h), top and bottom
// colors (the vertical gradient of the background of the original).
static void eg_gradient (double x, double y, double w, double h, SDL_Color c1, SDL_Color c2) {
    SDL_FPoint p0 = eg_xy(x,y), p1 = eg_xy(x+w,y), p2 = eg_xy(x+w,y+h), p3 = eg_xy(x,y+h);
    eg_vertex(NULL, p0, c1, 0, 0); eg_vertex(NULL, p1, c1, 0, 0); eg_vertex(NULL, p2, c2, 0, 0);
    eg_vertex(NULL, p0, c1, 0, 0); eg_vertex(NULL, p2, c2, 0, 0); eg_vertex(NULL, p3, c2, 0, 0);
}

///////////////////////////////////////////////////////////////////////////////
// FONTS
///////////////////////////////////////////////////////////////////////////////

// A font: the printable ASCII glyphs in one texture, as rendered by
// FreeType (through SDL_ttf). As LÖVE 11, the height of the font is the
// line height of FreeType, a glyph is drawn with its left bearing from
// the pen and its top `maxy` above the baseline, which is at the ascent
// from the top, and the pen moves by the advance (no kerning).
#define EG_GLYPH0 32
#define EG_GLYPHS 95
typedef struct {
    SDL_Texture* tex;
    int          tw, th;
    SDL_Rect     box[EG_GLYPHS];    // in the texture
    int          ox[EG_GLYPHS];     // the offset of the box from the pen
    int          oy[EG_GLYPHS];
    int          adv[EG_GLYPHS];
    int          h;                 // the height of the font
} EgFont;

#define EG_FONTS 16
static EgFont EG_FNTS[EG_FONTS];
static int    EG_FNTS_N = 0;

// A glyph alone, cropped to the pixels it covers.
static SDL_Surface* eg_glyph (TTF_Font* f, Uint32 ch) {
    SDL_Color white = { 255, 255, 255, 255 };
    SDL_Surface* g = TTF_RenderGlyph32_Blended(f, ch, white);
    if (g == NULL) {
        return NULL;
    }
    SDL_Surface* s = SDL_ConvertSurfaceFormat(g, SDL_PIXELFORMAT_RGBA32, 0);
    SDL_FreeSurface(g);
    int x0 = s->w, y0 = s->h, x1 = -1, y1 = -1;
    for (int y=0; y<s->h; y++) {
        Uint8* row = (Uint8*)s->pixels + y*s->pitch;
        for (int x=0; x<s->w; x++) {
            if (row[x*4+3] != 0) {
                if (x < x0) x0 = x;
                if (x > x1) x1 = x;
                if (y < y0) y0 = y;
                if (y > y1) y1 = y;
            }
        }
    }
    if (x1 < 0) {
        SDL_FreeSurface(s);
        return NULL;
    }
    SDL_Surface* c = SDL_CreateRGBSurfaceWithFormat(0, x1-x0+1, y1-y0+1, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_Rect r = { x0, y0, x1-x0+1, y1-y0+1 };
    SDL_SetSurfaceBlendMode(s, SDL_BLENDMODE_NONE);
    SDL_BlitSurface(s, &r, c, NULL);
    SDL_FreeSurface(s);
    return c;
}

static int eg_font_new (const char* path, int size) {
    if (path==NULL || EG_FNTS_N==EG_FONTS) {
        return -1;
    }
    TTF_Font* f = TTF_OpenFont(path, size);
    if (f == NULL) {
        fprintf(stderr, "font : %s : %s\n", path, TTF_GetError());
        return -1;
    }
    EgFont* ft = &EG_FNTS[EG_FNTS_N];
    memset(ft, 0, sizeof(*ft));
    ft->h = TTF_FontLineSkip(f);
    int asc = TTF_FontAscent(f);
    SDL_Surface* gs[EG_GLYPHS];
    int w = 0;
    for (int i=0; i<EG_GLYPHS; i++) {
        int minx, maxx, miny, maxy, adv;
        TTF_GlyphMetrics32(f, EG_GLYPH0+i, &minx, &maxx, &miny, &maxy, &adv);
        ft->adv[i] = adv;
        ft->ox[i]  = minx;
        ft->oy[i]  = asc - maxy;
        gs[i] = (i == 0) ? NULL : eg_glyph(f, EG_GLYPH0+i);
        if (gs[i] != NULL) {
            w += gs[i]->w + 1;
            if (gs[i]->h > ft->th) {
                ft->th = gs[i]->h;
            }
        }
    }
    ft->tw = (w > 0) ? w : 1;
    if (ft->th == 0) {
        ft->th = 1;
    }
    SDL_Surface* atlas = SDL_CreateRGBSurfaceWithFormat(0, ft->tw, ft->th, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_FillRect(atlas, NULL, SDL_MapRGBA(atlas->format, 255, 255, 255, 0));
    int x = 0;
    for (int i=0; i<EG_GLYPHS; i++) {
        if (gs[i] == NULL) {
            continue;
        }
        SDL_Rect r = { x, 0, gs[i]->w, gs[i]->h };
        SDL_SetSurfaceBlendMode(gs[i], SDL_BLENDMODE_NONE);
        SDL_BlitSurface(gs[i], NULL, atlas, &r);
        ft->box[i] = r;
        x += gs[i]->w + 1;
        SDL_FreeSurface(gs[i]);
    }
    ft->tex = SDL_CreateTextureFromSurface(EG_REN, atlas);
    SDL_SetTextureBlendMode(ft->tex, SDL_BLENDMODE_BLEND);
    SDL_FreeSurface(atlas);
    TTF_CloseFont(f);
    return EG_FNTS_N++;
}

static EgFont* eg_font (int i) {
    return (i>=0 && i<EG_FNTS_N) ? &EG_FNTS[i] : NULL;
}

static int eg_text_width (int fi, const char* s) {
    EgFont* f = eg_font(fi);
    int w = 0;
    for (; f!=NULL && s!=NULL && *s; s++) {
        int i = (unsigned char)*s - EG_GLYPH0;
        if (i>=0 && i<EG_GLYPHS) {
            w += f->adv[i];
        }
    }
    return w;
}

// Prints a text as `love.graphics.print(s, x, y, r, sx, sy, ox, oy)`.
static void eg_print (int fi, const char* s, double x, double y, double r,
                      double sx, double sy, double ox, double oy, SDL_Color c) {
    EgFont* f = eg_font(fi);
    if (f==NULL || s==NULL) {
        return;
    }
    if (EG_TOP+1 < EG_STACK) {
        EG_MATS[EG_TOP+1] = EG_MATS[EG_TOP];
        EG_TOP++;
    }
    eg_translate(x, y);
    eg_rotate(r);
    eg_scale(sx, sy);
    eg_translate(-ox, -oy);
    EG_OFF = EG_OFF_TEXT;
    double px = 0;
    for (; *s; s++) {
        int i = (unsigned char)*s - EG_GLYPH0;
        if (i<0 || i>=EG_GLYPHS) {
            continue;
        }
        SDL_Rect b = f->box[i];
        if (b.w > 0) {
            float u0 = (float)b.x/f->tw, u1 = (float)(b.x+b.w)/f->tw;
            float v0 = (float)b.y/f->th, v1 = (float)(b.y+b.h)/f->th;
            double gx = px + f->ox[i], gy = f->oy[i];
            SDL_FPoint p0 = eg_xy(gx, gy),         p1 = eg_xy(gx+b.w, gy);
            SDL_FPoint p2 = eg_xy(gx+b.w, gy+b.h), p3 = eg_xy(gx, gy+b.h);
            eg_vertex(f->tex, p0, c, u0, v0);
            eg_vertex(f->tex, p1, c, u1, v0);
            eg_vertex(f->tex, p2, c, u1, v1);
            eg_vertex(f->tex, p0, c, u0, v0);
            eg_vertex(f->tex, p2, c, u1, v1);
            eg_vertex(f->tex, p3, c, u0, v1);
        }
        px += f->adv[i];
    }
    EG_OFF = EG_OFF_SHAPE;
    eg_pop();
}

///////////////////////////////////////////////////////////////////////////////
// IMAGES
///////////////////////////////////////////////////////////////////////////////

typedef struct {
    char*        path;
    SDL_Texture* tex;
    int          w, h;
} EgImg;

#define EG_IMGS 256
static EgImg EG_IMGS_[EG_IMGS];
static int   EG_IMGS_N = 0;

// An image by path, loaded once.
static int eg_img (const char* path) {
    if (path == NULL) {
        return -1;
    }
    for (int i=0; i<EG_IMGS_N; i++) {
        if (!strcmp(EG_IMGS_[i].path, path)) {
            return i;
        }
    }
    if (EG_IMGS_N == EG_IMGS) {
        return -1;
    }
    SDL_Texture* t = IMG_LoadTexture(EG_REN, path);
    if (t == NULL) {
        fprintf(stderr, "image : %s : %s\n", path, IMG_GetError());
        return -1;
    }
    EgImg* im = &EG_IMGS_[EG_IMGS_N];
    im->path = strdup(path);
    im->tex = t;
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_BLEND);
    SDL_QueryTexture(t, NULL, NULL, &im->w, &im->h);
    return EG_IMGS_N++;
}

static int eg_img_dim (int i, int w) {
    return (i>=0 && i<EG_IMGS_N) ? (w ? EG_IMGS_[i].w : EG_IMGS_[i].h) : 0;
}

// Draws an image centered at (x,y), as `Image:draw` of the original.
static void eg_img_draw (int i, double x, double y, double r, double sx, double sy, SDL_Color c) {
    if (i<0 || i>=EG_IMGS_N) {
        return;
    }
    EgImg* im = &EG_IMGS_[i];
    if (EG_TOP+1 < EG_STACK) {
        EG_MATS[EG_TOP+1] = EG_MATS[EG_TOP];
        EG_TOP++;
    }
    eg_translate(x, y);
    eg_rotate(r);
    eg_scale(sx, sy);
    double x0 = -im->w/2.0, y0 = -im->h/2.0;
    SDL_FPoint p0 = eg_xy(x0, y0),             p1 = eg_xy(x0+im->w, y0);
    SDL_FPoint p2 = eg_xy(x0+im->w, y0+im->h), p3 = eg_xy(x0, y0+im->h);
    eg_vertex(im->tex, p0, c, 0, 0); eg_vertex(im->tex, p1, c, 1, 0); eg_vertex(im->tex, p2, c, 1, 1);
    eg_vertex(im->tex, p0, c, 0, 0); eg_vertex(im->tex, p2, c, 1, 1); eg_vertex(im->tex, p3, c, 0, 1);
    eg_pop();
}

///////////////////////////////////////////////////////////////////////////////
// SOUNDS
///////////////////////////////////////////////////////////////////////////////

// A sound is loaded once by path, whole, as the `static` sources of the
// original. An instance plays in a channel of SDL_mixer, with a volume
// and a pitch. SDL_mixer has no pitch, so an instance with a pitch other
// than 1 plays a copy of the samples, resampled once when it starts.
// The copy is released by the main thread after the channel finishes.

typedef struct {
    char*      path;
    Mix_Chunk* chk;
} EgSnd;

#define EG_SNDS 256
static EgSnd EG_SNDS_[EG_SNDS];
static int   EG_SNDS_N = 0;

#define EG_CHANS 64
static Mix_Chunk* EG_OWN[EG_CHANS];         // a resampled copy, or NULL
static int        EG_GEN[EG_CHANS];         // the instance in the channel
static volatile int EG_DONE[EG_CHANS];      // finished, set by SDL_mixer

static void eg_chan_done (int ch) {
    if (ch>=0 && ch<EG_CHANS) {
        EG_DONE[ch] = 1;
    }
}

static void eg_snd_gc (void) {
    for (int i=0; i<EG_CHANS; i++) {
        if (EG_DONE[i]) {
            EG_DONE[i] = 0;
            if (EG_OWN[i] != NULL && !Mix_Playing(i)) {
                SDL_free(EG_OWN[i]->abuf);
                SDL_free(EG_OWN[i]);
                EG_OWN[i] = NULL;
            }
        }
    }
}

static int eg_snd (const char* path) {
    if (path == NULL) {
        return -1;
    }
    for (int i=0; i<EG_SNDS_N; i++) {
        if (!strcmp(EG_SNDS_[i].path, path)) {
            return i;
        }
    }
    if (EG_SNDS_N == EG_SNDS) {
        return -1;
    }
    Mix_Chunk* c = NULL;
    if (EG_SND) {
        c = Mix_LoadWAV(path);
        if (c == NULL) {
            fprintf(stderr, "sound : %s : %s\n", path, Mix_GetError());
            return -1;
        }
    }
    EG_SNDS_[EG_SNDS_N].path = strdup(path);
    EG_SNDS_[EG_SNDS_N].chk  = c;
    return EG_SNDS_N++;
}

// The samples of a chunk resampled for a pitch, by linear interpolation
// (16-bit stereo, the format of the device).
static Mix_Chunk* eg_resample (Mix_Chunk* src, double pitch) {
    Sint16* in = (Sint16*) src->abuf;
    int n = src->alen / 4;                  // frames of two samples
    int m = (int)(n / pitch);
    if (m < 1) {
        return NULL;
    }
    Sint16* out = SDL_malloc(m * 4);
    for (int i=0; i<m; i++) {
        double p = i * pitch;
        int    j = (int) p;
        double t = p - j;
        int    k = (j+1 < n) ? j+1 : j;
        for (int c=0; c<2; c++) {
            out[i*2+c] = (Sint16)(in[j*2+c]*(1-t) + in[k*2+c]*t);
        }
    }
    Mix_Chunk* chk = SDL_malloc(sizeof(Mix_Chunk));
    chk->allocated = 0;
    chk->abuf = (Uint8*) out;
    chk->alen = m * 4;
    chk->volume = MIX_MAX_VOLUME;
    return chk;
}

// Plays sound `i` with a volume in [0,1] and a pitch; loops forever if
// `loop`. Returns the instance (channel and generation), or -1.
static int eg_snd_play (int i, double vol, double pitch, int loop) {
    if (!EG_SND || i<0 || i>=EG_SNDS_N || EG_SNDS_[i].chk==NULL) {
        return -1;
    }
    eg_snd_gc();
    Mix_Chunk* c = EG_SNDS_[i].chk;
    Mix_Chunk* own = NULL;
    if (pitch>0 && fabs(pitch-1) > 1e-6) {
        int freq, chans;
        Uint16 fmt;
        Mix_QuerySpec(&freq, &fmt, &chans);
        if (fmt==AUDIO_S16SYS && chans==2) {
            own = eg_resample(c, pitch);
            if (own != NULL) {
                c = own;
            }
        }
    }
    int ch = Mix_PlayChannel(-1, c, loop ? -1 : 0);
    if (ch<0 || ch>=EG_CHANS) {
        if (own != NULL) {
            SDL_free(own->abuf);
            SDL_free(own);
        }
        return -1;
    }
    if (EG_OWN[ch] != NULL) {           // the earlier copy of the channel
        SDL_free(EG_OWN[ch]->abuf);
        SDL_free(EG_OWN[ch]);
    }
    EG_OWN[ch] = own;
    EG_DONE[ch] = 0;
    Mix_Volume(ch, (int)(MIX_MAX_VOLUME * (vol<0 ? 0 : vol>1 ? 1 : vol)));
    EG_GEN[ch]++;
    return ch | (EG_GEN[ch] << 8);
}

static int eg_snd_chan (int h) {
    int ch = h & 0xFF;
    return (h>=0 && ch<EG_CHANS && EG_GEN[ch]==(h>>8)) ? ch : -1;
}

static int eg_snd_playing (int h) {
    int ch = eg_snd_chan(h);
    return EG_SND && ch>=0 && Mix_Playing(ch);
}

static void eg_snd_stop (int h) {
    int ch = eg_snd_chan(h);
    if (EG_SND && ch >= 0) {
        Mix_HaltChannel(ch);
    }
}

static void eg_snd_volume (int h, double vol) {
    int ch = eg_snd_chan(h);
    if (EG_SND && ch >= 0) {
        Mix_Volume(ch, (int)(MIX_MAX_VOLUME * (vol<0 ? 0 : vol>1 ? 1 : vol)));
    }
}

// The length of sound `i`, in seconds.
static double eg_snd_len (int i) {
    if (!EG_SND || i<0 || i>=EG_SNDS_N || EG_SNDS_[i].chk==NULL) {
        return 0;
    }
    int freq, chans;
    Uint16 fmt;
    Mix_QuerySpec(&freq, &fmt, &chans);
    return (double)EG_SNDS_[i].chk->alen / (freq * chans * 2);
}

///////////////////////////////////////////////////////////////////////////////
// DRIVER
///////////////////////////////////////////////////////////////////////////////

static AVal eg_field (AVal t, const char* k, AVal v) {
    AVal key = a_strz(k);
    a_setindex(t, key, v);
    a_dec(key);
    a_dec(v);
    return t;
}

// Emits `:tag`, `:tag [name=...]`, or `:tag [x=,y=,but=]`.
static AStatus eg_emit (const char* tag, const char* name, double x, double y, int but) {
    AVal t = a_strz(tag);
    AVal evt = a_tab();
    a_tag(evt, t);
    if (name != NULL) {
        eg_field(evt, "name", a_strz(name));
    } else if (but == -2) {
        eg_field(evt, "us", a_int((int64_t)x));
    } else if (but >= 0) {
        eg_field(evt, "x", a_num(x));
        eg_field(evt, "y", a_num(y));
        eg_field(evt, "but", a_int(but));
    }
    AStatus st = atm_emit(evt);
    a_dec(evt);
    a_dec(t);
    return st;
}

static void eg_shot (const char* path) {
    int w, h;
    SDL_SetRenderTarget(EG_REN, NULL);
    SDL_GetRendererOutputSize(EG_REN, &w, &h);
    SDL_Surface* sfc = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_RGBA32);
    SDL_RenderReadPixels(EG_REN, NULL, SDL_PIXELFORMAT_RGBA32, sfc->pixels, sfc->pitch);
    IMG_SavePNG(sfc, path);
    SDL_FreeSurface(sfc);
}

static AStatus eg_mouse (const char* tag, double x, double y, int but) {
    EG_MX = x;
    EG_MY = y;
    return eg_emit(tag, NULL, x, y, but);
}

// The test script, as `tools/bench/bench.lua` of the original:
//
//   SNX_INPUT = f:mv:x:y,f:dn:k,f:up:k,...   (game pixels; k is a key
//               name of SDL, or m1, m2, m3 for the mouse buttons)
//   SNX_SHOT  = f:path.png,...
static AStatus eg_script (const char* var, int tick) {
    AStatus st = A_OK;
    const char* s = getenv(var);
    while (s!=NULL && *s!='\0' && st==A_OK) {
        char item[256];
        size_t n = strcspn(s, ",");
        snprintf(item, sizeof(item), "%.*s", (int)n, s);
        s += n + (s[n] == ',');
        char* arg = strchr(item, ':');
        if (arg==NULL || atoi(item)!=tick) {
            continue;
        }
        arg++;
        if (!strcmp(var, "SNX_SHOT")) {
            eg_shot(arg);
        } else if (!strncmp(arg, "mv:", 3)) {
            double x=0, y=0;
            sscanf(arg+3, "%lf:%lf", &x, &y);
            st = eg_mouse("mouse.mv", x, y, 0);
        } else if (!strncmp(arg, "dn:", 3) || !strncmp(arg, "up:", 3)) {
            int dn = (arg[0] == 'd');
            const char* k = arg+3;
            if (k[0]=='m' && k[1]>='1' && k[1]<='3' && k[2]=='\0') {
                st = eg_mouse(dn ? "mouse.dn" : "mouse.up", EG_MX, EG_MY, k[1]-'0');
            } else {
                st = eg_emit(dn ? "key.dn" : "key.up", k, 0, 0, -1);
            }
        }
    }
    return st;
}

// The driver. Each loop is one tick of 1/60 s, as the original:
//
//   - the events of SDL and of the test script:
//       :key.dn [name]         :key.up [name]      (names of SDL: 'A', 'Left')
//       :mouse.mv [x,y]        :mouse.dn [x,y,but] :mouse.up [x,y,but]
//       :quit
//   - :tick [us], the real time of the tick, in microseconds (60 ticks
//     are exactly 1 s);
//   - the game time, the real time scaled by EG_SLOW (the slow motion of
//     the original), so that `await 1s` and `:clock` follow it;
//   - :draw, and the frame is rendered.
//
// Environment:
//   SNX_FRAMES=n       test mode: runs n ticks with no cap, then quits
//   SNX_INPUT, SNX_SHOT  the test script (see `eg_script`)
//   SNX_MUTE=1         no audio
//   SNX_SCALE=n        window pixels per game pixel (default 2)
//   SNX_RENDERER=name  a render driver of SDL (default: software in test
//                      mode, the best one otherwise)
int main (int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0) {
        SDL_Init(SDL_INIT_VIDEO);
    }
    TTF_Init();
    EG_SND = getenv("SNX_MUTE")==NULL &&
             (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0);
    if (EG_SND) {
        Mix_AllocateChannels(EG_CHANS);
        Mix_ChannelFinished(eg_chan_done);
    }
    int frames = -1;
    if (getenv("SNX_FRAMES") != NULL) {
        frames = atoi(getenv("SNX_FRAMES"));
        EG_TEST = 1;
    }
    if (getenv("SNX_SCALE") != NULL && atoi(getenv("SNX_SCALE")) > 0) {
        EG_SCALE = atoi(getenv("SNX_SCALE"));
    }
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0");
    EG_WIN = SDL_CreateWindow("SNKRX", SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                              EG_W*EG_SCALE, EG_H*EG_SCALE, 0);
    if (EG_WIN != NULL) {
        // the test mode renders in software, so that screenshots repeat
        const char* r = getenv("SNX_RENDERER");
        if (r == NULL) {
            r = EG_TEST ? "software" : NULL;
        }
        if (r != NULL) {
            SDL_SetHint(SDL_HINT_RENDER_DRIVER, r);
        }
        EG_REN = SDL_CreateRenderer(EG_WIN, -1, 0);
    }
    if (EG_REN == NULL) {
        fprintf(stderr, "sdl : %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetRenderDrawBlendMode(EG_REN, SDL_BLENDMODE_BLEND);
    {
        SDL_RendererInfo info;
        SDL_GetRendererInfo(EG_REN, &info);
        if (!strcmp(info.name, "software")) {
            EG_OFF_SHAPE = 0.499f;
            EG_OFF_TEXT  = 0.501f;
        }
        EG_OFF = EG_OFF_SHAPE;
        if (getenv("SNX_RENDERER_INFO") != NULL) {
            fprintf(stderr, "renderer : %s\n", info.name);
        }
    }

    atm_init(argc, argv);
    AStatus st = atm_main();

    const Uint64 period = SDL_GetPerformanceFrequency() / 60;
    Uint64 next = SDL_GetPerformanceCounter();
    int quit = 0;
    for (int tick=1; st==A_OK && !quit && atm_alive(); tick++) {
        EG_TICK = tick;
        SDL_Event e;
        while (st==A_OK && SDL_PollEvent(&e)) {
            switch (e.type) {
                case SDL_QUIT:
                    st = eg_emit("quit", NULL, 0, 0, -1);
                    quit = 1;
                    break;
                case SDL_KEYDOWN:
                    if (!e.key.repeat) {
                        st = eg_emit("key.dn", SDL_GetKeyName(e.key.keysym.sym), 0, 0, -1);
                    }
                    break;
                case SDL_KEYUP:
                    st = eg_emit("key.up", SDL_GetKeyName(e.key.keysym.sym), 0, 0, -1);
                    break;
                case SDL_MOUSEMOTION:
                    if (!EG_TEST) {
                        st = eg_mouse("mouse.mv", (double)e.motion.x/EG_SCALE,
                                      (double)e.motion.y/EG_SCALE, 0);
                    }
                    break;
                case SDL_MOUSEBUTTONDOWN:
                case SDL_MOUSEBUTTONUP:
                    if (!EG_TEST) {
                        st = eg_mouse(e.type==SDL_MOUSEBUTTONDOWN ? "mouse.dn" : "mouse.up",
                                      (double)e.button.x/EG_SCALE,
                                      (double)e.button.y/EG_SCALE, e.button.button);
                    }
                    break;
            }
        }
        if (st == A_OK) {
            st = eg_script("SNX_INPUT", tick);
        }

        // real time: 1/60 s, rounded so that 60 ticks are exactly 1 s
        int64_t us = (int64_t)tick*1000000/60 - (int64_t)(tick-1)*1000000/60;
        if (st == A_OK) {
            st = eg_emit("tick", NULL, (double)us, 0, -2);
        }
        // game time: the real time, scaled by the slow motion
        if (st == A_OK) {
            EG_GAME += us * EG_SLOW;
            int64_t g = (int64_t) EG_GAME;
            if (g > 0) {
                EG_GAME -= g;
                st = atm_emit(a_int(g));
            }
        }

        // screen
        if (st == A_OK) {
            st = eg_emit("draw", NULL, 0, 0, -1);
            eg_frame();
            eg_script("SNX_SHOT", tick);
            SDL_RenderPresent(EG_REN);
        }

        if (frames>0 && tick>=frames) {
            quit = 1;
        } else if (!EG_TEST) {
            next += period;
            Uint64 now = SDL_GetPerformanceCounter();
            if (now < next) {
                SDL_Delay((Uint32)((next-now)*1000 / SDL_GetPerformanceFrequency()));
            } else {
                next = now;
            }
        }
    }

    if (st == A_OK) {
        st = atm_stop();
    }
    fflush(stdout);
    if (st == A_ESCAPE) {
        st = a_escape_err();
    }
    if (st != A_OK) {
        a_error_dump();
    }
    atm_quit();
    if (EG_SND) {
        Mix_HaltChannel(-1);
        Mix_CloseAudio();
        for (int i=0; i<EG_CHANS; i++) {
            if (EG_OWN[i] != NULL) {
                SDL_free(EG_OWN[i]->abuf);
                SDL_free(EG_OWN[i]);
            }
        }
        for (int i=0; i<EG_SNDS_N; i++) {
            Mix_FreeChunk(EG_SNDS_[i].chk);
        }
    }
    TTF_Quit();
    SDL_Quit();
    return (st == A_OK) ? 0 : 1;
}
