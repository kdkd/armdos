/* Floating-point benchmark: n-body (double), mandelbrot (double), a float
   matrix/vector kernel (Quake-style 3D transform), all with checksums. */
#include "lib.h"
#ifndef ROUNDS
#define ROUNDS 1
#endif
#if defined(__ARM_FP)
#define SQRT(x) __builtin_sqrt(x)
#else
static double SQRT(double x) { if (x <= 0) return 0; double r = x > 1 ? x : 1; for (int i = 0; i < 60; i++) { double n = 0.5 * (r + x / r); if (n == r) break; r = n; } return r; }
#endif
typedef union { double d; unsigned long long u; } D;
typedef union { float f; u32 u; } F;
struct body { double x, y, z, vx, vy, vz, m; };
#define PI 3.141592653589793
#define SM (4 * PI * PI)
#define DPY 365.24
static struct body bodies[5] = {
  {0, 0, 0, 0, 0, 0, SM},
  {4.84143144246472090e+00, -1.16032004402742839e+00, -1.03622044471123109e-01, 1.66007664274403694e-03 * DPY, 7.69901118419740425e-03 * DPY, -6.90460016972063023e-05 * DPY, 9.54791938424326609e-04 * SM},
  {8.34336671824457987e+00, 4.12479856412430479e+00, -4.03523417114321381e-01, -2.76742510726862411e-03 * DPY, 4.99852801234917238e-03 * DPY, 2.30417297573763929e-05 * DPY, 2.85885980666130812e-04 * SM},
  {1.28943695621391310e+01, -1.51111514016986312e+01, -2.23307578892655734e-01, 2.96460137564761618e-03 * DPY, 2.37847173959480950e-03 * DPY, -2.96589568540237556e-05 * DPY, 4.36624404335156298e-05 * SM},
  {1.53796971148509165e+01, -2.59193146099879641e+01, 1.79258772950371181e-01, 2.68067772490389322e-03 * DPY, 1.62824170038242295e-03 * DPY, -9.51592254519715870e-05 * DPY, 5.15138902046611451e-05 * SM}};
static double energy(void) {
  double e = 0;
  for (int i = 0; i < 5; i++) {
    struct body *b = &bodies[i];
    e += 0.5 * b->m * (b->vx * b->vx + b->vy * b->vy + b->vz * b->vz);
    for (int j = i + 1; j < 5; j++) { struct body *c = &bodies[j]; double dx = b->x - c->x, dy = b->y - c->y, dz = b->z - c->z; e -= b->m * c->m / SQRT(dx * dx + dy * dy + dz * dz); }
  }
  return e;
}
static void advance(double dt) {
  for (int i = 0; i < 5; i++) {
    struct body *b = &bodies[i];
    for (int j = i + 1; j < 5; j++) {
      struct body *c = &bodies[j];
      double dx = b->x - c->x, dy = b->y - c->y, dz = b->z - c->z;
      double d2 = dx * dx + dy * dy + dz * dz, mag = dt / (d2 * SQRT(d2));
      b->vx -= dx * c->m * mag; b->vy -= dy * c->m * mag; b->vz -= dz * c->m * mag;
      c->vx += dx * b->m * mag; c->vy += dy * b->m * mag; c->vz += dz * b->m * mag;
    }
  }
  for (int i = 0; i < 5; i++) { struct body *b = &bodies[i]; b->x += dt * b->vx; b->y += dt * b->vy; b->z += dt * b->vz; }
}
static u32 mandel(int w, int h) {
  u32 sum = 0;
  for (int y = 0; y < h; y++) for (int x = 0; x < w; x++) {
    double cr = 2.0 * x / w - 1.5, ci = 2.0 * y / h - 1.0, zr = 0, zi = 0; int k = 0;
    while (k < 100 && zr * zr + zi * zi < 4.0) { double t = zr * zr - zi * zi + cr; zi = 2 * zr * zi + ci; zr = t; k++; }
    sum = sum * 31 + k;
  }
  return sum;
}
static float verts[3000][3], out[3000][3];
static u32 xform(void) {
  float m[3][4] = {{0.36f, 0.48f, -0.8f, 10.0f}, {-0.8f, 0.6f, 0.0f, -3.5f}, {0.48f, 0.64f, 0.6f, 1.25f}};
  for (int i = 0; i < 3000; i++) for (int k = 0; k < 3; k++) verts[i][k] = (float)((i * 7 + k * 13) % 101) * 0.37f - 18.0f;
  u32 h = 0;
  for (int r = 0; r < 20; r++) {
    for (int i = 0; i < 3000; i++) {
      float x = verts[i][0], y = verts[i][1], z = verts[i][2];
      float tx = m[0][0] * x + m[0][1] * y + m[0][2] * z + m[0][3];
      float ty = m[1][0] * x + m[1][1] * y + m[1][2] * z + m[1][3];
      float tz = m[2][0] * x + m[2][1] * y + m[2][2] * z + m[2][3];
      float iz = 1.0f / (tz + 100.0f);
      out[i][0] = tx * iz * 160.0f + 160.0f; out[i][1] = ty * iz * 100.0f + 100.0f; out[i][2] = iz;
    }
    m[0][3] += 0.5f;
  }
  for (int i = 0; i < 3000; i++) { F a; a.f = out[i][0]; F b; b.f = out[i][1]; h = h * 33 + a.u + (b.u >> 3) + (int)out[i][0]; }
  return h;
}
int main(void) {
  printf_("fbench start\n");
  for (int r = 0; r < ROUNDS; r++) {
    D e0; e0.d = energy();
    for (int i = 0; i < 20000; i++) advance(0.01);
    D e1; e1.d = energy();
    printf_("nbody %08x%08x -> %08x%08x\n", (u32)(e0.u >> 32), (u32)e0.u, (u32)(e1.u >> 32), (u32)e1.u);
    printf_("mandel %08x\n", mandel(160, 100));
    printf_("xform %08x\n", xform());
  }
  printf_("fbench done\n");
  return 0;
}
