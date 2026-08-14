/* DSP-1 HLE — see dsp1_hle.h for provenance. Command set and word counts from
 * public documentation (nesdev/sneslab); arithmetic implemented fresh from the
 * operations' definitions (Q15 fixed point, angles with 0x10000 = full turn).
 *
 * Precision stance: the real chip computes in 16-bit fixed point with its own
 * rounding. This HLE computes in double and rounds once, which is at least as
 * accurate; games tolerate this (they feed the results to the PPU or compare
 * magnitudes). If a title ever proves sensitive, tune per command.
 *
 * The projection group (Parameter/Raster/Project/Target) implements a single
 * coherent pinhole-over-plane model; the constants are geometric, not copied.
 * Good enough to drive Mode-7 sensibly; refine against hardware captures if a
 * game's horizon sits visibly wrong. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "dsp1_hle.h"
#include "snes_gnw_alloc.h"

#define DSP1_SAVE_VERSION 1

static const double TAU = 6.283185307179586;

static double angle(uint16_t a) { return (double)a * (TAU / 65536.0); }

static int16_t clamp16(double v) {
  if (v > 32767.0) return 32767;
  if (v < -32768.0) return -32768;
  return (int16_t)lround(v);
}

void dsp1_reset(Dsp1* d) {
  memset(d, 0, sizeof(*d));
  d->version = DSP1_SAVE_VERSION;
  d->lfe = 0x0100;               /* sane non-zero defaults until Parameter runs */
  d->fz = 0x0100;
}

uint8_t dsp1_readSR(Dsp1* d) {
  /* bit7 RQM: always ready (commands execute instantly).
   * bit4 DRS: a 16-bit transfer's second half is pending — games use this to
   * resync the byte stream after interrupts (Mario Kart checks it). */
  return 0x80 | ((d->byteIdx & 1) ? 0x10 : 0);
}

/* ---- command implementations ------------------------------------------- */

static void attitude(Dsp1* d, int slot) {
  /* in: m, Az, Ay, Ax -> build scaled rotation matrix (coordinate transform) */
  double m = (double)d->in[0] / 32768.0;
  double az = angle((uint16_t)d->in[1]);
  double ay = angle((uint16_t)d->in[2]);
  double ax = angle((uint16_t)d->in[3]);
  double cz = cos(az), sz = sin(az);
  double cy = cos(ay), sy = sin(ay);
  double cx = cos(ax), sx = sin(ax);
  /* R = Rx * Ry * Rz applied to row vectors: world -> object axes */
  double r[3][3] = {
    { cy * cz,                cy * sz,               -sy      },
    { sx * sy * cz - cx * sz, sx * sy * sz + cx * cz, sx * cy },
    { cx * sy * cz + sx * sz, cx * sy * sz - sx * cz, cx * cy },
  };
  for (int i = 0; i < 3; i++)
    for (int j = 0; j < 3; j++)
      d->matrix[slot][i][j] = clamp16(r[i][j] * m * 32768.0);
}

static void objective(Dsp1* d, int slot) {
  /* world (X,Y,Z) -> object frame (F,L,U): rows of M dot v */
  for (int i = 0; i < 3; i++) {
    double acc = 0;
    for (int j = 0; j < 3; j++)
      acc += (double)d->matrix[slot][i][j] * d->in[j];
    d->out[i] = clamp16(acc / 32768.0);
  }
}

static void subjective(Dsp1* d, int slot) {
  /* object frame -> world: transpose (rotation matrices: inverse = transpose) */
  for (int i = 0; i < 3; i++) {
    double acc = 0;
    for (int j = 0; j < 3; j++)
      acc += (double)d->matrix[slot][j][i] * d->in[j];
    d->out[i] = clamp16(acc / 32768.0);
  }
}

static void scalar(Dsp1* d, int slot) {
  /* forward-axis component of (X,Y,Z) in the object frame */
  double acc = 0;
  for (int j = 0; j < 3; j++)
    acc += (double)d->matrix[slot][0][j] * d->in[j];
  d->out[0] = clamp16(acc / 32768.0);
}

