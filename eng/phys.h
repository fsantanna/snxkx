// The physics of the port: Box2D v3, with the vocabulary of the engine
// of the original (`engine/game/physics.lua` over LÖVE).
//
// As LÖVE with `love.physics.setMeter(m)`, positions, sizes, velocities,
// forces, and impulses cross in pixels, and Box2D sees them divided by
// `m` (32 in the arena). Angles cross in radians, unscaled.
//
// A body is an index in a table of this file. Its shapes carry the
// filter of the original: a category bit per tag, and a mask without the
// tags that it does not collide with. A body of a tag with triggers also
// has a sensor with the same shape, as `Physics:set_as_*` does.
//
// After each step, the events that start or end a contact, or a sensor
// overlap, are kept in a list, which Atmos reads to deliver them to the
// tasks of the two bodies.

#include <box2d/box2d.h>

static b2WorldId PW_WORLD;
static int       PW_OK = 0;
static double    PW_M  = 32;            // pixels per meter

#define PW_BODIES 4096
static b2BodyId PW_BODY[PW_BODIES];
static int      PW_USED[PW_BODIES];
static int      PW_FREE[PW_BODIES];
static int      PW_FREE_N = 0;
static int      PW_DEAD[PW_BODIES];     // freed in this step: reused after it
static int      PW_DEAD_N = 0;
static int      PW_TOP = 0;

static void pw_world (double meter) {
    if (PW_OK) {
        b2DestroyWorld(PW_WORLD);
    }
    b2WorldDef wd = b2DefaultWorldDef();
    wd.gravity = (b2Vec2) { 0, 0 };
    PW_WORLD = b2CreateWorld(&wd);
    PW_M = meter;
    PW_OK = 1;
    PW_FREE_N = 0;
    PW_DEAD_N = 0;
    PW_TOP = 0;
    memset(PW_USED, 0, sizeof(PW_USED));
}

static void pw_world_free (void) {
    if (PW_OK) {
        b2DestroyWorld(PW_WORLD);
        PW_OK = 0;
    }
}

static int pw_ok (int b) {
    return PW_OK && b>=0 && b<PW_TOP && PW_USED[b];
}

// A body at (x,y): type 0 static, 1 kinematic, 2 dynamic.
static int pw_body (int type, double x, double y) {
    if (!PW_OK) {
        return -1;
    }
    int b;
    if (PW_FREE_N > 0) {
        b = PW_FREE[--PW_FREE_N];
    } else if (PW_TOP < PW_BODIES) {
        b = PW_TOP++;
    } else {
        return -1;
    }
    b2BodyDef bd = b2DefaultBodyDef();
    bd.type = (type == 0) ? b2_staticBody : (type == 1) ? b2_kinematicBody : b2_dynamicBody;
    bd.position = (b2Vec2) { (float)(x/PW_M), (float)(y/PW_M) };
    bd.userData = (void*)(intptr_t)(b + 1);
    PW_BODY[b] = b2CreateBody(PW_WORLD, &bd);
    PW_USED[b] = 1;
    return b;
}

static void pw_body_free (int b) {
    if (!pw_ok(b)) {
        return;
    }
    b2DestroyBody(PW_BODY[b]);
    PW_USED[b] = 0;
    PW_DEAD[PW_DEAD_N++] = b;
}

static int pw_of_shape (b2ShapeId s) {
    if (!b2Shape_IsValid(s)) {
        return -1;
    }
    b2BodyId bd = b2Shape_GetBody(s);
    return (int)(intptr_t) b2Body_GetUserData(bd) - 1;
}

static b2ShapeDef pw_def (uint64_t cat, uint64_t mask, int sensor) {
    b2ShapeDef sd = b2DefaultShapeDef();
    sd.enableContactEvents = true;
    sd.enableSensorEvents  = true;
    if (sensor) {
        // as a fixture of LÖVE with no filter: it senses everything, and
        // it has the default density, so it doubles the mass of the body
        sd.isSensor = true;
    } else {
        sd.filter.categoryBits = cat;
        sd.filter.maskBits = mask;
    }
    return sd;
}

// A shape of the body: a circle of radius r, centered.
static void pw_circle (int b, double r, uint64_t cat, uint64_t mask, int sensor) {
    if (!pw_ok(b)) return;
    b2Circle c = { { 0, 0 }, (float)(r/PW_M) };
    b2ShapeDef sd = pw_def(cat, mask, 0);
    b2CreateCircleShape(PW_BODY[b], &sd, &c);
    if (sensor) {
        sd = pw_def(cat, mask, 1);
        b2CreateCircleShape(PW_BODY[b], &sd, &c);
    }
}

// A shape of the body: a rectangle w x h, centered.
static void pw_rect (int b, double w, double h, uint64_t cat, uint64_t mask, int sensor) {
    if (!pw_ok(b)) return;
    b2Polygon p = b2MakeBox((float)(w/2/PW_M), (float)(h/2/PW_M));
    b2ShapeDef sd = pw_def(cat, mask, 0);
    b2CreatePolygonShape(PW_BODY[b], &sd, &p);
    if (sensor) {
        sd = pw_def(cat, mask, 1);
        b2CreatePolygonShape(PW_BODY[b], &sd, &p);
    }
}

