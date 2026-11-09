// Motion controls: the phone's gyroscope and accelerometer - or a controller's, when it has them - as the Switch
// controller's six-axis sensor (nn::hid::GetSixAxisSensorStates): aiming the bow and the Sheikah Slate's camera, the
// shrines' motion puzzles. Settings > Controls > Motion controls (g_settings.motion) off: the controller lies still.
//
// Frames: SDL gives a sensor as a device held in front of you - x right, y up, z toward you (a phone in its natural
// portrait: turned to the screen here, the game being in landscape). The Switch's six-axis frame is x right, y
// forward, z up (yuzu's mapping of SDL controllers): angular velocity (x, -z, y) in turns per second, acceleration
// -(x, -z, y) in G (at rest it points down). The direction matrix follows the angular velocity, kept level by the
// acceleration (a Mahony filter): its world is z up, with the yaw the sensor started at.
#include <SDL3/SDL.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#ifdef __ANDROID__
#include <jni.h>
#endif

#include "../hle.h"
#include "../settings.h"

typedef struct {
    int64_t delta_time;   // ns since the sample before
    int64_t sampling_number;
    float accel[3];       // G
    float gyro[3];        // turns per second
    float angle[3];       // turns: the angular velocity summed
    float dir[3][3];      // the sensor's x, y and z axes in the world (lying still, the right way up: identity)
    uint32_t attributes;  // 1: connected
    uint32_t reserved;
} SixAxisState;
_Static_assert(sizeof(SixAxisState) == 0x60, "nn::hid::SixAxisSensorState");

#define RING 32
#define KP 0.5f       // the acceleration's pull on the attitude (rad/s per unit of error)
#define TURN 6.2831853f
static pthread_mutex_t m_lock = PTHREAD_MUTEX_INITIALIZER;
static SixAxisState ring[RING];
static int ring_n, ring_head;            // samples kept, the newest's index
static int64_t m_sampling;
static float q[4] = {1, 0, 0, 0};        // w x y z: the sensor's attitude (sensor -> world)
static bool q_set;                       // (levelled by the first acceleration)
static float m_accel[3] = {0, 0, -1}, m_angle[3];
static uint64_t m_gyro_ts, m_live_ns, m_rest_ns;  // the last angular velocity's own time; host times
static uint64_t pad_until_ns;            // a controller's sensors are in use until then: the phone's wait

// q from the way down a (in G): no yaw - the rotation taking a to the world's (0, 0, -1)
static void level_from(const float a[3]) {
    float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (n < 0.5f) return;
    float ux = a[0] / n, uy = a[1] / n, uz = a[2] / n;
    float w = 1 - uz, x = -uy, y = ux;  // (1 + u.v, u x v) with v = (0, 0, -1)
    float m = sqrtf(w * w + x * x + y * y);
    if (m < 1e-4f) q[0] = 0, q[1] = 1, q[2] = 0, q[3] = 0;  // (upside down: half a turn about x)
    else q[0] = w / m, q[1] = x / m, q[2] = y / m, q[3] = 0;
    q_set = true;
}

// One step of the attitude: w (rad/s, the sensor's axes), pulled toward the measured down a
static void fuse(float w[3], const float a[3], float dt) {
    float qw = q[0], x = q[1], y = q[2], z = q[3];
    float n = sqrtf(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]);
    if (n > 0.75f && n < 1.25f) {  // (not while shaken)
        // the world's down seen from the sensor: -(R's third row)
        float vx = -2 * (x * z - qw * y), vy = -2 * (y * z + qw * x), vz = -(1 - 2 * (x * x + y * y));
        float ax = a[0] / n, ay = a[1] / n, az = a[2] / n;
        w[0] += KP * (ay * vz - az * vy);
        w[1] += KP * (az * vx - ax * vz);
        w[2] += KP * (ax * vy - ay * vx);
    }
    q[0] += 0.5f * dt * (-x * w[0] - y * w[1] - z * w[2]);
    q[1] += 0.5f * dt * (qw * w[0] + y * w[2] - z * w[1]);
    q[2] += 0.5f * dt * (qw * w[1] - x * w[2] + z * w[0]);
    q[3] += 0.5f * dt * (qw * w[2] + x * w[1] - y * w[0]);
    float m = sqrtf(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
    for (int i = 0; i < 4; i++) q[i] /= m;
}

