// PhobGCC's stick and trigger processing. See phob.h for what this is ported from and what differs.
// The functions keep the firmware's structure and order so they can be compared with it line by
// line; comments marked "firmware:" quote or follow the original.
// SPDX-License-Identifier: GPL-3.0-only
#include "phob.h"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <sstream>

namespace host::phob {
namespace {

constexpr float kPi = 3.14159265358979323846f;
constexpr float kFloatOrigin = 127.5f;
constexpr float kMaxStickAngle = 0.4886921906f;   // firmware: 28 degrees, the stick's max angular deflection

enum : uint16_t {
  PAD_LEFT = 0x0001, PAD_RIGHT = 0x0002, PAD_DOWN = 0x0004, PAD_UP = 0x0008, PAD_Z = 0x0010, PAD_R = 0x0020, PAD_L = 0x0040,
  PAD_A = 0x0100, PAD_B = 0x0200, PAD_X = 0x0400, PAD_Y = 0x0800, PAD_START = 0x1000,
};

// firmware: _calOrder, _notchStatusDefaults, _notchAdjOrder
constexpr int kCalOrder[kCalPoints] = {0, 1, 8, 9, 16, 17, 24, 25, 4, 5, 12, 13, 20, 21, 28, 29,
                                       2, 3, 6, 7, 10, 11, 14, 15, 18, 19, 22, 23, 26, 27, 30, 31};
constexpr NotchStatus kNotchStatusDefaults[kNotches] = {
    CARDINAL, TERT_ACTIVE, SECONDARY, TERT_ACTIVE, CARDINAL, TERT_ACTIVE, SECONDARY, TERT_ACTIVE,
    CARDINAL, TERT_ACTIVE, SECONDARY, TERT_ACTIVE, CARDINAL, TERT_ACTIVE, SECONDARY, TERT_ACTIVE};
constexpr int kNotchAdjOrder[kAdjNotches] = {2, 6, 10, 14, 1, 3, 5, 7, 9, 11, 13, 15};

float default_angle(int i) { return kPi * (float)i / 8.0f; }

void strip_cal_points(const float cx[], const float cy[], float sx[], float sy[]) {
  for (int i = 0; i < kCalPoints; i++) {
    sx[i] = cx[i];
    sy[i] = cy[i];
    if ((i + 1) % 4 == 0) {   // firmware: non-cardinal non-diagonal notch, set equal to origin
      sx[i] = cx[0];
      sy[i] = cy[0];
    }
  }
}

void compute_stick_angles(const float x[], const float y[], float angles[]) {
  for (int i = 0; i < kNotches; i++) {
    if (i % 2 == 0) angles[i] = default_angle(i);
    else angle_on_sphere(x[i + 1], y[i + 1], angles[i]);
  }
}

void clean_notches(float notch_angles[], const float measured[], const NotchStatus status[]) {
  for (int i = 0; i < kNotches; i++)
    if (status[i] == TERT_INACTIVE) notch_angles[i] = measured[i];
}

void transform_cal_points(const float xi[], const float yi[], float xo[], float yo[], const StickParams& p,
                          const Settings& s, Stick which) {
  for (int i = 0; i < kNotches + 1; i++) {
    const float xv = linearize(xi[i], p.fit_x), yv = linearize(yi[i], p.fit_y);
    notch_remap(xv, yv, xo[i], yo[i], p, 0, s, which);
  }
}

void clean_cal_points(const float cal_x[], const float cal_y[], const float notch_angles[], float cleaned_x[],
                      float cleaned_y[], float notch_x[], float notch_y[], NotchStatus status[]) {
  notch_x[0] = 0; notch_y[0] = 0;
  cleaned_x[0] = 0; cleaned_y[0] = 0;
  for (int i = 0; i < kNotches; i++) {
    cleaned_x[0] += cal_x[i * 2];   // firmware: add the origin values to the first x,y point
    cleaned_y[0] += cal_y[i * 2];
    cleaned_x[i + 1] = cal_x[i * 2 + 1];
    cleaned_y[i + 1] = cal_y[i * 2 + 1];
    calc_stick_values(notch_angles[i], notch_x[i + 1], notch_y[i + 1]);
    notch_x[i + 1] = std::round(notch_x[i + 1]);
    notch_y[i + 1] = std::round(notch_y[i + 1]);
  }
  // firmware: remove the largest and smallest two origin values to remove outliers
  int smallest_x = 0, small_x = 0, large_x = 0, largest_x = 0;
  int smallest_y = 0, small_y = 0, large_y = 0, largest_y = 0;
  for (int i = 0; i < kNotches; i++) {
    if (cal_x[i * 2] < cal_x[smallest_x]) { small_x = smallest_x; smallest_x = i * 2; }
    else if (cal_x[i * 2] < cal_x[small_x]) small_x = i * 2;
    if (cal_x[i * 2] > cal_x[largest_x]) { large_x = largest_x; largest_x = i * 2; }
    else if (cal_x[i * 2] > cal_x[large_x]) large_x = i * 2;
    if (cal_y[i * 2] < cal_y[smallest_y]) { small_y = smallest_y; smallest_y = i * 2; }
    else if (cal_y[i * 2] < cal_y[small_y]) small_y = i * 2;
    if (cal_y[i * 2] > cal_y[largest_y]) { large_y = largest_y; largest_y = i * 2; }
    else if (cal_y[i * 2] > cal_y[large_y]) large_y = i * 2;
  }
  cleaned_x[0] -= cal_x[smallest_x]; cleaned_x[0] -= cal_x[small_x];
  cleaned_x[0] -= cal_x[large_x];    cleaned_x[0] -= cal_x[largest_x];
  cleaned_y[0] -= cal_y[smallest_y]; cleaned_y[0] -= cal_y[small_y];
  cleaned_y[0] -= cal_y[large_y];    cleaned_y[0] -= cal_y[largest_y];
  cleaned_x[0] = cleaned_x[0] / ((float)kNotches - 4);
  cleaned_y[0] = cleaned_y[0] / ((float)kNotches - 4);

  for (int i = 0; i < kNotches; i++) {
    const float dx = cleaned_x[i + 1] - cleaned_x[0], dy = cleaned_y[i + 1] - cleaned_y[0];
    const float mag = std::sqrt(dx * dx + dy * dy);
    if (mag < 0.02f && (i % 2) != 0) {
      // firmware: the cleaned point was at the center and would be a firefox notch: average the
      // previous and next points (cardinal & diagonal), and skip notch adjustment for it
      const int prev = ((i - 1 + kNotches) % kNotches) + 1;
      const int next = ((i + 1) % kNotches) + 1;
      cleaned_x[i + 1] = (cleaned_x[prev] + cleaned_x[next]) / 2.0f;
      cleaned_y[i + 1] = (cleaned_y[prev] + cleaned_y[next]) / 2.0f;
      notch_x[i + 1] = (notch_x[prev] + notch_x[next]) / 2.0f;
      notch_y[i + 1] = (notch_y[prev] + notch_y[next]) / 2.0f;
      status[i] = TERT_INACTIVE;
    } else {
      status[i] = kNotchStatusDefaults[i];
    }
  }
}

// firmware: the notch adjustment is limited to control displacement and stretching (max +/- 30%)
void legalize_notch(int idx, const float measured[], float notch_angles[]) {
  bool diagonal = false;
  int prev = (idx - 1 + kNotches) % kNotches;
  int next = (idx + 1) % kNotches;
  if ((idx - 2) % 4 == 0) {   // firmware: for diagonals, the cardinals are the index points
    prev = (idx - 2 + kNotches) % kNotches;
    next = (idx + 2) % kNotches;
    diagonal = true;
  }
  const float prev_angle = notch_angles[prev];
  float next_angle = notch_angles[next];
  if (next_angle < prev_angle) next_angle += 2 * kPi;
  const float prev_meas = measured[prev];
  const float this_meas = measured[idx];
  float next_meas = measured[next];
  if (next_meas < this_meas) next_meas += 2 * kPi;

  const float cmp = diagonal ? 0.769f : 0.5f;
  const float str = diagonal ? 1.3f : 2.0f;
  const float lower_compress = prev_angle + cmp * (this_meas - prev_meas);
  float lower_stretch = next_angle - str * (next_meas - this_meas);
  const float upper_compress = next_angle - cmp * (next_meas - this_meas);
  float upper_stretch = prev_angle + str * (this_meas - prev_meas);

  // firmware: stretch leniency for angles within the deadzone next to a cardinal
  const float min_threshold = 0.1500f / 0.9750f;
  const float deadzone_limit = 0.2875f / 0.9500f;
  const float deadzone_plus = 0.3250f / 0.9375f;
  if (prev % 4 == 0 && !diagonal && (this_meas - prev_meas) > min_threshold && (this_meas - prev_meas) < deadzone_limit)
    upper_stretch = prev_angle + std::fmax(str * (this_meas - prev_meas), deadzone_plus);
  if (next % 4 == 0 && !diagonal && (next_meas - this_meas) > min_threshold && (next_meas - this_meas) < deadzone_limit)
    lower_stretch = next_angle - std::fmax(str * (next_meas - this_meas), deadzone_plus);

  const float lower_distort = std::fmax(lower_compress, lower_stretch);
  float upper_distort = std::fmin(upper_compress, upper_stretch);
  if (upper_distort < lower_distort) upper_distort += 2 * kPi;
  const float lower = lower_distort;
  float upper = upper_distort;
  if (upper < lower) upper += 2 * kPi;
  notch_angles[idx] = std::fmax(notch_angles[idx], lower);
  notch_angles[idx] = std::fmin(notch_angles[idx], upper);
}

void legalize_notches(int step, const float measured[], float notch_angles[]) {
  for (int i = step; i < kCalSteps; i++) legalize_notch(kNotchAdjOrder[i - kCalPoints], measured, notch_angles);
}

// Least-squares polynomial fit, highest order first (what the firmware's fitCurve returns).
void fit_curve(int order, int n, const double px[], const double py[], double coeffs[]) {
  const int m = order + 1;
  double a[8][9] = {};
  for (int r = 0; r < m; r++) {
    for (int c = 0; c < m; c++) {
      double sum = 0;
      for (int i = 0; i < n; i++) sum += std::pow(px[i], (double)(order - r)) * std::pow(px[i], (double)(order - c));
      a[r][c] = sum;
    }
    double sum = 0;
    for (int i = 0; i < n; i++) sum += std::pow(px[i], (double)(order - r)) * py[i];
    a[r][m] = sum;
  }
  for (int col = 0; col < m; col++) {
    int pivot = col;
    for (int r = col + 1; r < m; r++) if (std::fabs(a[r][col]) > std::fabs(a[pivot][col])) pivot = r;
    if (pivot != col) for (int c = 0; c <= m; c++) std::swap(a[col][c], a[pivot][c]);
    if (a[col][col] == 0) { for (int i = 0; i < m; i++) coeffs[i] = 0; return; }
    for (int r = 0; r < m; r++) {
      if (r == col) continue;
      const double f = a[r][col] / a[col][col];
      for (int c = col; c <= m; c++) a[r][c] -= f * a[col][c];
    }
  }
  for (int i = 0; i < m; i++) coeffs[i] = a[i][m] / a[i][i];
}

void linearize_cal(const float in_x[], const float in_y[], float out_x[], float out_y[], StickParams& p) {
  if (in_x[0] == 0 && in_y[0] == 0) {   // firmware: if the fit points are 0, then output 0 fit parameters
    out_x[0] = 0; out_x[1] = 0; out_x[3] = 0; out_x[4] = 0; out_x[5] = 0;
    out_y[0] = 0; out_y[1] = 0; out_y[3] = 0; out_y[4] = 0; out_y[5] = 0;
    return;
  }
  double fit_x[5], fit_y[5];
  fit_x[0] = in_x[8 + 1];
  fit_x[1] = (in_x[6 + 1] + in_x[10 + 1]) / 2.0;
  fit_x[2] = in_x[0];
  fit_x[3] = (in_x[2 + 1] + in_x[14 + 1]) / 2.0;
  fit_x[4] = in_x[0 + 1];
  fit_y[0] = in_y[12 + 1];
  fit_y[1] = (in_y[10 + 1] + in_y[14 + 1]) / 2.0;
  fit_y[2] = in_y[0];
  fit_y[3] = (in_y[6 + 1] + in_y[2 + 1]) / 2.0;
  fit_y[4] = in_y[4 + 1];
  // firmware: -100 -74.246 0 74.246 100, centered around 0-255 (spherical motion, not sin 45)
  const double x_output[5] = {27.5, 53.2537879754, 127.5, 201.7462120246, 227.5};
  const double y_output[5] = {27.5, 53.2537879754, 127.5, 201.7462120246, 227.5};
  double cx[4], cy[4];
  fit_curve(3, 5, fit_x, x_output, cx);
  fit_curve(3, 5, fit_y, y_output, cy);
  for (int i = 0; i < 4; i++) { p.fit_x[i] = (float)cx[i]; p.fit_y[i] = (float)cy[i]; }
  // firmware: take out the offset so the stick zero position is 0
  const float x_zero = linearize((float)fit_x[2], p.fit_x);
  const float y_zero = linearize((float)fit_y[2], p.fit_y);
  p.fit_x[3] = p.fit_x[3] - x_zero;
  p.fit_y[3] = p.fit_y[3] - y_zero;
  for (int i = 0; i <= kNotches; i++) {
    out_x[i] = linearize(in_x[i], p.fit_x);
    out_y[i] = linearize(in_y[i], p.fit_y);
  }
}

void inverse(const float in[3][3], float (&out)[3][3]) {
  const float det = in[0][0] * (in[1][1] * in[2][2] - in[2][1] * in[1][2]) -
                    in[0][1] * (in[1][0] * in[2][2] - in[1][2] * in[2][0]) +
                    in[0][2] * (in[1][0] * in[2][1] - in[1][1] * in[2][0]);
  const float invdet = 1 / det;
  out[0][0] = (in[1][1] * in[2][2] - in[2][1] * in[1][2]) * invdet;
  out[0][1] = (in[0][2] * in[2][1] - in[0][1] * in[2][2]) * invdet;
  out[0][2] = (in[0][1] * in[1][2] - in[0][2] * in[1][1]) * invdet;
  out[1][0] = (in[1][2] * in[2][0] - in[1][0] * in[2][2]) * invdet;
  out[1][1] = (in[0][0] * in[2][2] - in[0][2] * in[2][0]) * invdet;
  out[1][2] = (in[1][0] * in[0][2] - in[0][0] * in[1][2]) * invdet;
  out[2][0] = (in[1][0] * in[2][1] - in[2][0] * in[1][1]) * invdet;
  out[2][1] = (in[2][0] * in[0][1] - in[0][0] * in[2][1]) * invdet;
  out[2][2] = (in[0][0] * in[1][1] - in[1][0] * in[0][1]) * invdet;
}

void notch_calibrate(const float xi[], const float yi[], const float xo[], const float yo[], StickParams& p) {
  for (int i = 1; i <= kNotches; i++) {
    const int j = i == kNotches ? 1 : i + 1;   // firmware: the final region closes back on the first point
    const float in[3][3] = {{xi[0], xi[i], xi[j]}, {yi[0], yi[i], yi[j]}, {1, 1, 1}};
    const float out[3][3] = {{xo[0], xo[i], xo[j]}, {yo[0], yo[i], yo[j]}, {1, 1, 1}};
    float inv[3][3];
    inverse(in, inv);
    float a[3][3];
    for (int r = 0; r < 3; r++)
      for (int c = 0; c < 3; c++) {
        a[r][c] = 0;
        for (int k = 0; k < 3; k++) a[r][c] += out[r][k] * inv[k][c];
      }
    for (int r = 0; r < 2; r++)
      for (int c = 0; c < 2; c++) p.affine[i - 1][r * 2 + c] = a[r][c];
    p.boundary[i - 1] = std::atan2(yi[i] - yi[0], xi[i] - xi[0]);
    if (p.boundary[i - 1] < p.boundary[0]) p.boundary[i - 1] += kPi * 2;   // firmware: unwrap so the first is smallest
  }
}

// applyCalFromPoints: from freshly measured points, the measured notch angles and the stick
// parameters, with `notch_angles` starting from the saved ones.
void apply_cal_from_points(const float saved_angles[], float notch_angles[], const float temp_x[], const float temp_y[],
                           NotchStatus status[], float measured[], StickParams& p, const Settings& s, Stick which) {
  std::memcpy(notch_angles, saved_angles, sizeof(float) * kNotches);   // firmware: recall previous notch angles
  float tt_x[kCalPoints], tt_y[kCalPoints];
  strip_cal_points(temp_x, temp_y, tt_x, tt_y);
  float cleaned_x[kNotches + 1], cleaned_y[kNotches + 1], notch_x[kNotches + 1], notch_y[kNotches + 1];
  float defaults[kNotches];
  for (int i = 0; i < kNotches; i++) defaults[i] = default_angle(i);
  clean_cal_points(tt_x, tt_y, defaults, cleaned_x, cleaned_y, notch_x, notch_y, status);
  linearize_cal(cleaned_x, cleaned_y, cleaned_x, cleaned_y, p);
  notch_calibrate(cleaned_x, cleaned_y, notch_x, notch_y, p);
  clean_cal_points(temp_x, temp_y, defaults, cleaned_x, cleaned_y, notch_x, notch_y, status);
  float tx[kNotches + 1], ty[kNotches + 1];
  transform_cal_points(cleaned_x, cleaned_y, tx, ty, p, s, which);
  compute_stick_angles(tx, ty, measured);
  clean_cal_points(temp_x, temp_y, measured, cleaned_x, cleaned_y, notch_x, notch_y, status);
  clean_notches(notch_angles, measured, status);
  clean_cal_points(temp_x, temp_y, notch_angles, cleaned_x, cleaned_y, notch_x, notch_y, status);
  linearize_cal(cleaned_x, cleaned_y, cleaned_x, cleaned_y, p);
  notch_calibrate(cleaned_x, cleaned_y, notch_x, notch_y, p);
}

// The firmware's FilterGains defaults ("these values are for 800 hz") through recomputeGains.
float vel_damp_from_snapback(int snapback) {
  if (snapback >= 0) return (float)(0.125 * std::pow(2, (snapback - 4) / 3.0));
  return (float)(1 - 0.25 * std::pow(2, (snapback + 4) / 3.0));
}
float smoothing_gain(int setting) { return setting <= 8 ? setting / 10.0f : setting / 100.0f + 0.72f; }

void recompute_gains(const Settings& s, Gains& g) {
  const float time_factor = 1.0f / 1.2f, time_divisor = 1.2f / 1.0f;
  const float max_stick = 100, vel_decay = 0.1f, vel_pos = 0.01f, vel_thresh = 1.00f, accel_thresh = 3.00f;
  g.max_stick = max_stick * max_stick;
  g.x_vel_decay = vel_decay * time_factor;
  g.y_vel_decay = vel_decay * time_factor;
  g.x_vel_pos = vel_pos * time_factor;
  g.y_vel_pos = vel_pos * time_factor;
  g.x_vel_damp = vel_damp_from_snapback(s.x_snapback);
  if (s.x_snapback >= 0) g.x_vel_damp *= time_divisor;
  g.y_vel_damp = vel_damp_from_snapback(s.y_snapback);
  if (s.y_snapback >= 0) g.y_vel_damp *= time_divisor;
  g.vel_thresh = 1 / (vel_thresh * time_factor);
  g.accel_thresh = 1 / (accel_thresh * time_factor);
  g.vel_thresh = g.vel_thresh * g.vel_thresh;
  g.accel_thresh = g.accel_thresh * g.accel_thresh;
  g.ax_smooth = (float)std::pow(1 - smoothing_gain(s.ax_smoothing), time_divisor);
  g.ay_smooth = (float)std::pow(1 - smoothing_gain(s.ay_smoothing), time_divisor);
  g.cx_smooth = (float)std::pow(1 - smoothing_gain(s.cx_smoothing), time_divisor);
  g.cy_smooth = (float)std::pow(1 - smoothing_gain(s.cy_smoothing), time_divisor);
}

void run_kalman(PortState& st, float xz, float yz, const Settings& s, const Gains& g) {
  const float old_xpos = st.k_xpos, old_ypos = st.k_ypos;
  const float old_xpos_filt = st.x_pos_filt, old_ypos_filt = st.y_pos_filt;
  const float old_xvel = st.k_xvel, old_yvel = st.k_yvel;
  const float old_xvel_filt = st.k_xvel_filt, old_yvel_filt = st.k_yvel_filt;
  st.k_xpos = xz;
  st.k_ypos = yz;
  st.k_xvel = st.k_xpos - old_xpos;
  st.k_yvel = st.k_ypos - old_ypos;
  const float xvel_smooth = 0.5f * (st.k_xvel + old_xvel);
  const float yvel_smooth = 0.5f * (st.k_yvel + old_yvel);
  const float xaccel = st.k_xvel - old_xvel;
  const float yaccel = st.k_yvel - old_yvel;
  const float old_xpos_diff = old_xpos - old_xpos_filt;
  const float old_ypos_diff = old_ypos - old_ypos_filt;
  const float d2 = std::fmin(g.max_stick, st.k_xpos * st.k_xpos + st.k_ypos * st.k_ypos) / g.max_stick;
  const float d6 = d2 * d2 * d2;
  const float vel_w1 = d2, vel_w2 = 1 - vel_w1;

  if (s.x_snapback > 0) {
    st.k_xvel_filt = vel_w1 * st.k_xvel + (1 - g.x_vel_decay) * vel_w2 * old_xvel_filt + g.x_vel_pos * old_xpos_diff;
    const float w_va = 1 - (float)std::fmin(1.0, xvel_smooth * xvel_smooth * g.vel_thresh + xaccel * xaccel * g.accel_thresh);
    const float w1 = std::fmax(w_va, d6), w2 = 1 - w1;
    st.x_pos_filt = w1 * st.k_xpos + w2 * (old_xpos_filt + (1 - g.x_vel_damp) * st.k_xvel_filt);
  } else if (s.x_snapback < 0) {
    const float lpf = old_xpos_filt * g.x_vel_damp + st.k_xpos * (1 - g.x_vel_damp);
    const float w_va = 1 - (float)std::fmin(1.0, xvel_smooth * xvel_smooth * g.vel_thresh + xaccel * xaccel * g.accel_thresh);
    const float w1 = std::fmax(w_va, d6), w2 = 1 - w1;
    st.x_pos_filt = w1 * st.k_xpos + w2 * lpf;
  } else {
    st.x_pos_filt = st.k_xpos;
  }
  if (s.y_snapback > 0) {
    st.k_yvel_filt = vel_w1 * st.k_yvel + (1 - g.y_vel_decay) * vel_w2 * old_yvel_filt + g.y_vel_pos * old_ypos_diff;
    const float w_va = 1 - (float)std::fmin(1.0, yvel_smooth * yvel_smooth * g.vel_thresh + yaccel * yaccel * g.accel_thresh);
    const float w1 = std::fmax(w_va, d6), w2 = 1 - w1;
    st.y_pos_filt = w1 * st.k_ypos + w2 * (old_ypos_filt + (1 - g.y_vel_damp) * st.k_yvel_filt);
  } else if (s.y_snapback < 0) {
    const float lpf = old_ypos_filt * g.y_vel_damp + st.k_ypos * (1 - g.y_vel_damp);
    const float w_va = 1 - (float)std::fmin(1.0, yvel_smooth * yvel_smooth * g.vel_thresh + yaccel * yaccel * g.accel_thresh);
    const float w1 = std::fmax(w_va, d6), w2 = 1 - w1;
    st.y_pos_filt = w1 * st.k_ypos + w2 * lpf;
  } else {
    st.y_pos_filt = st.k_ypos;
  }
}

float waveshape_mult(int setting) {
  if (setting > 0 && setting <= 5) return 1.0f / (440 - 40 * setting);
  if (setting > 5 && setting <= 15) return 1.0f / (340 - 20 * setting);
  if (setting > 15 && setting <= 24) return 1.0f / (100 - 4 * setting);
  return 0;
}

// `a_stick` keeps a firmware quirk: the A-stick's Y waveshaping uses the X factor in its
// acceleration term (aRunWaveShaping), the C-stick's does not (cRunWaveShaping).
void run_waveshaping(PortState::Wave& w, float x, float y, float& xo, float& yo, int x_setting, int y_setting,
                     const Gains& g, bool a_stick) {
  const float xvel = x - w.old_x, yvel = y - w.old_y;
  const float xvs = 0.5f * (xvel + w.old_vx), yvs = 0.5f * (yvel + w.old_vy);
  const float xacc = xvel - w.old_vx, yacc = yvel - w.old_vy;
  const float xf = waveshape_mult(std::abs(x_setting));
  const float yf = waveshape_mult(std::abs(y_setting));
  const float high_x = x_setting < 0 ? 1.0f : 0.0f, high_y = y_setting < 0 ? 1.0f : 0.0f;
  const float accel_factor = 3;
  const float old_xw = (float)std::fmin(1.0, xvs * xvs * g.vel_thresh * xf + high_x * xacc * xacc * g.accel_thresh * xf * accel_factor);
  const float old_yw = (float)std::fmin(1.0, yvs * yvs * g.vel_thresh * yf +
                                        high_y * yacc * yacc * g.accel_thresh * (a_stick ? xf : yf) * accel_factor);
  xo = w.out_x * old_xw + x * (1 - old_xw);
  yo = w.out_y * old_yw + y * (1 - old_yw);
  w.old_x = x; w.old_y = y;
  w.old_vx = xvel; w.old_vy = yvel;
  w.out_x = xo; w.out_y = yo;
}

// An uncalibrated stick: the stock reading through the same scaling and snapping.
void remap_uncalibrated(float x, float y, float& xo, float& yo, const Settings& s, Stick which) {
  StickParams identity;
  for (int i = 0; i < kNotches; i++) {
    identity.affine[i][0] = 1; identity.affine[i][3] = 1;
    identity.boundary[i] = -kPi + 2 * kPi * (float)i / kNotches;
  }
  notch_remap(x, y, xo, yo, identity, -1, s, which);
}

void set_hysteresis(uint8_t& out, float remapped) {
  const float hyst = 0.3f;
  const float diff = (remapped + kFloatOrigin) - out;
  if (diff > (1.0f + hyst) || diff < -hyst) out = (uint8_t)(remapped + kFloatOrigin);
}

}  // namespace

StickCal::StickCal() {
  for (int i = 0; i < kNotches; i++) notch_angles[i] = default_angle(i);
}

void soft_reset(Settings& s) {
  const bool enabled = s.enabled;
  const StickCal a = s.a_cal, c = s.c_cal;
  s = Settings{};
  s.enabled = enabled;
  s.a_cal = a;
  s.c_cal = c;
}
void hard_reset(Settings& s) {
  const bool enabled = s.enabled;
  s = Settings{};
  s.enabled = enabled;
}

void clamp(Settings& s) {
  auto c = [](int& v, int lo, int hi) { v = std::clamp(v, lo, hi); };
  c(s.l_mode, kTriggerModeMin, kTriggerModeMax); c(s.r_mode, kTriggerModeMin, kTriggerModeMax);
  c(s.l_offset, kTriggerOffsetMin, kTriggerOffsetMax); c(s.r_offset, kTriggerOffsetMin, kTriggerOffsetMax);
  c(s.x_snapback, kSnapbackMin, kSnapbackMax); c(s.y_snapback, kSnapbackMin, kSnapbackMax);
  c(s.ax_waveshaping, kWaveshapingMin, kWaveshapingMax); c(s.ay_waveshaping, kWaveshapingMin, kWaveshapingMax);
  c(s.cx_waveshaping, kWaveshapingMin, kWaveshapingMax); c(s.cy_waveshaping, kWaveshapingMin, kWaveshapingMax);
  c(s.ax_smoothing, kSmoothingMin, kSmoothingMax); c(s.ay_smoothing, kSmoothingMin, kSmoothingMax);
  c(s.cx_smoothing, kSmoothingMin, kSmoothingMax); c(s.cy_smoothing, kSmoothingMin, kSmoothingMax);
  c(s.a_cardinal, kCardinalMin, kCardinalMax); c(s.c_cardinal, kCardinalMin, kCardinalMax);
  c(s.a_scaler, kScalerMin, kScalerMax); c(s.c_scaler, kScalerMin, kScalerMax);
  c(s.tourney, kTourneyMin, kTourneyMax);
  for (StickCal* cal : {&s.a_cal, &s.c_cal})
    for (int i = 0; i < kCalPoints; i++) {
      if (!std::isfinite(cal->points_x[i]) || !std::isfinite(cal->points_y[i])) { *cal = StickCal{}; break; }
    }
}

namespace {
struct IntField { const char* key; int Settings::*field; };
constexpr IntField kIntFields[] = {
    {"l_mode", &Settings::l_mode}, {"r_mode", &Settings::r_mode},
    {"l_offset", &Settings::l_offset}, {"r_offset", &Settings::r_offset},
    {"x_snapback", &Settings::x_snapback}, {"y_snapback", &Settings::y_snapback},
    {"ax_waveshaping", &Settings::ax_waveshaping}, {"ay_waveshaping", &Settings::ay_waveshaping},
    {"cx_waveshaping", &Settings::cx_waveshaping}, {"cy_waveshaping", &Settings::cy_waveshaping},
    {"ax_smoothing", &Settings::ax_smoothing}, {"ay_smoothing", &Settings::ay_smoothing},
    {"cx_smoothing", &Settings::cx_smoothing}, {"cy_smoothing", &Settings::cy_smoothing},
    {"a_cardinal", &Settings::a_cardinal}, {"c_cardinal", &Settings::c_cardinal},
    {"a_scaler", &Settings::a_scaler}, {"c_scaler", &Settings::c_scaler},
    {"tourney", &Settings::tourney},
};
constexpr const char* kRemapKeys[REMAP_COUNT] = {"remap_a", "remap_b", "remap_du", "remap_l", "remap_r", "remap_x", "remap_y", "remap_z"};
}  // namespace

std::string serialize(const Settings& s) {
  std::ostringstream o;
  o.precision(9);
  o << "# Melee Unlocked PhobGCC settings\n";
  o << "enabled " << (s.enabled ? 1 : 0) << "\n";
  for (const IntField& f : kIntFields) o << f.key << " " << s.*(f.field) << "\n";
  for (int i = 0; i < REMAP_COUNT; i++) o << kRemapKeys[i] << " " << (int)s.remap[i] << "\n";
  const StickCal* cals[2] = {&s.a_cal, &s.c_cal};
  const char* names[2] = {"a", "c"};
  for (int k = 0; k < 2; k++) {
    if (!cals[k]->measured) continue;
    o << names[k] << "_points_x";
    for (float v : cals[k]->points_x) o << " " << v;
    o << "\n" << names[k] << "_points_y";
    for (float v : cals[k]->points_y) o << " " << v;
    o << "\n" << names[k] << "_notch_angles";
    for (float v : cals[k]->notch_angles) o << " " << v;
    o << "\n";
  }
  return o.str();
}

void parse(const std::string& text, Settings& s) {
  std::istringstream in(text);
  std::string line;
  StickCal* cals[2] = {&s.a_cal, &s.c_cal};
  const char* names[2] = {"a", "c"};
  int have[2] = {0, 0};
  StickCal read[2] = {*cals[0], *cals[1]};
  while (std::getline(in, line)) {
    std::istringstream ls(line);
    std::string key;
    if (!(ls >> key) || key[0] == '#') continue;
    if (key == "enabled") { int v; if (ls >> v) s.enabled = v != 0; continue; }
    bool done = false;
    for (const IntField& f : kIntFields) if (key == f.key) { int v; if (ls >> v) s.*(f.field) = v; done = true; }
    for (int i = 0; i < REMAP_COUNT && !done; i++) if (key == kRemapKeys[i]) { int v; if (ls >> v) s.remap[i] = (uint8_t)v; done = true; }
    for (int k = 0; k < 2 && !done; k++) {
      const std::string n = names[k];
      float* dst = nullptr; int count = 0, bit = 0;
      if (key == n + "_points_x") { dst = read[k].points_x; count = kCalPoints; bit = 1; }
      else if (key == n + "_points_y") { dst = read[k].points_y; count = kCalPoints; bit = 2; }
      else if (key == n + "_notch_angles") { dst = read[k].notch_angles; count = kNotches; bit = 4; }
      if (!dst) continue;
      int i = 0;
      while (i < count && (ls >> dst[i])) i++;
      if (i == count) have[k] |= bit;
      done = true;
    }
  }
  for (int k = 0; k < 2; k++)
    if (have[k] == 7) { read[k].measured = true; *cals[k] = read[k]; }
  clamp(s);
}

float linearize(float point, const float coeffs[4]) {
  return coeffs[0] * (point * point * point) + coeffs[1] * (point * point) + coeffs[2] * point + coeffs[3];
}

void calc_stick_values(float angle, float& x, float& y) {
  x = 100 * std::atan2(std::sin(kMaxStickAngle) * std::cos(angle), std::cos(kMaxStickAngle)) / kMaxStickAngle;
  y = 100 * std::atan2(std::sin(kMaxStickAngle) * std::sin(angle), std::cos(kMaxStickAngle)) / kMaxStickAngle;
}

void angle_on_sphere(float x, float y, float& angle) {
  const float xx = std::sin(x * kMaxStickAngle / 100) * std::cos(y * kMaxStickAngle / 100);
  const float yy = std::cos(x * kMaxStickAngle / 100) * std::sin(y * kMaxStickAngle / 100);
  angle = std::atan2(yy, xx);
  if (angle < 0) angle += 2 * kPi;
}

void params_from_cal(const float points_x[], const float points_y[], const float notch_angles[], NotchStatus status[],
                     StickParams& out) {
  out = StickParams{};
  float cleaned_x[kNotches + 1], cleaned_y[kNotches + 1], notch_x[kNotches + 1], notch_y[kNotches + 1];
  clean_cal_points(points_x, points_y, notch_angles, cleaned_x, cleaned_y, notch_x, notch_y, status);
  linearize_cal(cleaned_x, cleaned_y, cleaned_x, cleaned_y, out);
  if (!out.calibrated()) return;
  notch_calibrate(cleaned_x, cleaned_y, notch_x, notch_y, out);
}

void notch_remap(float x_in, float y_in, float& x_out, float& y_out, const StickParams& p, int cal_step,
                 const Settings& s, Stick which) {
  float angle = std::atan2(y_in, x_in);
  if (angle < p.boundary[0]) angle += kPi * 2;
  int region = kNotches - 1;
  for (int i = 1; i < kNotches; i++) {
    if (angle < p.boundary[i]) { region = i - 1; break; }
  }
  x_out = p.affine[region][0] * x_in + p.affine[region][1] * y_in;
  y_out = p.affine[region][2] * x_in + p.affine[region][3] * y_in;
  const float scale = (which == ASTICK ? s.a_scaler : s.c_scaler) / 100.0f;
  x_out *= scale;
  y_out *= scale;
  if (cal_step != -1) return;
  const int snapping = which == ASTICK ? s.a_cardinal : s.c_cardinal;
  if (snapping > 0) {
    if (std::fabs(x_out) < snapping + 0.5f && std::fabs(y_out) >= 79.5f) x_out = 0;
    if (std::fabs(y_out) < snapping + 0.5f && std::fabs(x_out) >= 79.5f) y_out = 0;
  } else if (snapping < 0) {
    // firmware: -1 prevents 1.0 cardinals on vanilla and UCF before 0.84 (and on 0.84 except Zelda
    // teleport), -2 on vanilla and all UCF; neither prevents it on "Melee 1.03"
    const float zone = snapping == -2 ? 6.5f : 0.5f;
    const float push = snapping == -2 ? 7.0f : 1.0f;
    if (std::fabs(x_out) < zone && std::fabs(y_out) >= 79.5f) x_out = x_out >= 0 ? push : -push;
    if (std::fabs(y_out) < zone && std::fabs(x_out) >= 79.5f) y_out = y_out >= 0 ? push : -push;
  }
  if (std::fabs(x_out) < 3 && std::fabs(y_out) < 3) { x_out = 0; y_out = 0; }
}

void prepare(const Settings& s, Prepared& out) {
  out.s = s;
  clamp(out.s);
  NotchStatus status[kNotches];
  out.a = StickParams{};
  out.c = StickParams{};
  if (out.s.a_cal.measured) params_from_cal(out.s.a_cal.points_x, out.s.a_cal.points_y, out.s.a_cal.notch_angles, status, out.a);
  if (out.s.c_cal.measured) params_from_cal(out.s.c_cal.points_x, out.s.c_cal.points_y, out.s.c_cal.notch_angles, status, out.c);
  recompute_gains(out.s, out.g);
}

Output step(const Prepared& p, PortState& st, const HardwareInput& in, int steps) {
  const Settings& s = p.s;
  const Gains& g = p.g;
  if (steps < 1) steps = 1;
  const bool a_cal = p.a.calibrated(), c_cal = p.c.calibrated();
  const float raw_ax = in.ax / 255.0f, raw_ay = in.ay / 255.0f, raw_cx = in.cx / 255.0f, raw_cy = in.cy / 255.0f;
  // The linearized reading (readSticks' xZ, yZ, posCx, posCy).
  const float lin_ax = a_cal ? linearize(raw_ax, p.a.fit_x) : (float)((int)in.ax - (int)in.ax0);
  const float lin_ay = a_cal ? linearize(raw_ay, p.a.fit_y) : (float)((int)in.ay - (int)in.ay0);
  const float lin_cx = c_cal ? linearize(raw_cx, p.c.fit_x) : (float)((int)in.cx - (int)in.cx0);
  const float lin_cy = c_cal ? linearize(raw_cy, p.c.fit_y) : (float)((int)in.cy - (int)in.cy0);
  if (!st.primed) {
    // The firmware's statics start at power-on with the stick at rest; start from this reading so
    // connecting a controller is not seen as a flick.
    st = PortState{};
    st.k_xpos = st.x_pos_filt = st.smooth_ax = lin_ax;
    st.k_ypos = st.y_pos_filt = st.smooth_ay = lin_ay;
    st.wave_a.old_x = st.wave_a.out_x = lin_ax; st.wave_a.old_y = st.wave_a.out_y = lin_ay;
    st.wave_c.old_x = st.wave_c.out_x = st.smooth_cx = lin_cx;
    st.wave_c.old_y = st.wave_c.out_y = st.smooth_cy = lin_cy;
    st.primed = true;
  }

  // ---- processButtons ----
  const uint16_t hw = in.buttons;
  auto held = [hw](uint16_t bit) { return (hw & bit) != 0; };
  const uint8_t source = (uint8_t)(held(PAD_A) << A_REMAP | held(PAD_B) << B_REMAP | held(PAD_UP) << D_REMAP |
                                   held(PAD_L) << L_REMAP | held(PAD_R) << R_REMAP | held(PAD_X) << X_REMAP |
                                   held(PAD_Y) << Y_REMAP | held(PAD_Z) << Z_REMAP);
  // applyRemaps: everything but A B Du L R X Y Z passes through
  uint16_t base = hw & (uint16_t)~(PAD_A | PAD_B | PAD_UP | PAD_L | PAD_R | PAD_X | PAD_Y | PAD_Z);
  auto out_bit = [&](Remap r, uint16_t bit) { if (source & s.remap[r]) base |= bit; };
  out_bit(A_REMAP, PAD_A); out_bit(B_REMAP, PAD_B); out_bit(D_REMAP, PAD_UP); out_bit(L_REMAP, PAD_L);
  out_bit(R_REMAP, PAD_R); out_bit(X_REMAP, PAD_X); out_bit(Y_REMAP, PAD_Y); out_bit(Z_REMAP, PAD_Z);

  const int shutoff_la = s.remap[L_REMAP] == (1 << L_REMAP) ? 1 : 0;
  const int shutoff_ra = s.remap[R_REMAP] == (1 << R_REMAP) ? 1 : 0;
  const float scale_l = (0.0112f * s.l_offset) + 0.4494f;   // firmware: mode 6's multiplier
  const float scale_r = (0.0112f * s.r_offset) + 0.4494f;
  auto read_trigger = [](uint8_t travel, float scale) { return (int)std::fmin(255, std::fmax(0, (float)travel) * scale); };
  auto trigger_mode = [&](int mode, uint8_t travel, int offset, int shutoff, float scale, uint16_t click, uint16_t& b) {
    const bool pressed = (b & click) != 0;
    const int read = read_trigger(travel, 1);
    switch (mode) {
      case 1: return (uint8_t)0;   // Digital Only
      case 2: b &= (uint16_t)~click; return (uint8_t)(read * shutoff);   // Analog Only
      case 3: return pressed ? (uint8_t)255 : (uint8_t)std::fmin(offset, read * shutoff);   // Trigger Plug Emulation
      case 4: b &= (uint16_t)~click; return pressed ? (uint8_t)std::min(offset, 255) : (uint8_t)0;   // Digital => Analog Value
      case 5: return pressed ? (uint8_t)std::min(offset, 255) : (uint8_t)0;   // Digital => Analog Value + Digital
      case 6: return (uint8_t)(read_trigger(travel, scale) * shutoff);   // Scales Analog Trigger Values
      case 7: b &= (uint16_t)~click; return (uint8_t)std::fmin(offset, read * shutoff);   // plug emulation, no digital
      case 8: return pressed ? (uint8_t)255 : (uint8_t)std::fmin(offset, 2.5 * read * shutoff);   // plug emulation, high gain
      default: return (uint8_t)(read * shutoff);   // Default
    }
  };

  Output out;
  for (int n = 0; n < steps; n++) {
    uint16_t b = base;
    const uint8_t la = trigger_mode(s.l_mode, in.la, s.l_offset, shutoff_la, scale_l, PAD_L, b);
    const uint8_t ra = trigger_mode(s.r_mode, in.ra, s.r_offset, shutoff_ra, scale_r, PAD_R, b);
    uint8_t out_la = la, out_ra = ra;
    // Tourney lockouts, counted in the firmware's 1 ms loops.
    const bool temp_du = (b & PAD_UP) != 0;
    if (s.tourney >= 3 && held(PAD_START)) {
      if (st.start_lockout > 0) { st.start_lockout--; b &= (uint16_t)~PAD_START; }
      else b |= PAD_START;
      if (held(PAD_DOWN)) { b |= PAD_START; b &= (uint16_t)~PAD_DOWN; }
    } else if (st.start_lockout < 1000) {
      st.start_lockout++;
    }
    if ((s.tourney == 1 || s.tourney == 4) && held(PAD_UP)) {
      if (st.du_lockout > 0) { st.du_lockout--; b &= (uint16_t)~PAD_UP; }
      else b |= PAD_UP;
      if (held(PAD_START)) { b |= PAD_UP; b &= (uint16_t)~PAD_START; }
    } else if (st.du_lockout < 1000) {
      st.du_lockout++;
    }
    if (s.tourney == 2 || s.tourney == 5) {
      if (held(PAD_START) && temp_du) { b |= PAD_UP; b &= (uint16_t)~PAD_START; }
      else b &= (uint16_t)~PAD_UP;
    }
    // firmware: here we make sure LRAS actually operate
    if (held(PAD_L) && held(PAD_R) && held(PAD_A) && held(PAD_START)) {
      b |= PAD_L | PAD_R | PAD_A | PAD_START;
      out_la = 255; out_ra = 255;
    }

    // ---- readSticks ----
    run_kalman(st, lin_ax, lin_ay, s, g);
    float shaped_ax = st.x_pos_filt, shaped_ay = st.y_pos_filt;
    run_waveshaping(st.wave_a, shaped_ax, shaped_ay, shaped_ax, shaped_ay, s.ax_waveshaping, s.ay_waveshaping, g, true);
    const float pos_ax = g.ax_smooth * shaped_ax + (1 - g.ax_smooth) * st.smooth_ax;
    const float pos_ay = g.ay_smooth * shaped_ay + (1 - g.ay_smooth) * st.smooth_ay;
    st.smooth_ax = pos_ax;
    st.smooth_ay = pos_ay;
    float pos_cx, pos_cy;
    run_waveshaping(st.wave_c, lin_cx, lin_cy, pos_cx, pos_cy, s.cx_waveshaping, s.cy_waveshaping, g, false);
    const float old_cx = st.smooth_cx, old_cy = st.smooth_cy;
    st.smooth_cx = g.cx_smooth * pos_cx + (1 - g.cx_smooth) * old_cx;
    st.smooth_cy = g.cy_smooth * pos_cy + (1 - g.cy_smooth) * old_cy;
    pos_cx = st.smooth_cx;
    pos_cy = st.smooth_cy;

    float rax, ray, rcx, rcy, uax, uay, ucx, ucy;
    if (a_cal) {
      notch_remap(pos_ax, pos_ay, rax, ray, p.a, -1, s, ASTICK);
      notch_remap(lin_ax, lin_ay, uax, uay, p.a, 1, s, ASTICK);   // firmware: unfiltered, no snapping
    } else {
      remap_uncalibrated(pos_ax, pos_ay, rax, ray, s, ASTICK);
      uax = lin_ax; uay = lin_ay;
    }
    if (c_cal) {
      notch_remap(pos_cx, pos_cy, rcx, rcy, p.c, -1, s, CSTICK);
      notch_remap(lin_cx, lin_cy, ucx, ucy, p.c, 1, s, CSTICK);
    } else {
      remap_uncalibrated(pos_cx, pos_cy, rcx, rcy, s, CSTICK);
      ucx = lin_cx; ucy = lin_cy;
    }
    auto clamp125 = [](float v) { return std::fmin(125.0f, std::fmax(-125.0f, v)); };
    rax = clamp125(rax); ray = clamp125(ray); rcx = clamp125(rcx); rcy = clamp125(rcy);
    st.ax_unfilt = clamp125(uax); st.ay_unfilt = clamp125(uay);
    st.cx_unfilt = clamp125(ucx); st.cy_unfilt = clamp125(ucy);
    set_hysteresis(st.ax, rax);
    set_hysteresis(st.ay, ray);
    set_hysteresis(st.cx, rcx);
    set_hysteresis(st.cy, rcy);

    out.buttons = b;
    out.trig_l = out_la;
    out.trig_r = out_ra;
  }
  out.stick_x = (int8_t)((int)st.ax - 127);
  out.stick_y = (int8_t)((int)st.ay - 127);
  out.sub_x = (int8_t)((int)st.cx - 127);
  out.sub_y = (int8_t)((int)st.cy - 127);
  return out;
}

// The settings notchRemap sees while calibrating: only the radius scaling acts at a calibration step.
static Settings cal_settings(const CalSession& c) {
  Settings s;
  s.a_scaler = s.c_scaler = c.scaler;
  return s;
}

void cal_begin(CalSession& c, Stick which, const StickCal& saved, int scaler) {
  c = CalSession{};
  c.which = which;
  c.scaler = std::clamp(scaler, kScalerMin, kScalerMax);
  c.step = 0;
  std::memcpy(c.notch_angles, saved.notch_angles, sizeof c.notch_angles);
  std::memcpy(c.saved_angles, saved.notch_angles, sizeof c.saved_angles);
  for (int i = 0; i < kNotches; i++) { c.measured_angles[i] = default_angle(i); c.status[i] = kNotchStatusDefaults[i]; }
  if (saved.measured) params_from_cal(saved.points_x, saved.points_y, saved.notch_angles, c.status, c.params);
  for (int i = 0; i < kNotches; i++) c.status[i] = kNotchStatusDefaults[i];
}

bool cal_begin_notch_fix(CalSession& c, Stick which, const StickCal& saved, int scaler) {
  if (!saved.measured) return false;
  cal_begin(c, which, saved, scaler);
  std::memcpy(c.temp_x, saved.points_x, sizeof c.temp_x);
  std::memcpy(c.temp_y, saved.points_y, sizeof c.temp_y);
  c.step = kCalPoints;   // calibrationSkipMeasurement
  apply_cal_from_points(c.saved_angles, c.notch_angles, c.temp_x, c.temp_y, c.status, c.measured_angles, c.params,
                        cal_settings(c), which);
  return true;
}

bool cal_advance(CalSession& c, float raw_x, float raw_y, StickCal& saved) {
  if (c.step < 0) return false;
  if (c.step < kCalPoints) {
    const int idx = kCalOrder[c.step];
    c.temp_x[idx] = raw_x;
    c.temp_y[idx] = raw_y;
  }
  c.step++;
  if (c.step == kCalPoints) {
    legalize_notches(c.step, c.measured_angles, c.notch_angles);
    apply_cal_from_points(c.saved_angles, c.notch_angles, c.temp_x, c.temp_y, c.status, c.measured_angles, c.params,
                          cal_settings(c), c.which);
  }
  while (c.step >= kCalPoints && c.step < kCalSteps &&
         c.status[kNotchAdjOrder[std::min(c.step - kCalPoints, kAdjNotches - 1)]] == TERT_INACTIVE) {
    legalize_notches(c.step, c.measured_angles, c.notch_angles);
    c.step++;
  }
  if (c.step >= kCalSteps) {
    saved.measured = true;
    std::memcpy(saved.points_x, c.temp_x, sizeof saved.points_x);
    std::memcpy(saved.points_y, c.temp_y, sizeof saved.points_y);
    std::memcpy(saved.notch_angles, c.notch_angles, sizeof saved.notch_angles);
    params_from_cal(c.temp_x, c.temp_y, c.notch_angles, c.status, c.params);
    c.step = -1;
    return true;
  }
  return false;
}

void cal_undo(CalSession& c) {
  if (c.step % 2 == 0 && c.step < kCalPoints && c.step != 0) c.step -= 2;
  else if (c.step % 2 == 1 && c.step < kCalPoints && c.step != 0) c.step = std::max(c.step - 3, 0);
  else if (c.step > kCalPoints) c.step--;
  while (c.step >= kCalPoints && c.step < kCalSteps &&
         c.status[kNotchAdjOrder[std::min(c.step - kCalPoints, kAdjNotches - 1)]] == TERT_INACTIVE)
    c.step--;
}

void cal_adjust(CalSession& c, bool cw, bool ccw, bool reset, float dt_ms) {
  if (c.step < kCalPoints || c.step >= kCalSteps) return;
  const int idx = kNotchAdjOrder[c.step - kCalPoints];
  if (c.status[idx] == TERT_INACTIVE) return;
  if (cw && !ccw) c.notch_angles[idx] -= dt_ms * 0.000075f;
  else if (ccw && !cw) c.notch_angles[idx] += dt_ms * 0.000075f;
  else if (reset) c.notch_angles[idx] = c.measured_angles[idx];
  else return;
  legalize_notches(c.step, c.measured_angles, c.notch_angles);
  // firmware main loop: clean the full cal points again with the new angles, linearize, notchCalibrate
  float cleaned_x[kNotches + 1], cleaned_y[kNotches + 1], notch_x[kNotches + 1], notch_y[kNotches + 1];
  clean_cal_points(c.temp_x, c.temp_y, c.notch_angles, cleaned_x, cleaned_y, notch_x, notch_y, c.status);
  linearize_cal(cleaned_x, cleaned_y, cleaned_x, cleaned_y, c.params);
  notch_calibrate(cleaned_x, cleaned_y, notch_x, notch_y, c.params);
}

CalPrompt cal_prompt(int step) {
  if (step < 0 || step >= kCalPoints) return {-1, false, false};
  const int idx = kCalOrder[step];
  const int notch = idx / 2;
  return {notch, idx % 2 == 0, notch % 2 == 1};
}

int cal_adjust_notch(int step) {
  if (step < kCalPoints || step >= kCalSteps) return -1;
  return kNotchAdjOrder[step - kCalPoints];
}

// firmware (drawMenu.cpp): meleeCoordClamp
void melee_coords(int x, int y, float& mx, float& my) {
  const float magnitude = std::sqrt((float)x * x + (float)y * y);
  const float scale = magnitude > 0 ? std::fmin(1.0f, 80.0f / magnitude) : 1.0f;
  mx = std::trunc(x * scale) / 80.0f;
  my = std::trunc(y * scale) / 80.0f;
}

}  // namespace host::phob
