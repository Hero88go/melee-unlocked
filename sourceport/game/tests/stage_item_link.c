#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
typedef struct HSD_GObj { struct HSD_GObj* next; void* user_data; } HSD_GObj;
typedef struct Item { int kind; struct { struct { HSD_GObj* x20; } whitebea; } xDD4_itemVar; } Item;
enum { It_Kind_Freeze = 17 };
static HSD_GObj* heads[16];
static HSD_GObj** HSD_GObjPLinkHead = heads;
#include "mu_whitebea_block_alive_body.h"
#include "mu_whitebea_validate_block_body.h"
int main(void)
{
    Item owner = {0}, block_data = {It_Kind_Freeze};
    HSD_GObj block = {NULL,&block_data}; heads[9] = &block;
    owner.xDD4_itemVar.whitebea.x20 = (HSD_GObj*)0x131;
    mu_whitebea_validate_block(&owner); assert(owner.xDD4_itemVar.whitebea.x20 == NULL);
    owner.xDD4_itemVar.whitebea.x20 = &block;
    mu_whitebea_validate_block(&owner); assert(owner.xDD4_itemVar.whitebea.x20 == &block);
    block_data.kind = 1; mu_whitebea_validate_block(&owner); assert(owner.xDD4_itemVar.whitebea.x20 == NULL);
    block_data.kind = It_Kind_Freeze; owner.xDD4_itemVar.whitebea.x20 = &block;
    heads[9] = NULL; mu_whitebea_validate_block(&owner); assert(owner.xDD4_itemVar.whitebea.x20 == NULL);
    return 0;
}
