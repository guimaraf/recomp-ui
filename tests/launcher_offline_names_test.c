#include "launcher_model.h"
#include <stdio.h>
#include <string.h>

void launcher_binds_set_zapper(int mouse_enabled, int crosshair) {
    (void)mouse_enabled;
    (void)crosshair;
}

static int failures, save_calls, refuse_save;
static char saved[2][64];
static void check(int ok, const char* label) {
    if (!ok) { fprintf(stderr, "FAIL: %s\n", label); ++failures; }
}
static int save_names(const char* left, const char* right, char* error, size_t cap) {
    ++save_calls;
    if (refuse_save) {
        snprintf(error, cap, "Folder is read-only.");
        return -1;
    }
    snprintf(saved[0], sizeof(saved[0]), "%s", left);
    snprintf(saved[1], sizeof(saved[1]), "%s", right);
    return 0;
}

int main(void) {
    LauncherModel model;
    RecompLauncherCSettings settings = {0}, out = {0}, defaults = {0};
    RecompLauncherCGameInfo game = {0};
    settings.player_nameplates = 1;
    snprintf(settings.offline_player_names[0], 64, "Fred");
    snprintf(settings.offline_player_names[1], 64, "Felipe");
    snprintf(settings.netplay_player_name, sizeof(settings.netplay_player_name), "Online");
    game.name = "Offline names test";
    launcher_model_init(&model, &settings, &game, NULL);
    check(!model.save_offline_player_names_cb, "unrelated hosts do not offer offline editor");
    check(!launcher_model_save_offline_player_names(&model), "missing callback cannot save");

    game.save_offline_player_names = save_names;
    game.default_settings = &defaults;
    launcher_model_init(&model, &settings, &game, NULL);
    check(!strcmp(model.offline_player_names_edit[0], "Fred"), "draft starts from saved name");
    snprintf(model.offline_player_names_edit[0], 64, "Maria");
    launcher_model_commit(&model, &out);
    check(!strcmp(out.offline_player_names[0], "Fred"), "Play/Quit cannot promote unsaved edits");
    check(save_calls == 0, "typing/committing never saves implicitly");

    refuse_save = 1;
    check(!launcher_model_save_offline_player_names(&model), "failed write is reported");
    check(model.offline_player_names_save_status == -1 &&
          !strcmp(model.offline_player_names_error, "Folder is read-only."), "host error reaches UI");
    launcher_model_commit(&model, &out);
    check(!strcmp(out.offline_player_names[0], "Fred"), "failed Save keeps last saved name");
    check(!strcmp(model.offline_player_names_edit[0], "Maria"), "failed Save keeps draft for retry");

    refuse_save = 0;
    check(launcher_model_save_offline_player_names(&model), "successful Save is acknowledged");
    check(model.offline_player_names_save_status == 1, "success message is enabled");
    check(!strcmp(saved[0], "Maria") && !strcmp(saved[1], "Felipe"), "both names reach host");
    launcher_model_commit(&model, &out);
    check(!strcmp(out.offline_player_names[0], "Maria"), "successful Save promotes draft");
    check(!strcmp(out.netplay_player_name, "Online"), "Save preserves online name");
    model.s.player_nameplates = -1;
    launcher_model_commit(&model, &out);
    check(!strcmp(out.offline_player_names[0], "Maria"), "hiding bars retains saved names");
    launcher_model_restore_defaults(&model);
    check(!strcmp(model.s.offline_player_names[0], "Maria"), "Restore defaults retains explicit Save");

    model.offline_player_names_edit[0][0] = '\0';
    model.offline_player_names_edit[1][0] = '\0';
    check(launcher_model_save_offline_player_names(&model), "empty names can restore slot fallbacks");
    check(!model.s.offline_player_names[0][0] && !model.s.offline_player_names[1][0], "empty Save clears both names");
    if (failures) return 1;
    puts("launcher_offline_names_test: ok");
    return 0;
}