/* ---- projection group: Parameter / Raster / Project / Target -------------
 *
 * The real chip's projection pipeline (documented behavior, validated
 * numerically against the decapped chip's fixed-point algorithm used as a
 * LOCAL test oracle — no emulator code shipped):
 *
 *   Aas = azimuth (heading), Azs = zenith (downward view tilt), angles with
 *   0x10000 = full turn. Camera at (Fx,Fy,Fz); Lfe = eye->screen distance,
 *   Les = eye->ground reference distance.
 *
 *   Parameter: VPlane = Fz + Lfe*cos(Azs)  — the view-plane height term.
 *     NOTE Fz alone can be 0 (Mario Kart drives with Fz=0!) — the Lfe*cos
 *     term is what keeps the projection alive; the old pinhole model here
 *     used fz/tan(ray) and collapsed to a flat single-texel ground.
 *     The zenith angle is CLIPPED to a VPlane-magnitude-dependent maximum
 *     (~80 deg, MaxAZS table = chip data-ROM facts) before deriving
 *     VOffset = Les*cos(AZS) and Vva = -Les*cos(AZS)/sin(AZS) (the raster
 *     line of the horizon, which the game feeds back as the raster stream's
 *     STARTING Vs — negative!).
 *
 *   Raster(Vs): Vs is SIGNED. scale(Vs) = VPlane / (Vs*sin(Azs) + VOffset);
 *     A =  256*scale*cos(Aas)          C = 256*scale*sin(Aas)
 *     B = -256*(scale/cos(AZS))*sin(Aas)  D = 256*(scale/cos(AZS))*cos(Aas)
 *     (256 = the PPU's 8.8 fixed-point unit for $211B-E.)
 *
 *   Project(X,Y,Z): translate by -F, rotate by (pi - Aas) around Z, then by
 *     -Azs around X, push the view plane out by Lfe; behind-plane objects
 *     project H = -X'*Les/|Z'|, V = -Y'*Les/|Z'|, M = 256*Les/|Z'| (clamped
 *     to 0xFFFF); at/into the plane the chip pins H=0, V=224, M=0xFFFF. */

/* Derived trig state, deliberately NOT stored in Dsp1: the struct is a raw
 * savestate blob, so growing it would break every existing save. The cache
 * keys on the seven raw parameters; a savestate load simply misses once and
 * recomputes. Also the perf story: trig runs once per Parameter (per frame),
 * not once per scanline — raster_line itself is one divide + four multiplies. */
static struct {
  int valid;
  int16_t fx, fy, fz, lfe, les;
  uint16_t aas, azs;
  double sinAas, cosAas, sinAzs, cosAzs;
  double vplane;               /* Fz + Lfe*cos(Azs) */
  double sinAZS, cosAZS;       /* clipped zenith */
  double voffset;              /* Les*cos(AZS) */
  int16_t azs_clipped;         /* clipped zenith in angle units (for Vof path) */
} pj;

/* Maximum zenith angle by view-plane magnitude (chip data-ROM table).
 * Indexed by the normalization shift count of |VPlane| as an int16:
 * idx = 14 - floor(log2(|vplane|)), clamped to [0,15]. */
static const int16_t dsp1_maxazs[16] = {
  0x38b4, 0x38b7, 0x38ba, 0x38be, 0x38c0, 0x38c4, 0x38c7, 0x38ca,
  0x38ce, 0x38d0, 0x38d4, 0x38d7, 0x38da, 0x38dd, 0x38e0, 0x38e4
};