// A sample: angular velocity w (rad/s, the Switch's frame) over dt seconds (m_lock held)
static void add_sample(const float w[3], float dt) {
    if (!q_set) level_from(m_accel);
    float wc[3] = {w[0], w[1], w[2]};
    fuse(wc, m_accel, dt);
    ring_head = (ring_head + 1) % RING;
    if (ring_n < RING) ring_n++;
    SixAxisState *s = &ring[ring_head];
    memset(s, 0, sizeof *s);
    s->delta_time = (int64_t)(dt * 1e9f);
    s->sampling_number = ++m_sampling;
    for (int i = 0; i < 3; i++) {
        s->accel[i] = m_accel[i];
        s->gyro[i] = w[i] / TURN;
        m_angle[i] += s->gyro[i] * dt;
        s->angle[i] = m_angle[i];
    }
    float qw = q[0], x = q[1], y = q[2], z = q[3];
    float r[3][3] = {{1 - 2 * (y * y + z * z), 2 * (x * y - qw * z), 2 * (x * z + qw * y)},
                     {2 * (x * y + qw * z), 1 - 2 * (x * x + z * z), 2 * (y * z - qw * x)},
                     {2 * (x * z - qw * y), 2 * (y * z + qw * x), 1 - 2 * (x * x + y * y)}};
    for (int i = 0; i < 3; i++)
        for (int j = 0; j < 3; j++) s->dir[i][j] = r[j][i];  // (row i: the sensor's axis i, a column of R)
    s->attributes = 1;
}

// SDL's frame (rad/s or m/s^2) in: angular velocity -> a sample, acceleration -> the way down
// phone: the phone's own sensors (on the screen as it's turned: to_screen). Held upright they turn as a controller
// lying flat would - the Switch's axes (x, -z, y) of the screen's; with Motion like the Switch's screen (#211, #247:
// "tilting turns the aim, turning tilts it") they are the screen's own, as the console's in handheld mode. Gyroscope
// and accelerometer alike: a rotation, so the way down stays with the turns.
static void feed(SDL_SensorType type, const float d[3], uint64_t ts, bool phone) {
    pthread_mutex_lock(&m_lock);
    bool flat = phone && g_settings.motion_flat;
    static int last_flat = -1;  // (the frame changed in the menu: the attitude is levelled again from the way down)
    if (phone && last_flat != flat) { if (last_flat >= 0) q_set = false; last_flat = flat; }
    if (type == SDL_SENSOR_ACCEL) {
        if (flat) m_accel[0] = -d[0] / 9.80665f, m_accel[1] = -d[1] / 9.80665f, m_accel[2] = -d[2] / 9.80665f;
        else m_accel[0] = -d[0] / 9.80665f, m_accel[1] = d[2] / 9.80665f, m_accel[2] = -d[1] / 9.80665f;
        if (!q_set) level_from(m_accel);
    } else if (type == SDL_SENSOR_GYRO) {
        float dt = m_gyro_ts && ts > m_gyro_ts ? (float)((ts - m_gyro_ts) / 1e9) : 0.005f;
        if (dt > 0.1f) dt = 0.005f;  // (after a pause)
        m_gyro_ts = ts;
        // The gyroscope's bias at rest, learned and taken off: a phone's reads a little turn while it lies still, and the
        // aim drifted (SavageWindRecomp / MacNativeGames: learn it while still). Still: every axis within ~3.4 deg/s of
        // the bias for 1.5 s; then the bias follows slowly. LARECOMP_NO_GYRO_BIAS=1: off.
        static int bias_on = -1;
        static float bias[3];
        static uint64_t still_since;
        if (bias_on < 0) bias_on = getenv("LARECOMP_NO_GYRO_BIAS") == NULL;
        float g[3] = {d[0], d[1], d[2]};
        if (bias_on) {
            float dev = 0;
            for (int i = 0; i < 3; i++) dev = fmaxf(dev, fabsf(d[i] - bias[i]));
            if (dev < 0.06f) {
                if (!still_since) still_since = ts;
                else if (ts - still_since > 1500000000ull) {
                    for (int i = 0; i < 3; i++) bias[i] += (d[i] - bias[i]) * 0.02f;
                    static bool told;
                    if (!told && ts - still_since > 4000000000ull) {
                        told = true;
                        LOG("[motion] gyroscope at rest: bias %.4f %.4f %.4f rad/s taken off\n", bias[0], bias[1], bias[2]);
                    }
                }
            } else {
                still_since = 0;
            }
            for (int i = 0; i < 3; i++) g[i] -= bias[i];
        }
        const float w[3] = {g[0], flat ? g[1] : -g[2], flat ? g[2] : g[1]};
        add_sample(w, dt);
        m_live_ns = SDL_GetTicksNS();
    }
    pthread_mutex_unlock(&m_lock);
}