// A shape of the body: a convex polygon of up to 8 points, relative to
// the body, in xs/ys.
static void pw_polygon (int b, int n, double* xs, double* ys, uint64_t cat, uint64_t mask, int sensor) {
    if (!pw_ok(b) || n<3 || n>B2_MAX_POLYGON_VERTICES) return;
    b2Vec2 ps[B2_MAX_POLYGON_VERTICES];
    for (int i=0; i<n; i++) {
        ps[i] = (b2Vec2) { (float)(xs[i]/PW_M), (float)(ys[i]/PW_M) };
    }
    b2Hull hull = b2ComputeHull(ps, n);
    if (hull.count == 0) return;
    b2Polygon p = b2MakePolygon(&hull, 0);
    b2ShapeDef sd = pw_def(cat, mask, 0);
    b2CreatePolygonShape(PW_BODY[b], &sd, &p);
    if (sensor) {
        sd = pw_def(cat, mask, 1);
        b2CreatePolygonShape(PW_BODY[b], &sd, &p);
    }
}

// Applies to every shape of the body.
static void pw_shapes (int b, int what, double v) {
    if (!pw_ok(b)) return;
    b2ShapeId ss[8];
    int n = b2Body_GetShapes(PW_BODY[b], ss, 8);
    for (int i=0; i<n; i++) {
        if (b2Shape_IsSensor(ss[i])) continue;
        if (what == 0) b2Shape_SetRestitution(ss[i], (float)v);
        if (what == 1) b2Shape_SetFriction(ss[i], (float)v);
    }
}

static double pw_get (int b, int what) {
    if (!pw_ok(b)) return 0;
    b2BodyId id = PW_BODY[b];
    switch (what) {
        case 0: return b2Body_GetPosition(id).x * PW_M;
        case 1: return b2Body_GetPosition(id).y * PW_M;
        case 2: return b2Body_GetLinearVelocity(id).x * PW_M;
        case 3: return b2Body_GetLinearVelocity(id).y * PW_M;
        case 4: return b2Rot_GetAngle(b2Body_GetRotation(id));
        case 5: return b2Body_GetAngularVelocity(id);
        case 6: return b2Body_GetMass(id);
    }
    return 0;
}

static void pw_set (int b, int what, double x, double y) {
    if (!pw_ok(b)) return;
    b2BodyId id = PW_BODY[b];
    switch (what) {
        case 0: b2Body_SetTransform(id, (b2Vec2){ (float)(x/PW_M), (float)(y/PW_M) }, b2Body_GetRotation(id)); break;
        case 1: b2Body_SetLinearVelocity(id, (b2Vec2){ (float)(x/PW_M), (float)(y/PW_M) }); break;
        case 2: b2Body_SetTransform(id, b2Body_GetPosition(id), b2MakeRot((float)x)); break;
        case 3: b2Body_SetAngularVelocity(id, (float)x); break;
        case 4: b2Body_SetLinearDamping(id, (float)x); break;
        case 5: b2Body_SetAngularDamping(id, (float)x); break;
        case 6: b2Body_SetFixedRotation(id, x != 0); break;
        case 7: b2Body_SetBullet(id, x != 0); break;
        case 8: b2Body_ApplyForceToCenter(id, (b2Vec2){ (float)(x/PW_M), (float)(y/PW_M) }, true); break;
        case 9: b2Body_ApplyLinearImpulseToCenter(id, (b2Vec2){ (float)(x/PW_M), (float)(y/PW_M) }, true); break;
        case 10: b2Body_ApplyAngularImpulse(id, (float)x, true); break;
        case 11: b2Body_ApplyTorque(id, (float)x, true); break;
    }
}

// The events of the last step: kind 0 contact begin, 1 contact end,
// 2 sensor begin, 3 sensor end (a is the sensor, b the visitor).
#define PW_EVTS 4096
static int PW_EV_N[4];
static int PW_EV_A[4][PW_EVTS];
static int PW_EV_B[4][PW_EVTS];

static void pw_ev (int k, int a, int b) {
    if (a>=0 && b>=0 && PW_EV_N[k] < PW_EVTS) {
        PW_EV_A[k][PW_EV_N[k]] = a;
        PW_EV_B[k][PW_EV_N[k]] = b;
        PW_EV_N[k]++;
    }
}

// A step of the world, as `World:update(dt)` of LÖVE.
static void pw_step (double dt) {
    memset(PW_EV_N, 0, sizeof(PW_EV_N));
    if (!PW_OK) return;
    while (PW_DEAD_N > 0) {
        PW_FREE[PW_FREE_N++] = PW_DEAD[--PW_DEAD_N];
    }
    b2World_Step(PW_WORLD, (float)dt, 4);
    b2ContactEvents ce = b2World_GetContactEvents(PW_WORLD);
    for (int i=0; i<ce.beginCount; i++) {
        pw_ev(0, pw_of_shape(ce.beginEvents[i].shapeIdA), pw_of_shape(ce.beginEvents[i].shapeIdB));
    }
    for (int i=0; i<ce.endCount; i++) {
        pw_ev(1, pw_of_shape(ce.endEvents[i].shapeIdA), pw_of_shape(ce.endEvents[i].shapeIdB));
    }
    b2SensorEvents se = b2World_GetSensorEvents(PW_WORLD);
    for (int i=0; i<se.beginCount; i++) {
        int a = pw_of_shape(se.beginEvents[i].sensorShapeId);
        int b = pw_of_shape(se.beginEvents[i].visitorShapeId);
        if (a != b) pw_ev(2, a, b);
    }
    for (int i=0; i<se.endCount; i++) {
        int a = pw_of_shape(se.endEvents[i].sensorShapeId);
        int b = pw_of_shape(se.endEvents[i].visitorShapeId);
        if (a != b) pw_ev(3, a, b);
    }
}