static void proj_update(Dsp1* d) {
  if (pj.valid && pj.fx == d->fx && pj.fy == d->fy && pj.fz == d->fz &&
      pj.lfe == d->lfe && pj.les == d->les && pj.aas == d->aas &&
      pj.azs == d->azs)
    return;
  pj.fx = d->fx; pj.fy = d->fy; pj.fz = d->fz;
  pj.lfe = d->lfe; pj.les = d->les; pj.aas = d->aas; pj.azs = d->azs;

  double aas = angle(d->aas), azs = angle(d->azs);
  pj.sinAas = sin(aas); pj.cosAas = cos(aas);
  pj.sinAzs = sin(azs); pj.cosAzs = cos(azs);
  pj.vplane = (double)d->fz + (double)d->lfe * pj.cosAzs;

  /* zenith clip: max angle depends on |VPlane|'s binary magnitude */
  double av = pj.vplane < 0 ? -pj.vplane : pj.vplane;
  int idx;
  if (av < 1.0) idx = 15;
  else {
    int lg = 0;
    while ((1 << (lg + 1)) <= (int)av && lg < 14) lg++;
    idx = 14 - lg;
    if (idx < 0) idx = 0;
    if (idx > 15) idx = 15;
  }
  int16_t maxazs = dsp1_maxazs[idx];
  int16_t azs_s = (int16_t)d->azs;
  if (azs_s < 0) {
    if (azs_s < (int16_t)(-maxazs + 1)) azs_s = (int16_t)(-maxazs + 1);
  } else {
    if (azs_s > maxazs) azs_s = maxazs;
  }
  pj.azs_clipped = azs_s;
  double azsc = angle((uint16_t)azs_s);
  pj.sinAZS = sin(azsc);
  pj.cosAZS = cos(azsc);
  if (pj.cosAZS < 1e-6 && pj.cosAZS > -1e-6) pj.cosAZS = 1e-6;
  if (pj.sinAZS < 1e-6 && pj.sinAZS > -1e-6) pj.sinAZS = 1e-6;
  pj.voffset = (double)d->les * pj.cosAZS;
}

static void cmd_parameter(Dsp1* d) {
  d->fx = d->in[0]; d->fy = d->in[1]; d->fz = d->in[2];
  d->lfe = d->in[3]; d->les = d->in[4];
  d->aas = (uint16_t)d->in[5]; d->azs = (uint16_t)d->in[6];
  pj.valid = 0;
  proj_update(d);

  /* projection centre on the ground plane */
  double t = pj.vplane / pj.cosAZS;
  if (t > 32767.0) t = 32767.0;
  if (t < -32767.0) t = -32767.0;             /* chip saturates this term */
  double c = t * pj.sinAZS;
  d->centerX = clamp16((double)d->fx + (double)d->lfe * (-pj.sinAzs * pj.sinAas)
                       + c * pj.sinAas);
  d->centerY = clamp16((double)d->fy + (double)d->lfe * ( pj.sinAzs * pj.cosAas)
                       - c * pj.cosAas);

  /* Vof: 0 unless the zenith angle was clipped; the clipped-branch quadratic
   * correction (chip data-ROM constants 0x14ac/0x6488/0x0a26/0x277a as Q15:
   * 0.1615, pi/4, 0.0793, 0.3084) nudges Vof and cos(AZS) near the limit. */
  double vof = 0.0;
  int16_t azs_s = (int16_t)d->azs;
  if (azs_s != pj.azs_clipped ||
      azs_s == (azs_s < 0 ? (int16_t)-dsp1_maxazs[0] : dsp1_maxazs[0])) {
    double cq = (double)(azs_s - pj.azs_clipped);
    if (cq >= 0.0) cq -= 1.0;
    double aux = (-(cq * 4.0) - 1.0) / 32768.0;   /* ~(C<<2) as Q15 */
    double c2 = aux * 0.161499;
    c2 = c2 * aux + 0.785400;
    vof -= (c2 * aux) * (double)d->les;
    double c3 = aux * aux;
    double aux2 = c3 * 0.079285 + 0.308411;
    pj.cosAZS += (c3 * aux2) * pj.cosAZS;
    pj.voffset = (double)d->les * pj.cosAZS;
  }
  d->vof = clamp16(vof);
  d->vva = clamp16(floor(-pj.voffset / pj.sinAZS));

  d->out[0] = d->vof; d->out[1] = d->vva;
  d->out[2] = d->centerX; d->out[3] = d->centerY;
}