#ifdef __ANDROID__
// Display.getRotation() of the activity's display, through JNI: 0-3, or -1 if it can't be had
static int android_rotation(void) {
    JNIEnv *env = (JNIEnv *)SDL_GetAndroidJNIEnv();
    if (!env) return -1;
    if ((*env)->PushLocalFrame(env, 8) != 0) {
        (*env)->ExceptionClear(env);
        return -1;
    }
    int r = -1;
    jobject act = (jobject)SDL_GetAndroidActivity();  // (a local reference: the frame's pop releases it)
    jobject wm = NULL, disp = NULL;
    jmethodID m = NULL;
    if (act) m = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, act), "getWindowManager", "()Landroid/view/WindowManager;");
    if (m && !(*env)->ExceptionCheck(env)) wm = (*env)->CallObjectMethod(env, act, m);
    m = NULL;
    if (wm && !(*env)->ExceptionCheck(env)) m = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, wm), "getDefaultDisplay", "()Landroid/view/Display;");
    if (m && !(*env)->ExceptionCheck(env)) disp = (*env)->CallObjectMethod(env, wm, m);
    m = NULL;
    if (disp && !(*env)->ExceptionCheck(env)) m = (*env)->GetMethodID(env, (*env)->GetObjectClass(env, disp), "getRotation", "()I");
    if (m && !(*env)->ExceptionCheck(env)) r = (*env)->CallIntMethod(env, disp, m);
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env), r = -1;
    (*env)->PopLocalFrame(env, NULL);
    return r >= 0 && r <= 3 ? r : -1;
}
#endif

// The screen's turn from the display's natural orientation, in quarter turns counterclockwise (Display.getRotation()):
// the turn that takes the sensors' axes - which follow the natural orientation - onto the screen's. Not SDL's display
// orientation: SDL counts a quarter turn more on a display whose natural orientation is landscape (SDL_android.c
// onNativeRotationChanged), and takes that natural orientation once, in SDLActivity.onCreate - the activity lives
// through folding and unfolding (configChanges). On a foldable's inner screen, landscape by nature (Galaxy Z Fold 8,
// Pixel Fold), rotation 0 read as LANDSCAPE and turned the motion a quarter too far: turning the phone tilted the aim.
// Asked of Android at most every 250 ms (folding changes it with no event here); SDL's orientation only without JNI.
static int screen_turns(void) {
    static int turns = -1;
    static uint64_t asked_ns;
    uint64_t now = SDL_GetTicksNS();
    if (turns >= 0 && now - asked_ns < 250000000ull) return turns;
    asked_ns = now;
    int r = -1;
#ifdef __ANDROID__
    r = android_rotation();
#endif
    if (r < 0) {
        SDL_DisplayID d = SDL_GetPrimaryDisplay();
        SDL_DisplayOrientation o = SDL_GetCurrentDisplayOrientation(d);
        r = o == SDL_ORIENTATION_LANDSCAPE ? 1 : o == SDL_ORIENTATION_PORTRAIT_FLIPPED ? 2 : o == SDL_ORIENTATION_LANDSCAPE_FLIPPED ? 3 : 0;
        if (SDL_GetNaturalDisplayOrientation(d) == SDL_ORIENTATION_LANDSCAPE) r = (r + 3) % 4;
    }
    if (r != turns) LOG("[motion] the screen is turned %d degrees from its natural orientation\n", r * 90);
    turns = r;
    return turns;
}

// The phone's sensors (device axes, as the display's natural orientation) on the screen as it's turned now
static void to_screen(const float d[3], float s[3]) {
    switch (screen_turns()) {
    case 1: s[0] = -d[1], s[1] = d[0]; break;   // (a quarter counterclockwise: a portrait phone in landscape, its right side up)
    case 3: s[0] = d[1], s[1] = -d[0]; break;   // (a quarter clockwise: its left side up)
    case 2: s[0] = -d[0], s[1] = -d[1]; break;
    default: s[0] = d[0], s[1] = d[1]; break;   // (as by nature: a foldable's landscape inner screen held normally too)
    }
    s[2] = d[2];
}

