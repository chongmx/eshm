// Wire format shared by robot_sim.cpp and policy.py.
//
// Models the shape of a real policy-in-the-loop robot system:
//
//   C++ (robot)                          Python (policy)
//   -----------                          ---------------
//   state    ---- 100 Hz .. 1 kHz ---->   read latest
//   camera N ---- ~30 fps         ---->   read latest
//            <--- 10 .. 30 Hz -------     action chunk
//
// Every stream is "latest value wins", which is exactly what ESHM's channel is.
// The policy does not want every state sample at 1 kHz - it wants the newest
// one at the instant it starts inferring. Samples the policy never read are
// not lost data, they are samples that were already superseded.
//
// Timestamps are CLOCK_MONOTONIC nanoseconds. Python's time.monotonic_ns()
// reads the same clock on Linux, so a stamp written by one side can be
// subtracted from a reading taken by the other, and the difference is real.

#ifndef ESHM_EXAMPLE_ROBOT_LINK_H
#define ESHM_EXAMPLE_ROBOT_LINK_H

#include <eshm.h>
#include <stdint.h>
#include <time.h>

#define ROBOT_JOINTS        7    /* a 7-DoF arm */
#define ACTION_HORIZON     16    /* steps per action chunk */
#define ROBOT_FRAME_MAGIC  0x524643u  /* "RFC" */

/* Robot -> policy. Small and frequent. */
struct RobotState {
    uint64_t seq;                       /* monotonic, so gaps are visible   */
    uint64_t stamp_ns;                  /* when the robot wrote it          */
    double   joint_pos[ROBOT_JOINTS];
    double   joint_vel[ROBOT_JOINTS];
    uint64_t flags;
};

/* Policy -> robot. Infrequent, but carries a whole chunk of future actions.
 *
 * state_stamp_ns is the interesting field: it is copied from the state this
 * chunk was computed from, so the robot can subtract it on arrival and get the
 * true closed-loop latency - write state, read state, infer, write action,
 * read action - without the two processes needing a shared session clock. */
struct ActionChunk {
    uint64_t seq;
    uint64_t stamp_ns;                  /* when the policy wrote the chunk  */
    uint64_t state_seq;                 /* which state it acted on          */
    uint64_t state_stamp_ns;            /* that state's stamp               */
    uint32_t horizon;                   /* steps actually filled            */
    uint32_t pad;
    float    actions[ACTION_HORIZON][ROBOT_JOINTS];
};

/* Camera -> policy. One channel per camera; pixels follow the header. */
struct FrameHeader {
    uint32_t magic;
    uint32_t cam_id;
    uint64_t seq;
    uint64_t stamp_ns;
    uint32_t width;
    uint32_t height;
    uint32_t channels;
    uint32_t bytes;                     /* pixel bytes following this header */
};

static inline uint64_t robot_now_ns(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

#endif /* ESHM_EXAMPLE_ROBOT_LINK_H */