static void raster_line(Dsp1* d, int16_t vs) {
  /* Per-scanline Mode-7 matrix ($211B-E, 8.8 fixed point). Vs is SIGNED:
   * Mario Kart starts the stream at Vs = Vva (about -73) and walks down
   * through zero into the visible ground. The old code took uint16_t here,
   * turned -73 into 65463, and computed garbage for every visible line. */
  proj_update(d);
  double denom = (double)vs * pj.sinAzs + pj.voffset;
  double ad = denom < 0 ? -denom : denom;
  if (ad < 1e-3) denom = denom < 0 ? -1e-3 : 1e-3;   /* horizon line: clamp */
  double scaleA = pj.vplane / denom;
  double scaleB = scaleA / pj.cosAZS;
  d->out[0] = clamp16(256.0 * scaleA * pj.cosAas);   /* A */
  d->out[1] = clamp16(-256.0 * scaleB * pj.sinAas);  /* B */
  d->out[2] = clamp16(256.0 * scaleA * pj.sinAas);   /* C */
  d->out[3] = clamp16(256.0 * scaleB * pj.cosAas);   /* D */
}

static void cmd_project(Dsp1* d) {
  /* world (X,Y,Z) -> screen (H,V) + enlargement M (out[2], unsigned) */
  proj_update(d);
  double px = (double)d->in[0] - d->fx;
  double py = (double)d->in[1] - d->fy;
  double pz = (double)d->in[2] - d->fz;

  /* rotate by (pi - Aas) around Z: cos -> -cosAas, sin -> +sinAas */
  double x1 = px * (-pj.cosAas) - py * pj.sinAas;
  double y1 = px * pj.sinAas    - py * pj.cosAas;
  /* rotate by -Azs around X: cos -> cosAzs, sin -> -sinAzs */
  double y2 = y1 * pj.cosAzs + pz * pj.sinAzs;
  double z2 = -y1 * pj.sinAzs + pz * pj.cosAzs;
  z2 -= (double)d->lfe;

  if (z2 < 0.0) {
    double inv = (double)d->les / -z2;
    d->out[0] = clamp16(-x1 * inv);
    d->out[1] = clamp16(-y2 * inv);
    double m = 256.0 * (double)d->les / -z2;
    if (m > 65535.0) m = 65535.0;
    if (m < 0.0) m = 0.0;
    d->out[2] = (int16_t)(uint16_t)m;
  } else {
    d->out[0] = 0;
    d->out[1] = 224;
    d->out[2] = (int16_t)0xffff;
  }
}

static void cmd_target(Dsp1* d) {
  /* screen (H,V) -> ground (X,Y): apply the same per-line matrix the PPU
   * would use for line V (consistent with raster_line by construction;
   * unused by Mario Kart, kept coherent for Target-using titles). */
  proj_update(d);
  int16_t h = d->in[0];
  int16_t v = d->in[1];
  double denom = (double)v * pj.sinAzs + pj.voffset;
  double ad = denom < 0 ? -denom : denom;
  if (ad < 1e-3) denom = denom < 0 ? -1e-3 : 1e-3;
  double scaleA = pj.vplane / denom;
  double scaleB = scaleA / pj.cosAZS;
  d->out[0] = clamp16((double)d->centerX + scaleA * pj.cosAas * h
                                        - scaleB * pj.sinAas * v);
  d->out[1] = clamp16((double)d->centerY + scaleA * pj.sinAas * h
                                        + scaleB * pj.cosAas * v);
}

static void cmd_inverse(Dsp1* d) {
  /* floating inverse: value = in[0] * 2^in[1]; out mantissa Q15 in [0x4000,0x7fff] */
  int16_t a = d->in[0];
  int16_t e = d->in[1];
  if (a == 0) { d->out[0] = 0x7fff; d->out[1] = 0x7fff; return; }
  /* base is always exactly 2 with an integer exponent -- scalbn (direct
   * exponent manipulation) is exact and avoids pulling the generic pow()
   * (which pulls exp()/log()/log10()/fmod() with it: several KB nothing else
   * in this command needs). */
  double v = scalbn((double)a / 32768.0, e);
  double inv = 1.0 / v;
  int oe = 0;
  double m = fabs(inv);
  /* v can UNDERFLOW to exactly +-0.0 for e <~ -1060 (any nonzero a): then
   * inv = +-Inf, and Inf/2.0 == Inf, so the normalization loop below never
   * terminates -- a device hang for ~half of e's int16 range. The a==0
   * early-return above only covers the literal-zero input, not the
   * computed-underflow. Saturate exactly like that existing convention.
   * (m == 0.0 is the mirror overflow case, inv underflowed: same treatment.) */
  if (!isfinite(m) || m == 0.0) { d->out[0] = 0x7fff; d->out[1] = 0x7fff; return; }
  while (m >= 1.0) { m /= 2.0; oe++; }
  while (m < 0.5)  { m *= 2.0; oe--; }
  if (inv < 0) m = -m;
  d->out[0] = clamp16(m * 32768.0);
  d->out[1] = (int16_t)oe;
}