// (the event thread: runtime/hle/input.c) The phone's gyroscope and accelerometer, once the setting is on
void motion_open(void) {
    static bool opened;
    if (opened || !g_settings.motion) return;
    opened = true;
    if (!SDL_InitSubSystem(SDL_INIT_SENSOR)) { LOG("[motion] sensors: %s\n", SDL_GetError()); return; }
    int n = 0;
    SDL_SensorID *ids = SDL_GetSensors(&n);
    bool gyro = false, accel = false;
    for (int i = 0; i < n; i++) {
        SDL_SensorType t = SDL_GetSensorTypeForID(ids[i]);
        if ((t == SDL_SENSOR_GYRO && !gyro) || (t == SDL_SENSOR_ACCEL && !accel)) {
            if (!SDL_OpenSensor(ids[i])) continue;
            if (t == SDL_SENSOR_GYRO) gyro = true;
            else accel = true;
            LOG("[motion] %s: %s\n", t == SDL_SENSOR_GYRO ? "gyroscope" : "accelerometer", SDL_GetSensorNameForID(ids[i]));
        }
    }
    SDL_free(ids);
    if (!gyro) LOG("[motion] no gyroscope among %d sensors: motion controls only from a controller that has one\n", n);
}

// A controller that has a gyroscope: its own sensors, in place of the phone's while it sends them
void motion_pad_opened(SDL_Gamepad *g) {
    if (!SDL_GamepadHasSensor(g, SDL_SENSOR_GYRO)) return;
    // (Motion sensor: Phone - the controller's left off: a USB-C controller the phone sits in, its sensors on, and the
    // phone's aim stopped after a few shots until the controller was plugged in again, #78)
    if (g_settings.motion_source == 1) { LOG("[motion] %s: its gyroscope left off (Phone)\n", SDL_GetGamepadName(g)); return; }
    SDL_SetGamepadSensorEnabled(g, SDL_SENSOR_GYRO, true);
    if (SDL_GamepadHasSensor(g, SDL_SENSOR_ACCEL)) SDL_SetGamepadSensorEnabled(g, SDL_SENSOR_ACCEL, true);
    LOG("[motion] %s: its gyroscope\n", SDL_GetGamepadName(g));
}

