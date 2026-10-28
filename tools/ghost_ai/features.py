"""The model's inputs and outputs. The game builds the same inputs (port/runtime/hle/ghost_ai.cpp);
keep the two in step, and bump VERSION when anything here changes.
"""
import numpy as np

VERSION = 1
PROS = ["mang0", "zain", "hbox", "cody"]
# Slippi stage ids of the legal stages; anything else is 0.
STAGES = {2: 1, 3: 2, 8: 3, 28: 4, 31: 5, 32: 6}
N_STATES = 512
N_CHARS = 34

# Per player, from the post-frame (columns of *_num): x y facing percent shield stocks age misc
# airborne jumps vx_air vy kbx kby vx_ground hitlag
NUM_SCALE = np.array([1 / 100, 1 / 100, 1, 1 / 100, 1 / 60, 1 / 4, 1 / 60, 1 / 60, 1, 1 / 6,
                      1 / 3, 1 / 3, 1 / 3, 1 / 3, 1 / 3, 1 / 10], np.float32)
NUM_CLIP = 6.0
MIRROR_NUM = [0, 2, 10, 12, 14]          # columns whose sign flips when the picture is mirrored
N_NUM = 16
N_FLAGS = 40
# Buttons the model presses (physical bits): A B X|Y Z L|R
BTN_BITS = [0x0100, 0x0200, 0x0C00, 0x0010, 0x0060]
N_BTN = len(BTN_BITS)
STICK_STEPS = 9                          # main stick on a 9 x 9 grid of -1 .. 1
N_STICK = STICK_STEPS * STICK_STEPS
N_CSTICK = 5                             # none, up, down, left, right
PREV = 2                                 # previous frames of its own controller
PAD_IN = 5 + N_BTN                       # stick x y, c-stick x y, trigger, buttons
N_IN = 2 * (N_NUM + N_FLAGS) + 3 + PREV * PAD_IN


def stick_class(x, y):
    qx = np.clip(np.rint(x * 4), -4, 4).astype(np.int64) + 4
    qy = np.clip(np.rint(y * 4), -4, 4).astype(np.int64) + 4
    return qx * STICK_STEPS + qy


def stick_value(c):
    return (c // STICK_STEPS - 4) / 4.0, (c % STICK_STEPS - 4) / 4.0


def cstick_class(x, y):
    c = np.zeros(np.shape(x), np.int64)
    big = np.maximum(np.abs(x), np.abs(y)) >= 0.5
    horiz = np.abs(x) > np.abs(y)
    c[big & ~horiz & (y > 0)] = 1
    c[big & ~horiz & (y <= 0)] = 2
    c[big & horiz & (x < 0)] = 3
    c[big & horiz & (x >= 0)] = 4
    return c


def btn_bits(btn, trigger):
    out = np.stack([(btn & b) != 0 for b in BTN_BITS], -1).astype(np.float32)
    out[..., 4] = np.maximum(out[..., 4], (trigger > 0.3).astype(np.float32))
    return out


def player_block(num, flags):
    """num: (..., 16) raw post-frame numbers; flags: (..., 5) uint8."""
    n = np.clip(num.astype(np.float32) * NUM_SCALE, -NUM_CLIP, NUM_CLIP)
    bits = np.unpackbits(flags.astype(np.uint8)[..., None], axis=-1, bitorder="little").reshape(*flags.shape[:-1], 40)
    return np.concatenate([n, bits.astype(np.float32)], -1)


def pad_block(pad, btn):
    p = pad.astype(np.float32)
    return np.concatenate([p, btn_bits(btn, p[..., 4])], -1)