static void execute(Dsp1* d) {
  /* d->cmd holds the CANONICAL command (cmd_canon below folded the mirror
   * aliases already); NOPs never reach here */
  switch (d->cmd) {
    case 0x01: attitude(d, 0); return;
    case 0x11: attitude(d, 1); return;
    case 0x21: attitude(d, 2); return;
    case 0x03: subjective(d, 0); return;
    case 0x13: subjective(d, 1); return;
    case 0x23: subjective(d, 2); return;
    case 0x0b: scalar(d, 0); return;
    case 0x1b: scalar(d, 1); return;
    case 0x2b: scalar(d, 2); return;
    case 0x0d: objective(d, 0); return;
    case 0x1d: objective(d, 1); return;
    case 0x2d: objective(d, 2); return;
    case 0x00:  /* multiply: (a*b)>>15 */
      d->out[0] = clamp16((double)d->in[0] * d->in[1] / 32768.0);
      return;
    case 0x20:  /* multiply variant: (a*b)>>15 + 1 (chip's second entry) */
      d->out[0] = clamp16((double)d->in[0] * d->in[1] / 32768.0 + 1.0);
      return;
    case 0x04: {  /* triangle: r*sin, r*cos */
      double th = angle((uint16_t)d->in[0]);
      double r = (double)d->in[1];
      d->out[0] = clamp16(r * sin(th));
      d->out[1] = clamp16(r * cos(th));
      return;
    }
    case 0x08: {  /* radius: 32-bit (x^2+y^2+z^2)>>15, LSW first */
      double x = d->in[0], y = d->in[1], z = d->in[2];
      double r = (x * x + y * y + z * z) / 32768.0;
      uint32_t r32 = (r >= 4294967295.0) ? 0xffffffffu : (uint32_t)r;
      d->out[0] = (int16_t)(r32 & 0xffff);
      d->out[1] = (int16_t)(r32 >> 16);
      return;
    }
    case 0x0c: {  /* rotate 2D (coordinate system): in A,X,Y */
      double th = angle((uint16_t)d->in[0]);
      double x = d->in[1], y = d->in[2];
      d->out[0] = clamp16(x * cos(th) + y * sin(th));
      d->out[1] = clamp16(-x * sin(th) + y * cos(th));
      return;
    }
    case 0x10: cmd_inverse(d); return;
    case 0x18: {  /* range: (x^2+y^2+z^2-r^2)>>15 */
      double x = d->in[0], y = d->in[1], z = d->in[2], r = d->in[3];
      d->out[0] = clamp16((x * x + y * y + z * z - r * r) / 32768.0);
      return;
    }
    case 0x1c: {  /* polar: rotate (X,Y,Z) by Az,Ay,Ax (coordinate transform) */
      double az = angle((uint16_t)d->in[0]);
      double ay = angle((uint16_t)d->in[1]);
      double ax = angle((uint16_t)d->in[2]);
      double x = d->in[3], y = d->in[4], z = d->in[5];
      double x1 = x * cos(az) + y * sin(az), y1 = -x * sin(az) + y * cos(az);
      double x2 = x1 * cos(ay) - z * sin(ay), z1 = x1 * sin(ay) + z * cos(ay);
      double y2 = y1 * cos(ax) + z1 * sin(ax), z2 = -y1 * sin(ax) + z1 * cos(ax);
      d->out[0] = clamp16(x2); d->out[1] = clamp16(y2); d->out[2] = clamp16(z2);
      return;
    }
    case 0x28: {  /* distance: sqrt(x^2+y^2+z^2) */
      double x = d->in[0], y = d->in[1], z = d->in[2];
      d->out[0] = clamp16(sqrt(x * x + y * y + z * z));
      return;
    }
    case 0x02: cmd_parameter(d); return;
    case 0x06: cmd_project(d); return;
    case 0x0e: cmd_target(d); return;
    case 0x0a: raster_line(d, (int16_t)d->in[0]); return;   /* Vs is SIGNED */
    case 0x14: {  /* gyrate: integrate angular velocities (docs sparse; see log) */
      d->out[0] = (int16_t)(d->in[0] + d->in[3]);
      d->out[1] = (int16_t)(d->in[1] + d->in[4]);
      d->out[2] = (int16_t)(d->in[2] + d->in[5]);
      return;
    }
    case 0x0f:
      d->out[0] = 0x0000;      /* self test (RAM check): pass */
      return;
    case 0x2f:
      d->out[0] = 0x0100;      /* memory size probe: the chip reports 0x0100 */
      return;
    case 0x1f:
      /* data-ROM dump (1024 words on the real chip). No known retail game
       * depends on the contents at runtime; serving zeros keeps the protocol
       * shape without carrying the ROM image. */
      d->out[0] = 0x0000;
      return;
    default:
      break;
  }
  d->out[0] = 0;
}

