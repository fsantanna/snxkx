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
static SDL_Color EG_BG = { 0x30, 0x30, 0x30, 0xFF };

static int eg_canvas_new (int screen, int shadow) {
    if (EG_CVS_N == EG_CANVASES) {
        return -1;
    }
    EgCanvas* c = &EG_CVS[EG_CVS_N];
    memset(c, 0, sizeof(*c));
    c->tex = SDL_CreateTexture(EG_REN, SDL_PIXELFORMAT_RGBA8888,
                               SDL_TEXTUREACCESS_TARGET, EG_W, EG_H);
    SDL_SetTextureBlendMode(c->tex, SDL_BLENDMODE_BLEND);
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

// Appends a vertex to the current canvas, in a run of the texture.
static void eg_vertex (SDL_Texture* tex, SDL_FPoint p, SDL_Color c, float u, float v) {
    if (EG_CVS_N == 0) {
        return;
    }
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
static void eg_frame (void) {
    for (int i=0; i<EG_CVS_N; i++) {
        eg_render(&EG_CVS[i], EG_CVS[i].tex, 0);
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
        SDL_RenderCopyF(EG_REN, cv->tex, NULL, &r);
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
    SDL_Texture* t = EG_CVS[i].tex;
    SDL_FPoint p0 = eg_xy(x, y),       p1 = eg_xy(x+EG_W, y);
    SDL_FPoint p2 = eg_xy(x+EG_W, y+EG_H), p3 = eg_xy(x, y+EG_H);
    eg_vertex(t, p0, c, 0, 0); eg_vertex(t, p1, c, 1, 0); eg_vertex(t, p2, c, 1, 1);
    eg_vertex(t, p0, c, 0, 0); eg_vertex(t, p2, c, 1, 1); eg_vertex(t, p3, c, 0, 1);
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
//   - the time of the tick, in microseconds (60 ticks are exactly 1s);
//   - :draw, and the frame is rendered.
//
// Environment:
//   SNX_FRAMES=n       test mode: runs n ticks with no cap, then quits
//   SNX_INPUT, SNX_SHOT  the test script (see `eg_script`)
//   SNX_MUTE=1         no audio
//   SNX_SCALE=n        window pixels per game pixel (default 2)
int main (int argc, char** argv) {
    if (SDL_Init(SDL_INIT_VIDEO|SDL_INIT_AUDIO) != 0) {
        SDL_Init(SDL_INIT_VIDEO);
    }
    TTF_Init();
    EG_SND = getenv("SNX_MUTE")==NULL &&
             (Mix_OpenAudio(44100, MIX_DEFAULT_FORMAT, 2, 1024) == 0);
    if (EG_SND) {
        Mix_AllocateChannels(64);
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
        EG_REN = SDL_CreateRenderer(EG_WIN, -1,
            EG_TEST ? SDL_RENDERER_SOFTWARE|SDL_RENDERER_TARGETTEXTURE : 0);
    }
    if (EG_REN == NULL) {
        fprintf(stderr, "sdl : %s\n", SDL_GetError());
        exit(1);
    }
    SDL_SetRenderDrawBlendMode(EG_REN, SDL_BLENDMODE_BLEND);

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

        // time: 1/60 s, rounded so that 60 ticks are exactly 1 s
        if (st == A_OK) {
            int64_t us = (int64_t)tick*1000000/60 - (int64_t)(tick-1)*1000000/60;
            st = atm_emit(a_int(us));
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
        Mix_CloseAudio();
    }
    TTF_Quit();
    SDL_Quit();
    return (st == A_OK) ? 0 : 1;
}