void motion_event(const SDL_Event *e) {
    if (!g_settings.motion || ENV("LARECOMP_MOTION_TEST")) return;
    // Settings > Controls > Motion sensor (settings.h motion_source): 1 the phone's alone - for a controller that reports
    // a gyroscope it hasn't got, whose noise passed for a live one and muted the phone's (GameSir X2 Type-C, #115); 2 a
    // controller's alone, taken as it comes; 0 the automatic switch below.
    int source = g_settings.motion_source;
    if (e->type == SDL_EVENT_SENSOR_UPDATE) {
        if (source == 2 || (source == 0 && SDL_GetTicksNS() < pad_until_ns)) return;
        float s[3];
        SDL_SensorType type = SDL_GetSensorTypeForID(e->sensor.which);
        to_screen(e->sensor.data, s);
        {   // Turning the phone round (landscape to the other landscape): the screen's axes flip part way, when Android
            // turns the game, and what the sensors read on the way (the hand's tilt, the way down sweeping round) went into
            // the attitude in one frame before and the other after - the aim further up with each turn. Held against the
            // screen, the phone is as it was before the half turn: while it turns fast about the screen's normal (over ~115
            // deg/s) and for 0.4 s after the screen turned, its readings are left out - the attitude kept as it was.
            static uint64_t quiet_until;
            static int last_turns = -1;
            uint64_t now = SDL_GetTicksNS();
            int t = screen_turns();
            if (last_turns >= 0 && t != last_turns) quiet_until = now + 400000000ull;
            last_turns = t;
            if (type == SDL_SENSOR_GYRO && fabsf(s[2]) > 2.0f) quiet_until = now + 250000000ull;
            if (now < quiet_until) return;
        }
        feed(type, s, e->sensor.sensor_timestamp ? e->sensor.sensor_timestamp : SDL_GetTicksNS(), true);
    } else if (e->type == SDL_EVENT_GAMEPAD_SENSOR_UPDATE) {
        if (source == 1) return;
        if (source == 2) {
            feed((SDL_SensorType)e->gsensor.sensor, e->gsensor.data,
                 e->gsensor.sensor_timestamp ? e->gsensor.sensor_timestamp : SDL_GetTicksNS(), false);
            return;
        }
        // A controller's sensors replace the phone's only once its gyroscope is alive: some report one that never is (a
        // USB-C controller the phone sits in, GameSir X5 Lite: issue #78), and its zeros held the phone's off. Alive is
        // readings that change from one to the next - a real gyroscope's noise does even lying still - 20 in a row:
        // one stray reading switched to that controller for good (0.2.3), and a speed threshold (0.2.4) never let a
        // slow aim or a resting controller take over (#78, #115). Back to the phone's after 2 s of readings that are
        // zero or repeat: a dead or frozen sensor. And a turn first: some controllers with no gyroscope send noise that
        // changes (GameSir X2 Type-C, #115, #137) - once a reading of 0.5 rad/s (~30 deg/s: a hand turning it, more
        // than any noise) has come, that controller's changing readings are a gyroscope.
        static bool pad_live, pad_turned;
        static int changing;
        static float last[3];
        static uint64_t dead_ns;
        if ((SDL_SensorType)e->gsensor.sensor == SDL_SENSOR_GYRO) {
            const float *g = e->gsensor.data;
            bool alive = (g[0] != 0.0f || g[1] != 0.0f || g[2] != 0.0f) &&
                         (g[0] != last[0] || g[1] != last[1] || g[2] != last[2]);
            memcpy(last, g, sizeof last);
            uint64_t now = SDL_GetTicksNS();
            if (!pad_turned && g[0] * g[0] + g[1] * g[1] + g[2] * g[2] > 0.25f) {
                pad_turned = true;
                LOG("[motion] the controller's gyroscope turned (%.2f %.2f %.2f rad/s)\n", g[0], g[1], g[2]);
            }
            if (!pad_live) {
                changing = alive ? changing + 1 : 0;
                if (changing >= 20 && pad_turned) {
                    pad_live = true, dead_ns = 0;
                    LOG("[motion] the controller's gyroscope is alive: its sensors in place of the phone's\n");
                }
            } else if (alive) dead_ns = 0;
            else if (!dead_ns) dead_ns = now;
            else if (now - dead_ns > 2000000000) {
                pad_live = false, changing = 0, pad_until_ns = 0;
                LOG("[motion] the controller's gyroscope reads zero or the same: the phone's sensors again\n");
            }
        }
        if (!pad_live) return;
        pad_until_ns = SDL_GetTicksNS() + 500000000;
        feed((SDL_SensorType)e->gsensor.sensor, e->gsensor.data,
             e->gsensor.sensor_timestamp ? e->gsensor.sensor_timestamp : SDL_GetTicksNS(), false);
    }
}

// LARECOMP_MOTION_TEST=gx,gy,gz[:from:frames]: that angular velocity (turns per second, the Switch's frame) in place
// of the sensors', from that presented frame on for that many (all along without them)
static bool test_motion(float w[3]) {
    const char *e = ENV("LARECOMP_MOTION_TEST");
    if (!e) return false;
    float g[3] = {0};
    unsigned long from = 0, len = ~0ul;
    if (sscanf(e, "%f,%f,%f:%lu:%lu", &g[0], &g[1], &g[2], &from, &len) < 3) return false;
    extern uint64_t renderer_present_count(void);
    uint64_t f = renderer_present_count();
    for (int i = 0; i < 3; i++) w[i] = f >= from && f - from < len ? g[i] * TURN : 0;
    return true;
}

// A read with nothing new from the sensors (none, the setting off, or a test): one sample of its own
static void own_sample(uint64_t now) {
    float dt = m_rest_ns && now > m_rest_ns ? (float)((now - m_rest_ns) / 1e9) : 1 / 30.0f;
    if (dt > 0.1f) dt = 1 / 30.0f;
    m_rest_ns = now;
    float w[3] = {0, 0, 0};
    if (!test_motion(w) && !g_settings.motion) {  // (off: lying still, the right way up)
        q[0] = 1, q[1] = q[2] = q[3] = 0, q_set = true;
        m_accel[0] = m_accel[1] = 0, m_accel[2] = -1;
    }
    add_sample(w, dt);
}