/* The chip's byte-protocol dispatch, mirrored from its documented behavior:
 * mirror aliases fold per-group (Parameter answers at $02/$12/$22/$32,
 * Project at $06/$16/$26/$36, attitude A also at $05/$31/$35, ...) -- NOT a
 * uniform low-six-bits mask. Everything else, notably $80, is a NOP: the
 * chip consumes the byte and stays idle, no parameters, no output. Mario
 * Kart leans on that at race start -- it writes $80 to DR 128 times as a
 * protocol flush, which must leave the chip idle no matter what transfer
 * phase it was in. Treating $80 as a command (the old low-six-bits read of
 * the dispatch, "$80 = Multiply") shifted the byte stream by one command:
 * the race-init Parameter got eaten as phantom-Multiply parameters, every
 * Raster then ran on reset-default projection state, and the game's line
 * buffers filled with saturated 32767s -- the flat single-colour road.
 * Returns the canonical command, or 0xff for NOP. */
static uint8_t cmd_canon(uint8_t cmd, uint8_t* inW, uint8_t* outW) {
  switch (cmd) {
    case 0x00:                                  *inW = 2; *outW = 1; return 0x00; /* multiply */
    case 0x20:                                  *inW = 2; *outW = 1; return 0x20; /* multiply+1 */
    case 0x10: case 0x30:                       *inW = 2; *outW = 2; return 0x10; /* inverse */
    case 0x04: case 0x24:                       *inW = 2; *outW = 2; return 0x04; /* triangle */
    case 0x08:                                  *inW = 3; *outW = 2; return 0x08; /* radius */
    case 0x18: case 0x38:                       *inW = 4; *outW = 1; return 0x18; /* range */
    case 0x28:                                  *inW = 3; *outW = 1; return 0x28; /* distance */
    case 0x0c: case 0x2c:                       *inW = 3; *outW = 2; return 0x0c; /* rotate */
    case 0x1c: case 0x3c:                       *inW = 6; *outW = 3; return 0x1c; /* polar */
    case 0x02: case 0x12: case 0x22: case 0x32: *inW = 7; *outW = 4; return 0x02; /* parameter */
    case 0x0a: case 0x1a: case 0x2a: case 0x3a: *inW = 1; *outW = 4; return 0x0a; /* raster */
    case 0x06: case 0x16: case 0x26: case 0x36: *inW = 3; *outW = 3; return 0x06; /* project */
    case 0x0e: case 0x1e: case 0x2e: case 0x3e: *inW = 2; *outW = 2; return 0x0e; /* target */
    case 0x01: case 0x05: case 0x31: case 0x35: *inW = 4; *outW = 0; return 0x01; /* attitude A */
    case 0x11: case 0x15:                       *inW = 4; *outW = 0; return 0x11; /* attitude B */
    case 0x21: case 0x25:                       *inW = 4; *outW = 0; return 0x21; /* attitude C */
    case 0x0d: case 0x09: case 0x39: case 0x3d: *inW = 3; *outW = 3; return 0x0d; /* objective A */
    case 0x1d: case 0x19:                       *inW = 3; *outW = 3; return 0x1d; /* objective B */
    case 0x2d: case 0x29:                       *inW = 3; *outW = 3; return 0x2d; /* objective C */
    case 0x03: case 0x33:                       *inW = 3; *outW = 3; return 0x03; /* subjective A */
    case 0x13:                                  *inW = 3; *outW = 3; return 0x13; /* subjective B */
    case 0x23:                                  *inW = 3; *outW = 3; return 0x23; /* subjective C */
    case 0x0b: case 0x3b:                       *inW = 3; *outW = 1; return 0x0b; /* scalar A */
    case 0x1b:                                  *inW = 3; *outW = 1; return 0x1b; /* scalar B */
    case 0x2b:                                  *inW = 3; *outW = 1; return 0x2b; /* scalar C */
    case 0x14: case 0x34:                       *inW = 6; *outW = 3; return 0x14; /* gyrate */
    case 0x0f: case 0x07:                       *inW = 1; *outW = 1; return 0x0f; /* self test */
    case 0x2f: case 0x27:                       *inW = 1; *outW = 1; return 0x2f; /* memory size */
    case 0x1f: case 0x17: case 0x37: case 0x3f: *inW = 1; *outW = 1; return 0x1f; /* ROM dump */
    default:                                    *inW = 0; *outW = 0; return 0xff; /* incl. $80: NOP */
  }
}

