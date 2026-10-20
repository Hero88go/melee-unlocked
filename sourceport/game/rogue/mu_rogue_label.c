/* rogueMelee's character select heading, cut natively from the player's own disc.
 *
 * rogueMelee's build (tools/lib/native_menu_assets.py) makes its 96x32 I4 heading from letters of the
 * game's own main menu labels (Rumble, 1-P Mode, Training in MnMaAll.usd) and compiles the result in,
 * so no game pixels are in its source. Natively the same cut runs on the loaded disc: the main menu
 * hands over its cursor material tree when it loads MnMaAll (mnmain.c, MU_ROGUE), the label is built
 * once, and Rogue Mode's heading (rogue_mode.c RogueMode_CssHeading) shows it. Every step mirrors
 * label_image, rogue_label and generate_css_label in that file. */
#include <mu_native.h>

#include <sysdolphin/baselib/mobj.h>
#include <sysdolphin/baselib/tobj.h>

enum { W = 176, H = 30, IA4_BYTES = 5632, OUT_W = 96, OUT_H = 32 };

unsigned char mu_rogue_css_label[OUT_W * OUT_H / 2] __attribute__((aligned(32)));
static int label_ready;

/* MatAnimJoint -> MatAnim -> TexAnim image tables, joints numbered in walk order (material_images) */
struct Found { const unsigned char* pixels[20]; int count[20]; };

static int walk(HSD_MatAnimJoint* joint, int index, struct Found* found, int depth)
{
    HSD_MatAnim* material;
    int next = index + 1;
    if (depth > 64) return next;
    for (material = DP(joint->matanim); material != NULL; material = DP(material->next)) {
        HSD_TexAnim* texture;
        for (texture = DP(material->texanim); texture != NULL; texture = DP(texture->next)) {
            int k;
            if (index != 3 || DISC_NULL(texture->imagetbl)) continue;
            for (k = 0; k < texture->n_imagetbl && k < 20; ++k) {
                struct HSD_ImageDesc* image = DP(DP(texture->imagetbl)[k]);
                if (image == NULL || image->width != W || image->height != H || image->format != GX_TF_IA4) continue;
                found->pixels[k] = DP(image->image_ptr);
                found->count[k] += 1;
            }
        }
    }
    if (!DISC_NULL(joint->child)) next = walk(DP(joint->child), next, found, depth + 1);
    if (!DISC_NULL(joint->next)) next = walk(DP(joint->next), next, found, depth + 1);
    return next;
}

/* GX tiled IA4 (8x4 tiles, one byte a pixel) to row-major */
static void decode_ia4(const unsigned char* data, unsigned char* out)
{
    int pos = 0, bx, by, x, y;
    for (by = 0; by < H; by += 4)
        for (bx = 0; bx < W; bx += 8)
            for (y = 0; y < 4; ++y)
                for (x = 0; x < 8; ++x, ++pos)
                    if (bx + x < W && by + y < H) out[(by + y) * W + bx + x] = data[pos];
}

void mu_rogue_capture_label(HSD_MatAnimJoint* cursor_matanim)
{
    static const signed short glyphs[][3] = {   /* source label, left, right ("ROGUE MODE") */
        {19, 34, 55}, {0, 106, 123}, {9, 128, 145}, {19, 55, 72}, {0, 138, 157},
        {-1, 0, 0}, {0, 82, 106}, {0, 106, 123}, {0, 123, 138}, {0, 138, 157},
    };
    static unsigned char planes[3][W * H], label[W * H], plane[OUT_W * OUT_H];
    struct Found found = {0};
    int i, width = 0, cursor, x, y;
    if (label_ready || cursor_matanim == NULL || !mu_rogue_enabled()) return;
    walk(cursor_matanim, 0, &found, 0);
    if (found.count[0] != 1 || found.count[9] != 1 || found.count[19] != 1) {
        OSReport("[rogue] heading: the menu's cursor labels are not the 1.02 layout; native heading kept\n");
        return;
    }
    decode_ia4(found.pixels[0], planes[0]);
    decode_ia4(found.pixels[9], planes[1]);
    decode_ia4(found.pixels[19], planes[2]);
    for (i = 0; i < 10; ++i) width += glyphs[i][0] < 0 ? 5 : glyphs[i][2] - glyphs[i][1];
    for (i = 0; i < W * H; ++i) label[i] = 0;
    cursor = (W - width) / 2;
    for (i = 0; i < 10; ++i) {
        const unsigned char* source;
        int left = glyphs[i][1], right = glyphs[i][2];
        if (glyphs[i][0] < 0) { cursor += 5; continue; }
        source = planes[glyphs[i][0] == 0 ? 0 : glyphs[i][0] == 9 ? 1 : 2];
        for (y = 0; y < H; ++y)
            for (x = 0; x < right - left; ++x) label[y * W + cursor + x] = source[y * W + left + x];
        cursor += right - left;
    }
    /* The heading's two lines, each 16 rows tall, from the label's intensity times alpha */
    for (i = 0; i < OUT_W * OUT_H; ++i) plane[i] = 0;
    {
        static const short rows[2][3] = {{0, 2, 91}, {16, 98, 75}};   /* top, left, width */
        int r;
        for (r = 0; r < 2; ++r)
            for (y = 0; y < 16; ++y)
                for (x = 0; x < rows[r][2]; ++x) {
                    int sy, sum = 0, n = 0;
                    for (sy = y * H / 16; sy < (y + 1) * H / 16; ++sy, ++n) {
                        const unsigned char v = label[sy * W + rows[r][1] + x];
                        sum += ((v & 15) * (v >> 4) + 7) / 15;
                    }
                    plane[(rows[r][0] + y) * OUT_W + (OUT_W - rows[r][2]) / 2 + x] = (unsigned char) ((sum + n / 2) / n);
                }
    }
    /* GX tiled I4 (8x8 tiles, two pixels a byte, high nibble first) */
    {
        int pos = 0, bx, by;
        for (by = 0; by < OUT_H; by += 8)
            for (bx = 0; bx < OUT_W; bx += 8)
                for (y = 0; y < 8; ++y)
                    for (x = 0; x < 8; x += 2)
                        mu_rogue_css_label[pos++] = (unsigned char) ((plane[(by + y) * OUT_W + bx + x] << 4) |
                                                                     plane[(by + y) * OUT_W + bx + x + 1]);
    }
    label_ready = 1;
}

int mu_rogue_css_label_ready(void)
{
    return label_ready;
}