// int GetSixAxisSensorHandles(SixAxisSensorHandle *out, int count, const NpadIdType &, NpadStyleSet): one sensor, two
// for a pair of Joy-Con (both read the same here)
HLE_FUNC("_ZN2nn3hid23GetSixAxisSensorHandlesEPNS0_19SixAxisSensorHandleEiRKjNS_4util10BitFlagSetILi32ENS0_12NpadStyleTagEEE",
         hid_sixaxis_handles) {
    uint32_t *out = (uint32_t *)(uintptr_t)c->x[0];
    int n = (int)(int32_t)c->x[1];
    uint32_t id = *(uint32_t *)(uintptr_t)c->x[2], style = (uint32_t)c->x[3];
    int k = style & 4 ? 2 : 1;  // (JoyDual: left and right)
    if (n > k) n = k;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) out[i] = (uint32_t)i << 16 | (id & 0xFF) << 8 | 3;  // device, player, FullKey
    static int nlog;
    if (nlog++ < 4) LOG("[motion] sensor handles: player %#x style %#x -> %d\n", id, style, n);
    c->x[0] = (uint64_t)n;
}

HLE_FUNC("_ZN2nn3hid18StartSixAxisSensorERKNS0_19SixAxisSensorHandleE", hid_sixaxis_start) {
    static int nlog;
    if (nlog++ < 4) LOG("[motion] sensor %#x started\n", *(uint32_t *)(uintptr_t)c->x[0]);
}

HLE_FUNC("_ZN2nn3hid25SetGyroscopeZeroDriftModeERKNS0_19SixAxisSensorHandleERKNS0_22GyroscopeZeroDriftModeE",
         hid_sixaxis_drift) { (void)c; }

