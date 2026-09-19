/* Fixed archive bytes exercise the fields that previously compiled with the
 * wrong offsets or byte order. No game boot or disc image is needed. */
#include <melee/ft/types.h>
#include <melee/ft/kinds/ftPikachu/types.h>

int main(void)
{
    _Alignas(ftData) unsigned char data[0x60] = {
        [0x08] = 0x82, 0x00, 0x10, 0x00,
        [0x0C] = 0x82, 0x1B, 0xC2, 0x94,
        [0x14] = 0x82, 0x00, 0x20, 0x00,
    };
    _Alignas(Fighter_WaitAnimData) unsigned char animation[0x18] = {
        [0x08] = 0x00, 0x00, 0x12, 0x34,
        [0x10] = 0x40,
        [0x14] = 0x82, 0x10, 0x00, 0x00,
    };
    _Alignas(ftPikachuAttributes) unsigned char attributes[0xF8] = {
        [0x00] = 0x3F, 0x80, 0x00, 0x00,
        [0x14] = 0x00, 0x00, 0x00, 0x50,
        [0xDC] = 0x00, 0x00, 0x00, 0x51,
        [0xE0] = 0x41, 0x20, 0x00, 0x00,
        [0xE8] = 0xBF, 0x80, 0x00, 0x00,
    };
    ftData* ft = (ftData*) data;
    Fighter_WaitAnimData* entry = (Fighter_WaitAnimData*) animation;
    struct ftData_80085FD4_ret* view = (void*) animation;
    ftPikachuAttributes copy = *(ftPikachuAttributes*) attributes;
    ftCollisionBox box = ftCollisionBox_FromDisc(&copy.height_attributes);
    Fighter fighter = {0};
    _Alignas(CmdUnion) unsigned char command[4] = {0x54, 0x24, 0x00, 0x02};
    CmdUnion* cmd = (CmdUnion*) command;
    if (DISC_RAW(ft->xC) != 0x821BC294 ||
        DISC_RAW(ft->x14) != 0x82002000) return 1;
    if (entry->x8 != 0x1234 || view->x8 != 0x1234 ||
        !view->x10_b1 || view->x10_b0 || view->x14 != 0x82100000) return 2;
    if (copy.specialn_spawn_offset.x != 1.0f ||
        copy.specialn_itkind != 0x50 || copy.xDC != 0x51) return 3;
    if (box.top != 10.0f || box.left.x != -1.0f) return 4;
    fighter.x594_s32 = 0x40000008;
    if (!fighter.x594_b1_loop || fighter.x597_bits != 8 ||
        fighter.x594_bits != 0) return 5;
    if (((gmScriptEventDefault*) command)->opcode != 21 ||
        cmd->set_hurt_state.bone_idx != 9 || cmd->set_hurt_state.state != 2)
        return 6;
    fighter.x21FC_flag.byte = 1;
    if (!fighter.x21FC_flag.b7 || fighter.x21FC_flag.b0) return 7;
    fighter.throw_flags = 0x80000000;
    if (!fighter.throw_flags_b0 || fighter.throw_flags_b7) return 8;
    {
        FighterBone bone = {0};
        bone.flags8 = 0x8001;
        if (!bone.flags_b0 || !bone.flags2_b7 || bone.flags_b7) return 9;
    }
    return 0;
}
