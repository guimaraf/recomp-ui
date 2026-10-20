#ifndef RECOMP_UI_PSX_NAMEPLATES_H
#define RECOMP_UI_PSX_NAMEPLATES_H
#ifdef __cplusplus
extern "C" {
#endif
struct PsxHostNameplates;
/* GL context current; independent of whether the in-game menu is available. */
int recomp_ui_psx_nameplates_init(const char *assets_dir);
void recomp_ui_psx_nameplates_draw(const struct PsxHostNameplates *labels,
                                  int width, int height, const int game_rect[4]);
void recomp_ui_psx_nameplates_release(void);
#ifdef __cplusplus
}
#endif
#endif