// int GetSixAxisSensorStates(SixAxisSensorState *out, int count, const SixAxisSensorHandle &): the newest first
HLE_FUNC("_ZN2nn3hid22GetSixAxisSensorStatesEPNS0_18SixAxisSensorStateEiRKNS0_19SixAxisSensorHandleE", hid_sixaxis_states) {
    SixAxisState *out = (SixAxisState *)(uintptr_t)c->x[0];
    int n = (int)(int32_t)c->x[1];
    uint64_t now = SDL_GetTicksNS();
    pthread_mutex_lock(&m_lock);
    if (!g_settings.motion || ENV("LARECOMP_MOTION_TEST") || now - m_live_ns > 250000000) own_sample(now);
    {   // (the log: a sensor that stops sending while the game reads it - aiming that stops working, #78)
        static bool quiet;
        static int nlog;
        bool q = g_settings.motion && m_live_ns && now - m_live_ns > 1000000000;
        if (q != quiet && nlog < 40) {
            nlog++;
            LOG(q ? "[motion] no gyroscope readings for 1 s while the game reads the sensor (%s)\n"
                  : "[motion] gyroscope readings again (%s)\n",
                g_settings.motion_source == 1 ? "Phone" : g_settings.motion_source == 2 ? "Controller" : "Auto");
        }
        quiet = q;
    }
    if (n > ring_n) n = ring_n;
    if (n < 0) n = 0;
    for (int i = 0; i < n; i++) out[i] = ring[(ring_head - i + RING) % RING];
    {   // TEMP DIAG (TOTK 1.4.3 gyro aim): sead's controllers and their motion addons, LARECOMP_LOG_MOTION=2
        static uint64_t dlog, calls;
        static int watched;
        calls++;
        const char *lm = ENV("LARECOMP_LOG_MOTION");
        if (lm && lm[0] == '2' && now - dlog > 1000000000) {
            dlog = now;
            uint64_t mgr = *(uint64_t *)(uintptr_t)(MAIN_BASE + 0x3accdb8);
            int nc = mgr ? *(int32_t *)(uintptr_t)(mgr + 0x178) : 0, found = 0;
            LOG("[motdiag] calls %llu lr %llx out %llx mgr %llx controllers %d\n", (unsigned long long)calls,
                (unsigned long long)c->x[30], (unsigned long long)c->x[0], (unsigned long long)mgr, nc);
            for (int i = 0; i < nc && i < 16; i++) {
                uint64_t ct = ((uint64_t *)(uintptr_t)*(uint64_t *)(uintptr_t)(mgr + 0x180))[i];
                if (!ct) continue;
                const uint8_t *cb = (const uint8_t *)(uintptr_t)ct;
                LOG("[motdiag]  ctl%d %llx vt +%llx npad %d flags %#x style %d btn %#x sticks %.3f %.3f %.3f %.3f\n", i,
                    (unsigned long long)ct, (unsigned long long)(*(uint64_t *)cb - MAIN_BASE), *(int32_t *)(cb + 0x178),
                    cb[0x17c], cb[0x17d], *(uint32_t *)(cb + 0x114), *(float *)(cb + 0x120), *(float *)(cb + 0x124),
                    *(float *)(cb + 0x128), *(float *)(cb + 0x12c));
                uint64_t head = ct + 0x148, node = *(uint64_t *)(cb + 0x150);
                int64_t off = *(int32_t *)(cb + 0x15c);
                for (int k = 0; node && node != head && k < 16; k++, node = *(uint64_t *)(uintptr_t)(node + 8)) {
                    uint64_t ad = node - off;
                    const uint8_t *ab = (const uint8_t *)(uintptr_t)ad;
                    uint64_t vt = *(uint64_t *)ab - MAIN_BASE;
                    if (vt != 0x394c1d8) { LOG("[motdiag]   addon %llx vt +%llx\n", (unsigned long long)ad, (unsigned long long)vt); continue; }
                    const float *s0 = (const float *)(ab + 0x28);
                    LOG("[motdiag]   motion %llx owner %llx count %d last %lld b4..7 %d %d %d %d gyro %.3f %.3f %.3f dir0 %.2f %.2f %.2f\n",
                        (unsigned long long)ad, (unsigned long long)*(uint64_t *)(ab + 0x20), *(int32_t *)(ab + 0x4b0),
                        (long long)*(int64_t *)(ab + 0x4a8), ab[0x4b4], ab[0x4b5], ab[0x4b6], ab[0x4b7], s0[3], s0[4], s0[5],
                        s0[9], s0[10], s0[11]);
                    if (!watched && ENV("LARECOMP_MOTDIAG_WATCH")) {
                        extern void rt_range_watch(uint64_t lo, uint64_t hi);
                        rt_range_watch(ad + 0x28, ad + 0x4b4);
                        found = 1;
                    }
                }
            }
            if (found) watched = 1;
            // the game's PlayerCameraControllerInput nodes (vtable main+0x38c1988): found once by a scan, then logged
            static uint64_t nodes[8], preupd;
            static int nn = -1;
            if (nn < 0 && calls > 6000) {
                extern int kernel_data_regions(uint64_t *addr, uint64_t *size, int max);
                static uint64_t ra[16384], rs[16384]; uint64_t want = MAIN_BASE + 0x38c1988;
                // also counted: the PreUpdate node (0x37935f0), the motion addon (0x394c1d8, known to exist), and
                // pointers to the player controller ctl8 (mgr's controller 8)
                uint64_t other[3] = {MAIN_BASE + 0x37935f0, MAIN_BASE + 0x394c1d8,
                                     ((uint64_t *)(uintptr_t)*(uint64_t *)(uintptr_t)(mgr + 0x180))[8]};
                int cnt[3] = {0};
                uint64_t first[3] = {0};
                int nr = kernel_data_regions(ra, rs, 16384);
                nn = 0;
                for (int r = 0; r < nr; r++) {
                    if (rs[r] >= 0x1000000) LOG("[motdiag] region %llx +%llx\n", (unsigned long long)ra[r], (unsigned long long)rs[r]);
                    for (uint64_t p = ra[r]; p + 0x88 <= ra[r] + rs[r]; p += 8) {
                        uint64_t v = *(uint64_t *)(uintptr_t)p;
                        if (v == want && nn < 8) nodes[nn++] = p;
                        for (int k = 0; k < 3; k++)
                            if (v == other[k]) { if (!cnt[k]++) first[k] = p; if (k == 2 && cnt[k] <= 12) LOG("[motdiag] ctl8 ref at %llx\n", (unsigned long long)p); }
                    }
                }
                LOG("[motdiag] %d camera input nodes in %d regions; preupdate %d (%llx) addons %d (%llx) ctl8 refs %d\n", nn, nr,
                    cnt[0], (unsigned long long)first[0], cnt[1], (unsigned long long)first[1], cnt[2]);
                preupd = first[0];
            }
            if (preupd) {
                uint64_t blk = *(uint64_t *)(uintptr_t)(preupd + 0x20);
                if (blk) {
                    const float *b = (const float *)(uintptr_t)blk;
                    LOG("[motdiag] preupdate %llx input %llx rstick %.2f %.2f lstick %.2f %.2f count %d gyro %.3f %.3f %.3f\n",
                        (unsigned long long)preupd, (unsigned long long)blk, b[0], b[1], b[2], b[3], ((const int32_t *)b)[6],
                        b[7], b[8], b[9]);
                } else LOG("[motdiag] preupdate %llx: no input\n", (unsigned long long)preupd);
                // the player camera ([[[[[preupd+8]+8]+0x1f8]+0x228]+0x3b0], ExecutePlayerCameraPreUpdate 0x8749b0):
                // +0x34 bits 2.. = IsUseGyro, ?, ?, reverse X, reverse Y; its gyro sample at +0x8c0 (0x87509c/0x874e6c)
                uint64_t p = *(uint64_t *)(uintptr_t)(preupd + 8);
                if (p) p = *(uint64_t *)(uintptr_t)(p + 8);
                if (p) p = *(uint64_t *)(uintptr_t)(p + 0x1f8);
                if (p) p = *(uint64_t *)(uintptr_t)(p + 0x228);
                if (p) p = *(uint64_t *)(uintptr_t)(p + 0x3b0);
                if (p) {
                    const uint8_t *cm = (const uint8_t *)(uintptr_t)p;
                    const float *gs = (const float *)(cm + 0x8c8);
                    LOG("[motdiag] camera %llx flags34 %#x flags8a8 %#x gyro %.3f %.3f %.3f valid %d invalid %d\n",
                        (unsigned long long)p, *(uint32_t *)(cm + 0x34), *(uint32_t *)(cm + 0x8a8), gs[0], gs[1], gs[2],
                        cm[0x940], cm[0x955]);
                }
            }
            for (int i = 0; i < nn; i++) {
                const uint8_t *nb = (const uint8_t *)(uintptr_t)nodes[i];
                uint64_t np = *(uint64_t *)(nb + 0x70) & ~3ull;
                const char *name = np ? *(const char **)(uintptr_t)np : NULL;
                const float *g = (const float *)(nb + 0x3c);
                LOG("[motdiag] node %llx name %.40s out %llx rstick %.2f %.2f btn %#x count %d gyro %.3f %.3f %.3f dir0 %.2f %.2f %.2f\n",
                    (unsigned long long)nodes[i], name ? name : "-", (unsigned long long)*(uint64_t *)(nb + 0x80),
                    *(float *)(nb + 0x20), *(float *)(nb + 0x24), *(uint32_t *)(nb + 0x30), *(int32_t *)(nb + 0x38), g[0],
                    g[1], g[2], g[3], g[4], g[5]);
            }
        }
    }
    static uint64_t logged;
    if (ENV("LARECOMP_LOG_MOTION") && now - logged > 1000000000) {  // (once a second: the newest sample)
        logged = now;
        const SixAxisState *s = &ring[ring_head];
        LOG("[motion] #%lld dt %.1f ms accel %.2f %.2f %.2f gyro %.3f %.3f %.3f angle %.3f %.3f %.3f y-axis %.2f %.2f %.2f "
            "(%d of %d)\n", (long long)s->sampling_number, s->delta_time / 1e6, s->accel[0], s->accel[1], s->accel[2],
            s->gyro[0], s->gyro[1], s->gyro[2], s->angle[0], s->angle[1], s->angle[2], s->dir[1][0], s->dir[1][1],
            s->dir[1][2], n, (int)(int32_t)c->x[1]);
    }
    pthread_mutex_unlock(&m_lock);
    c->x[0] = (uint64_t)n;
}

RT_ON_RESET(motion_reset) {
    pthread_mutex_lock(&m_lock);
    ring_n = 0, q_set = false, m_rest_ns = 0;
    memset(m_angle, 0, sizeof m_angle);
    pthread_mutex_unlock(&m_lock);
}