/* ---- transfer state machine --------------------------------------------- */

static void start_command(Dsp1* d, uint8_t val) {
#if !defined(NDEBUG_DSP1_TRACE) && !defined(TARGET_GNW)
  static uint64_t seen;            /* one line per distinct opcode bucket, diagnostics */
  if (!(seen & (1ull << (val & 0x3f)))) {
    seen |= 1ull << (val & 0x3f);
    fprintf(stderr, "[dsp1] first use: cmd %02x\n", val);
  }
#endif
  uint8_t canon = cmd_canon(val, &d->inWords, &d->outWords);
  d->byteIdx = 0;
  if (canon == 0xff) {             /* $80 flush / undocumented: consume, stay idle */
    if (val != 0x80) d->unknownCmds++;   /* diagnostics only; $80 is expected */
    d->state = 0;
    return;
  }
  d->cmd = canon;
  memset(d->in, 0, sizeof(d->in));
  if (d->inWords == 0) {           /* no params: execute immediately */
    execute(d);
    d->state = d->outWords ? 2 : 0;
  } else {
    d->state = 1;
  }
}

/* byte-level wire trace for bring-up: DSP1_TRACE=1 in the environment.
 * Host-only: getenv() has nothing to read on-device (no environment), so this
 * is compiled out under TARGET_GNW rather than left to always resolve false --
 * it was the sole caller of getenv() in the whole firmware, pulling in real
 * newlib reentrant-environ support for a check that can never do anything. */
#ifndef TARGET_GNW
#include <stdlib.h>
static int trace_on(void) {
  static int t = -1;
  if (t < 0) t = getenv("DSP1_TRACE") ? 1 : 0;
  return t;
}
#else
static int trace_on(void) { return 0; }
#endif

