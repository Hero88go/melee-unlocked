/* Native VS CPU selector, included after the CSS helpers and private door types. */
int mu_online_abi_command(unsigned int, const unsigned char*, unsigned int,
                          unsigned char*, unsigned int, unsigned int*);
static struct {
    HSD_Text* menu;
    HSD_Text* names[4];
    int drawn[4];
    int door, owner, choice, repeat;
    float box[4];
} mu_vs_cpu = { .door = -1 };

static const char* mu_vs_cpu_names[7] = {
    "Human", "20XX CPU", "Mang0", "Cody Schwab", "Zain", "aMSa", "Closed"
};
static const int mu_vs_cpu_pros[7] = { -1, -1, 0, 3, 1, 4, -1 };
static const int mu_vs_cpu_characters[7] = { -1, -1, 22, 1, 18, 14, -1 };

static int mu_vs_cpu_read(unsigned char* pros)
{
    unsigned int size = 0;
    unsigned char reply[4096];
    if (mu_online_abi_command(0xC8, NULL, 0, reply, sizeof reply, &size) != 0 || size != 4) return 0;
    memcpy(pros, reply, 4);
    return 1;
}

static void mu_vs_cpu_reset(void)
{
    int port;
    mu_vs_cpu.menu = NULL;
    mu_vs_cpu.door = -1;
    mu_vs_cpu.repeat = 0;
    for (port = 0; port < 4; ++port) {
        mu_vs_cpu.names[port] = NULL;
        mu_vs_cpu.drawn[port] = -99;
    }
}

static void mu_vs_cpu_backdrop(void* object)
{
    GXColor edge = { 222, 198, 108, 255 };
    GXColor panel = { 18, 24, 34, 255 };
    GXColor selected = { 112, 88, 24, 255 };
    float row[4];
    (void) object;
    GXSetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_C0);
    GXSetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_ZERO, GX_CA_ZERO, GX_CA_A0);
    GXSetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    GXSetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_FALSE, GX_TEVPREV);
    mu_f4_caption_quad(mu_vs_cpu.box, 0.0f, edge);
    mu_f4_caption_quad(mu_vs_cpu.box, 0.15f, panel);
    memcpy(row, mu_vs_cpu.box, sizeof row);
    row[1] -= 1.7f + 1.65f * mu_vs_cpu.choice;
    row[3] = row[1] - 1.6f;
    mu_f4_caption_quad(row, 0.2f, selected);
}

static void mu_vs_cpu_menu(void)
{
    int row;
    HSD_Text* text = mu_vs_cpu.menu;
    const float left = mu_vs_cpu.box[0] + 0.6f;
    const float top = mu_vs_cpu.box[1] - 0.6f;
    GXColor white = { 244, 244, 244, 255 };
    GXColor gold = { 255, 222, 84, 255 };
    if (text == NULL) {
        text = HSD_SisLib_803A6754(0, 0);
        mu_vs_cpu.menu = text;
        text->default_kerning = 1;
        text->default_alignment = 0;
        text->font_size.x = text->font_size.y = 0.045f;
        text->render_callback = mu_vs_cpu_backdrop;
        HSD_SisLib_803A6B98(text, 0, 0, "PLAYER TYPE");
        for (row = 0; row < 7; ++row)
            HSD_SisLib_803A6B98(text, 0, 38.0f + row * 36.7f, "%s", mu_vs_cpu_names[row]);
    }
    text->pos_x = left;
    text->pos_y = -top;
    text->hidden = 0;
    for (row = 0; row < 7; ++row)
        HSD_SisLib_803A74F0(text, row + 1, row == mu_vs_cpu.choice ? &gold : &white);
}

static void mu_vs_cpu_labels(void)
{
    unsigned char pros[4];
    int port, choice;
    if (!mu_vs_cpu_read(pros)) return;
    for (port = 0; port < 4; ++port) {
        HSD_Text* text = mu_vs_cpu.names[port];
        const int pro = mnCharSel_803F0DFC.doors[port].p_kind == 1 ? pros[port] : 255;
        const char* name = NULL;
        for (choice = 2; choice < 6; ++choice)
            if (pro == mu_vs_cpu_pros[choice]) name = mu_vs_cpu_names[choice];
        if (text != NULL && name == NULL) { text->hidden = 1; continue; }
        if (name == NULL || mnCharSel_803F0E8C[port].data == NULL) continue;
        if (text == NULL) {
            HSD_Text* original = mnCharSel_803F0E8C[port].data->text;
            text = mu_f4_marks_make();
            text->font_size = original->font_size;
            text->box_size_x = original->box_size_x;
            text->box_size_y = original->box_size_y;
            text->pos_x = original->pos_x;
            text->pos_y = original->pos_y;
            mu_vs_cpu.names[port] = text;
        }
        if (mu_vs_cpu.drawn[port] != pro) {
            GXColor gold = { 255, 222, 84, 255 };
            HSD_SisLib_803A70A0(text, 0, "%s", name);
            HSD_SisLib_803A74F0(text, 0, &gold);
            mu_vs_cpu.drawn[port] = pro;
        }
        text->hidden = 0;
    }
}

