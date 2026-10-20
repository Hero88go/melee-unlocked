/* Native build: the heading is cut from the player's own disc at run time (rogue/mu_rogue_label.c),
 * where rogueMelee's own build generates this file from the local disc. */
extern unsigned char mu_rogue_css_label[96 * 32 / 2];
int mu_rogue_css_label_ready(void);
#define rogue_css_label mu_rogue_css_label