void dsp1_writeDR(Dsp1* d, uint8_t val) {
  if (trace_on()) fprintf(stderr, "W %02x s%d i%d c%02x\n", val, d->state, d->byteIdx, d->cmd);
  switch (d->state) {
    case 0:                        /* idle: command byte */
      start_command(d, val);
      return;
    case 1: {                      /* collecting parameter bytes, LSB first */
      int w = d->byteIdx >> 1;
      if (d->byteIdx & 1) d->in[w] = (int16_t)((d->in[w] & 0x00ff) | (val << 8));
      else                d->in[w] = (int16_t)((d->in[w] & 0xff00) | val);
      d->byteIdx++;
      if (d->byteIdx >= d->inWords * 2) {
        execute(d);
        if (trace_on()) {   /* LOCAL DIAG: command-level trace with values */
          fprintf(stderr, "X c%02x in[%d %d %d %d %d %d %d] out[%d %d %d %d]\n",
                  d->cmd & 0x3f, d->in[0], d->in[1], d->in[2], d->in[3],
                  d->in[4], d->in[5], d->in[6],
                  d->out[0], d->out[1], d->out[2], d->out[3]);
        }
        d->byteIdx = 0;
        if (d->cmd == 0x0a) { d->rasterVs = (uint16_t)d->in[0]; d->state = 3; }
        else d->state = d->outWords ? 2 : 0;
      }
      return;
    }
    case 3:
      /* Raster stream: a write consumes (and discards) one pending output
       * byte; only once the current line's 8 bytes are drained does a write
       * count as a command byte again. This is how Mario Kart exits the
       * stream: after reading its 96 lines it writes exactly 8 filler bytes
       * ($8000 x4) to drain the auto-refilled line, then sends the next real
       * command. Taking the first filler byte as a command (the old code)
       * desynced the byte phase by half a protocol cycle. */
      d->byteIdx++;
      if (d->byteIdx >= 8) { d->state = 0; d->byteIdx = 0; }
      return;
    default:
      /* a write while results are pending = the host moved on: treat as a
       * new command byte (the chip re-arms for a command as soon as a
       * command finishes executing) */
      d->state = 0;
      start_command(d, val);
      return;
  }
}

uint8_t dsp1_readDR(Dsp1* d) {
  if (trace_on()) {
    /* LOCAL DIAG: include the byte the game will actually receive */
    uint8_t peek = 0xff;
    if (d->state == 2 || d->state == 3) {
      int w = d->byteIdx >> 1;
      peek = (d->byteIdx & 1) ? (uint8_t)(d->out[w] >> 8) : (uint8_t)d->out[w];
    }
    fprintf(stderr, "R s%d i%d c%02x =%02x\n", d->state, d->byteIdx, d->cmd, peek);
  }
  switch (d->state) {
    case 2: {                      /* result bytes, LSB first */
      int w = d->byteIdx >> 1;
      uint8_t b = (d->byteIdx & 1) ? (uint8_t)(d->out[w] >> 8) : (uint8_t)d->out[w];
      d->byteIdx++;
      if (d->byteIdx >= d->outWords * 2) { d->state = 0; d->byteIdx = 0; }
      return b;
    }
    case 3: {                      /* raster stream: 4 words per line, then next line */
      int w = d->byteIdx >> 1;
      uint8_t b = (d->byteIdx & 1) ? (uint8_t)(d->out[w] >> 8) : (uint8_t)d->out[w];
      d->byteIdx++;
      if (d->byteIdx >= 8) {       /* line consumed: advance and refill */
        d->byteIdx = 0;
        d->rasterVs++;
        raster_line(d, (int16_t)d->rasterVs);  /* signed walk: -73 -> 0 -> +150 */
        if (trace_on())              /* LOCAL DIAG: per-line stream values */
          fprintf(stderr, "RL vs=%d out[%d %d %d %d]\n", (int16_t)d->rasterVs,
                  d->out[0], d->out[1], d->out[2], d->out[3]);
      }
      return b;
    }
    case 1:
      /* A read while we are collecting parameters means host and chip have
       * lost byte alignment (the boot-time probe spam does this). The host
       * only reads when it believes results are pending, so resynchronize:
       * abandon the partial command; the next write is a command byte. Games
       * re-issue their per-frame command cycle, so one discarded cycle heals
       * everything. */
      d->state = 0;
      d->byteIdx = 0;
      return 0xff;
    default:
      return 0xff;                 /* nothing pending */
  }
}

/* strong allocation pair for cart.c's weak fallbacks */
#include <stdlib.h>
Dsp1* dsp1_alloc(void) {
  return (Dsp1*)snes_zalloc(sizeof(Dsp1));
}
uint32_t dsp1_size(void) { return (uint32_t)sizeof(Dsp1); }