static void mu_vs_cpu_choose(void)
{
    const int port = mu_vs_cpu.door;
    const int choice = mu_vs_cpu.choice;
    unsigned char request[2], reply[4096];
    unsigned int size = 0;
    request[0] = (unsigned char) port;
    request[1] = choice == 1 ? 0xFE :
        mu_vs_cpu_pros[choice] < 0 ? 0xFF : (unsigned char) mu_vs_cpu_pros[choice];
    mu_online_abi_command(0xC8, request, sizeof request, reply, sizeof reply, &size);
    mnCharSel_803F0DFC.doors[port].p_kind = choice == 0 ? 0 : choice == 6 ? 3 : 1;
    mnCharSel_804D6CB0->vs.start.players[port].slot_type = mnCharSel_803F0DFC.doors[port].p_kind;
    if (mu_vs_cpu_characters[choice] >= 0) {
        mnCharSel_804D6CB0->vs.start.players[port].ckind = mu_vs_cpu_characters[choice];
        mnCharSel_803F0DFC.doors[port].costume = 0;
        mnCharSel_8025FDEC((u8) port);
        mnCharSel_803F0DFC.doors[port].selected_since_load = 1;
    }
    mnCharSel_8025DB34((u8) port);
    OSReport("vs cpu: P%d selected %s\n", port + 1, mu_vs_cpu_names[choice]);
    mu_vs_cpu.door = -1;
    mu_vs_cpu.menu->hidden = 1;
    mu_vs_cpu_labels();
}

static int mu_vs_cpu_hand(struct CSSCursorData* cursor)
{
    int port, row, direction = 0;
    u32 trigger;
    float dx, dy;
    if (mnCharSel_804D6CB0->match_type != VS_MELEE || mnCharSel_804D6CF5 != 4 ||
        mu_lobby_css() || mu_slippi_css_online()) return 0;
    if (cursor->x4 == 0) mu_vs_cpu_labels();
    if (mu_vs_cpu.door < 0) {
        trigger = HSD_PadCopyStatus[cursor->x4].trigger;
        if (!(trigger & HSD_PAD_A) || cursor->x5 == 1 ||
            cursor->x10 >= 0.2f || cursor->x10 <= -4.6f) return 0;
        for (port = 0; port < 4; ++port) {
            CSSDoor* door = &mnCharSel_803F0DFC.doors[port];
            if (cursor->xC <= door->togglebtn_left || cursor->xC >= door->togglebtn_right) continue;
            mu_vs_cpu.door = port;
            mu_vs_cpu.owner = cursor->x4;
            mu_vs_cpu.choice = door->p_kind == 1 ? 1 : door->p_kind == 3 ? 6 : 0;
            mu_vs_cpu.repeat = 0;
            mu_vs_cpu.box[0] = (door->togglebtn_left + door->togglebtn_right) / 2.0f - 7.0f;
            if (mu_vs_cpu.box[0] > 17.0f) mu_vs_cpu.box[0] = 17.0f;
            mu_vs_cpu.box[1] = 12.0f;
            mu_vs_cpu.box[2] = mu_vs_cpu.box[0] + 14.5f;
            mu_vs_cpu.box[3] = -2.1f;
            mu_vs_cpu_menu();
            return 1;
        }
        return 0;
    }
    if (cursor->x4 != mu_vs_cpu.owner) return 1;
    port = mu_vs_cpu.owner;
    trigger = HSD_PadCopyStatus[port].trigger;
    if (trigger & HSD_PAD_B) {
        mu_vs_cpu.door = -1;
        mu_vs_cpu.menu->hidden = 1;
        return 1;
    }
    getStickDelta(port, &dx, &dy);
    if (trigger & HSD_PAD_DPADUP) direction = -1;
    else if (trigger & HSD_PAD_DPADDOWN) direction = 1;
    else if (dy > 0.2f) direction = -1;
    else if (dy < -0.2f) direction = 1;
    if (!direction) mu_vs_cpu.repeat = 0;
    else if (mu_vs_cpu.repeat == 0 || ++mu_vs_cpu.repeat >= 12) {
        mu_vs_cpu.choice = (mu_vs_cpu.choice + direction + 7) % 7;
        mu_vs_cpu.repeat = 1;
        mu_vs_cpu_menu();
        sfxMove();
    }
    if (trigger & HSD_PAD_A) mu_vs_cpu_choose();
    for (row = 0; row < 4; ++row)
        HSD_PadCopyStatus[row].trigger &= ~(HSD_PAD_A | HSD_PAD_B | HSD_PAD_START);
    return 1;
}
