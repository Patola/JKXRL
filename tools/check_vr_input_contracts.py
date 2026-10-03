#!/usr/bin/env python3
"""Structural input-boundary checks; headset interaction still needs a live test."""
from pathlib import Path
import unittest
import shutil
import subprocess
import tempfile
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'
CONSOLE = (ROOT / 'code/client/cl_console.cpp').read_text()
INPUT = (ROOT / 'code/client/cl_input.cpp').read_text()
CONTROLLERS = (ROOT / 'JKXR/VrInputDefault.cpp').read_text()


class VrInputContracts(unittest.TestCase):
    def test_spatial_console_follows_reached_teleports_only(self):
        harness = r'''
#include <cassert>
constexpr int PACKET_BACKUP=4, PACKET_MASK=3, EF_TELEPORT_BIT=4, qfalse=0;
struct clSnapshot_t {bool valid=false; int messageNum=0,serverTime=0; struct {int eFlags=0;} ps;};
struct {clSnapshot_t frame,frames[PACKET_BACKUP]; int serverTime=100;} cl;
bool visible=false,gameplay=true;
bool Con_VrPhaseVisible(){return visible;}
bool Con_VrGameplayAvailable(){return gameplay;}
struct Pointer { bool valid; } vrConsolePointers[2]={{true},{true}};
int vrConsoleHeldKey[2]={2,3},vrConsoleNextRepeat[2]={100,100};
int resets=0;
void Reset(int active,float x,float y,float alpha){
    assert(!active && x==0 && y==0 && alpha==0); ++resets;
}
struct {void (*VR_SetSpatialConsoleState)(int,float,float,float)=Reset;} re;
void Con_VrFollowTeleport(){
''' + body(CONSOLE, 'Con_VrFollowTeleport') + r'''
}
void Snapshot(int message,int time,int flags){
    cl.frame={true,message,time,{flags}};
    cl.frames[message & PACKET_MASK]=cl.frame;
}
int main(){
    Snapshot(1,100,0); Con_VrFollowTeleport(); assert(resets==0);
    visible=true; Con_VrFollowTeleport(); assert(resets==0);
    // Receipt precedes rendering: retain the old anchor until time reaches it.
    Snapshot(2,200,EF_TELEPORT_BIT); cl.serverTime=150;
    Con_VrFollowTeleport(); assert(resets==0);
    cl.serverTime=200; Con_VrFollowTeleport(); assert(resets==1);
    for(int h=0;h<2;++h) {
        assert(!vrConsolePointers[h].valid);
        assert(vrConsoleHeldKey[h]==-1 && vrConsoleNextRepeat[h]==0);
    }
    for(int i=0;i<20;++i) Con_VrFollowTeleport(); assert(resets==1);
    // Unrelated flags and ordinary movement cannot move the console.
    Snapshot(3,220,EF_TELEPORT_BIT|128); cl.serverTime=220;
    Con_VrFollowTeleport(); assert(resets==1);
    Snapshot(4,240,128); cl.serverTime=240;
    Con_VrFollowTeleport(); assert(resets==2);
    // Missing/overwritten snapshot slots must not fabricate a teleport.
    Snapshot(5,260,EF_TELEPORT_BIT); cl.frames[1].messageNum=1;
    cl.serverTime=260; Con_VrFollowTeleport(); assert(resets==2);
    Snapshot(5,260,EF_TELEPORT_BIT); Con_VrFollowTeleport(); assert(resets==3);
    visible=false; Con_VrFollowTeleport();
    Snapshot(6,280,0); cl.serverTime=280;
    visible=true; Con_VrFollowTeleport(); assert(resets==3);
    gameplay=false; Con_VrFollowTeleport();
    Snapshot(7,300,EF_TELEPORT_BIT); cl.serverTime=300;
    gameplay=true; Con_VrFollowTeleport(); assert(resets==3);
    re.VR_SetSpatialConsoleState=nullptr;
    Snapshot(8,320,0); cl.serverTime=320; Con_VrFollowTeleport(); assert(resets==3);
}
'''
        with tempfile.TemporaryDirectory(prefix='jkxr-console-teleport-') as directory:
            executable = str(Path(directory) / 'probe')
            subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                            '-x', 'c++', '-', '-o', executable], input=harness,
                           text=True, check=True, capture_output=True)
            subprocess.run([executable], check=True, capture_output=True)
        draw = body(CONSOLE, 'Con_DrawConsole')
        self.assertLess(draw.index('Con_VrFollowTeleport()'), draw.index('Con_DrawVrConsole()'))
        follow = body(CONSOLE, 'Con_VrFollowTeleport')
        for protected in ('g_consoleField', 'vrConsoleCaps', 'vrConsoleShift',
                          'vrConsolePhaseStart', 'Con_VrFeedback', 'Key_SetCatcher'):
            self.assertNotIn(protected, follow)
        renderer = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()
        capture = body(renderer, 'VK_CaptureSpatialConsolePose')
        self.assertIn('VK_CaptureSpatialConsoleGamePose()', capture)
        self.assertIn('&vk.spatialConsoleHitCenter', capture)
        self.assertIn('opening || !vk.spatialConsolePoseValid',
                      body(renderer, 'VK_Backend_SetSpatialConsoleState'))

    def test_console_gameplay_gate_and_binding_transitions(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable')
        controller = body(CONSOLE, 'Con_VrFilterControllerInput')
        binding = controller[controller.index('const int now ='):controller.index('Con_VrUpdatePointersAndKeyboard(')]
        harness = r'''
#include <cassert>
constexpr int CA_ACTIVE=4, KEYCATCH_UI=2, KEYCATCH_CONSOLE=1, A_TAB=9;
constexpr bool qtrue=true, qfalse=false;
struct {int state=CA_ACTIVE; bool cgameStarted=true, uiStarted=true;} cls;
struct {bool cin_camera=false, misc_camera=false;} vr;
int catcher=0, nowMs=0, opens=0, taps=0, vrConsoleBindingPressStart=0;
bool fullUI=false, movie=false, standby=false;
bool vrConsoleBindingWasDown=false, vrConsoleBindingLongPress=false, vrConsoleBindingCanOpen=false;
struct Cvar {int integer=600;} hold;
Cvar* vrConsoleHoldCvar=&hold;
int Key_GetCatcher(){return catcher;}
bool _UI_IsFullscreen(){return fullUI;}
bool CL_IsRunningInGameCinematic(){return movie;}
bool CL_InGameCinematicOnStandBy(){return standby;}
int Sys_Milliseconds(){return nowMs;}
bool Con_VrPhaseInteractive(){return false;}
void Con_VrSetOpen(bool,int){++opens;}
void Con_VrQueueControlKey(int){++taps;}
bool Con_VrGameplayAvailable(){
''' + body(CONSOLE, 'Con_VrGameplayAvailable') + r'''
}
void Tick(bool bindingDown, int time) {
    nowMs=time; int bindingHand=0;
''' + binding + r'''
}
int main(){
    assert(Con_VrGameplayAvailable());
    catcher=KEYCATCH_CONSOLE; assert(Con_VrGameplayAvailable()); catcher=0;
    cls.state=0; assert(!Con_VrGameplayAvailable()); cls.state=CA_ACTIVE;
    cls.cgameStarted=false; assert(!Con_VrGameplayAvailable()); cls.cgameStarted=true;
    for (bool* b : {&fullUI,&movie,&standby,&vr.cin_camera,&vr.misc_camera}) {
        *b=true; assert(!Con_VrGameplayAvailable()); *b=false;
    }
    catcher=KEYCATCH_UI; assert(!Con_VrGameplayAvailable());
    Tick(true,0); Tick(true,700); assert(opens==0);
    catcher=0; Tick(true,800); assert(opens==0); Tick(false,900); assert(taps==0);
    Tick(true,1000); Tick(true,1700); assert(opens==1); Tick(false,1800); assert(taps==0);
    Tick(true,2000); catcher=KEYCATCH_UI; Tick(true,2200);
    catcher=0; Tick(true,2700); assert(opens==1); Tick(false,2800); assert(taps==0);
    // Ordinary short datapad/menu taps retain their existing behavior.
    Tick(true,3000); Tick(false,3100); assert(taps==1);
    catcher=KEYCATCH_UI; Tick(true,3200); Tick(false,3300); assert(taps==2);
}
'''
        with tempfile.TemporaryDirectory(prefix='jkxr-console-gameplay-') as directory:
            executable = str(Path(directory) / 'probe')
            subprocess.run([compiler, '-std=c++17', '-x', 'c++', '-', '-o', executable],
                           input='#include <initializer_list>\n'+harness, text=True,
                           check=True, capture_output=True)
            subprocess.run([executable], check=True, capture_output=True)
        self.assertIn('open && !Con_VrGameplayAvailable()', body(CONSOLE, 'Con_VrSetOpen'))
        phase = body(CONSOLE, 'Con_VrUpdatePhase')
        self.assertIn('Con_VrPhaseVisible() && !Con_VrGameplayAvailable()', phase)
        self.assertIn('Con_Close()', phase)
        draw = body(CONSOLE, 'Con_DrawConsole')
        self.assertIn('return; // Do not fall through', draw)

    def test_console_close_transition_routes(self):
        main = (ROOT / 'code/client/cl_main.cpp').read_text()
        server = (ROOT / 'code/server/sv_ccmds.cpp').read_text()
        spawn = (ROOT / 'code/server/sv_init.cpp').read_text()
        save = (ROOT / 'code/server/sv_savegame.cpp').read_text()
        for command in ('map', 'devmap', 'devmapbsp', 'devmapmdl', 'devmapsnd', 'devmapall'):
            self.assertIn('Cmd_AddCommand ("' + command + '", SV_Map_f)', server)
        self.assertIn('SV_Map_(', body(server, 'SV_Map_f'))
        for function in ('SV_MapTransition_f', 'SV_LoadTransition_f'):
            self.assertIn('SV_Map_(', body(server, function))
        self.assertIn('SV_SpawnServer(', body(server, 'SV_Map_'))
        for function in ('SV_LoadGame_f', 'SV_TryLoadTransition'):
            self.assertIn('SG_ReadSavegame(', body(save, function))
        self.assertIn('SV_SpawnServer(', body(save[save.index('qboolean SG_ReadSavegame('):], 'SG_ReadSavegame'))
        self.assertIn('CL_MapLoading()', body(spawn, 'SV_SpawnServer'))
        for function, boundary in (
                ('CL_MapLoading', 'SCR_UpdateScreen'),
                ('CL_Disconnect', 'SCR_StopCinematic'),
                ('CL_FlushMemory', 'CL_ShutdownCGame'),
                ('CL_ShutdownRef', 're.Shutdown(')):
            code = body(main, function)
            self.assertLess(code.index('Con_Close()'), code.index(boundary))
        self.assertIn('CL_ShutdownRef(qtrue)', body(main, 'CL_Vid_Restart_f'))
        # Sound restarts and invalid map/load requests should keep diagnostics visible.
        self.assertNotIn('Con_Close()', body(main, 'CL_Snd_Restart_f'))
        self.assertNotIn('Con_Close()', body(server, 'SV_Map_'))
        self.assertNotIn('Con_Close()', body(save, 'SV_LoadGame_f'))
        ui = (ROOT / 'code/client/cl_ui.cpp').read_text()
        for function in ('CL_GenericMenu_f', 'CL_DataPad_f'):
            code = body(ui, function)
            self.assertLess(code.index('Con_Close()'), code.index('UI_SetActiveMenu'))
        cinema = (ROOT / 'code/client/cl_cin.cpp').read_text()
        play = body(cinema, 'PlayCinematic')
        self.assertLess(play.index('if (CL_handle >= 0)'), play.index('Con_Close()'))
        self.assertLess(play.index('Con_Close()'), play.index('SCR_RunCinematic()'))
        low_level = body(cinema, 'CIN_PlayCinematic')
        self.assertEqual(low_level.count('Con_Close()'), 1)
        self.assertRegex(low_level, r'if \(cinTable\[currentHandle\].alterGameState\)\s*\{\s*// close the menu\s*Con_Close\(\)')

    def test_transition_close_resets_actual_console_state(self):
        compiler = shutil.which('c++')
        if not compiler:
            self.skipTest('C++ compiler unavailable for console lifecycle probe')
        # Compile the production function body against observable client/renderer
        # state, without requiring an OpenXR session or linking the entire engine.
        harness = r'''
#include <cassert>
using qboolean = int;
constexpr int qfalse = 0, KEYCATCH_CONSOLE = 1, KEYCATCH_UI = 2;
enum { VR_CONSOLE_CLOSED, VR_CONSOLE_OPENING, VR_CONSOLE_OPEN, VR_CONSOLE_CLOSING };
int vrConsolePhase, vrConsolePhaseStart, vrConsoleShift, vrConsoleCaps;
bool vrConsoleBindingWasDown, vrConsoleBindingLongPress;
bool vrConsoleBindingCanOpen;
int vrConsoleBindingPressStart, vrConsoleHeldKey[2], vrConsoleNextRepeat[2];
bool vrConsoleTriggerWasDown[2], vrConsoleConsumedIndexTrigger[2], vrConsoleConsumedGripTrigger[2];
unsigned vrConsoleConsumedButtons[2];
struct Pointer { bool valid; float x, y, distance; } vrConsolePointers[2];
struct { bool spatial_console_visible; } vr;
struct { float finalFrac, displayFrac; } con;
bool vrLayout = true, rendererVisible = true, rendererPose = true, consoleMode = true;
bool notifyCleared = false;
int g_consoleField = 42, catcher;
bool Con_UseVrLayout() { return vrLayout; }
void Field_Clear(int* field) { *field = 0; }
void Con_ClearNotify() { notifyCleared = true; }
int Key_GetCatcher() { return catcher; }
void Key_SetCatcher(int value) { catcher = value; }
void SetMode(qboolean active) { consoleMode = active; }
void SetSpatial(qboolean active, float x, float y, float opacity) {
    assert(!active && x == 0 && y == 0 && opacity == 0);
    rendererVisible = rendererPose = false;
}
struct Renderer {
    void (*VR_SetConsoleMode)(qboolean) = SetMode;
    void (*VR_SetSpatialConsoleState)(qboolean,float,float,float) = SetSpatial;
} re;
void Con_Close() {
''' + body(CONSOLE, 'Con_Close') + r'''
}
int main() {
    for (int phase : {0,1,2,3}) for (bool held : {false,true}) {
        vrConsolePhase = phase; vrConsolePhaseStart = 500; vrConsoleShift = 1;
        vrConsoleCaps = 1; vr.spatial_console_visible = true;
        vrConsoleBindingWasDown = held; vrConsoleBindingLongPress = false;
        vrConsoleBindingPressStart = 100; con.finalFrac = con.displayFrac = 1;
        catcher = KEYCATCH_CONSOLE | KEYCATCH_UI;
        rendererVisible = rendererPose = consoleMode = true; notifyCleared = false;
        for (int h = 0; h < 2; ++h) {
            vrConsolePointers[h] = {true,1,2,3}; vrConsoleHeldKey[h] = 22;
            vrConsoleNextRepeat[h] = 700; vrConsoleTriggerWasDown[h] = true;
            vrConsoleConsumedButtons[h] = 7;
            vrConsoleConsumedIndexTrigger[h] = vrConsoleConsumedGripTrigger[h] = true;
        }
        Con_Close();
        assert(vrConsolePhase == VR_CONSOLE_CLOSED && vrConsolePhaseStart == 0);
        assert(!vr.spatial_console_visible && !vrConsoleShift && vrConsoleCaps == 1);
        assert(!rendererVisible && !rendererPose && !consoleMode);
        assert(con.finalFrac == 0 && con.displayFrac == 0 && notifyCleared);
        assert(catcher == KEYCATCH_UI && g_consoleField == 42);
        assert(vrConsoleBindingWasDown == held && vrConsoleBindingLongPress == held);
        for (int h = 0; h < 2; ++h) {
            assert(!vrConsolePointers[h].valid && vrConsoleHeldKey[h] == -1);
            assert(vrConsoleNextRepeat[h] == 0 && vrConsoleTriggerWasDown[h]);
            assert(vrConsoleConsumedButtons[h] == 7);
            assert(vrConsoleConsumedIndexTrigger[h] && vrConsoleConsumedGripTrigger[h]);
        }
        Con_Close(); // Repeated map/movie close is idempotent.
        assert(vrConsolePhase == VR_CONSOLE_CLOSED && g_consoleField == 42);
    }
    re = {nullptr,nullptr}; vrLayout = false;
    Con_Close(); // Also safe before renderer registration; preserve flat behavior.
    assert(g_consoleField == 0);
}
'''
        harness = '#include <initializer_list>\n' + harness
        with tempfile.TemporaryDirectory(prefix='jkxr-console-close-') as directory:
            executable = str(Path(directory) / 'probe')
            subprocess.run([compiler, '-std=c++17', '-x', 'c++', '-', '-o', executable],
                           input=harness, text=True, check=True, capture_output=True)
            subprocess.run([executable], check=True, capture_output=True)
        main = (ROOT / 'code/client/cl_main.cpp').read_text()
        loading = body(main, 'CL_MapLoading')
        self.assertLess(loading.index('Con_Close()'), loading.index('SCR_UpdateScreen()'))
        self.assertLess(loading.index('Con_Close()'), loading.index('CL_FlushMemory()'))
        cinema = (ROOT / 'code/client/cl_cin.cpp').read_text()
        self.assertIn('Con_Close()', body(cinema, 'CIN_PlayCinematic'))

    def test_spatial_toggle_preserves_prompt(self):
        self.assertNotIn('Field_Clear', body(CONSOLE, 'Con_VrSetOpen'))
        self.assertRegex(body(CONSOLE, 'Con_Close'),
                         r'if\s*\(\s*!Con_UseVrLayout\(\)\s*\)\s*Field_Clear')

    def test_keys_and_repeats_share_sound_and_haptic_path(self):
        activate = body(CONSOLE, 'Con_VrActivateKey')
        self.assertLess(activate.index('VR_CONSOLE_KEY_SPACER'),
                        activate.index('S_StartLocalSound'))
        self.assertIn('VR_ApplyHaptic', activate)
        self.assertIn('sound/interface/console_key.wav', activate)
        self.assertNotIn('button1.mp3', activate)
        self.assertIn('SE_CHAR', activate)
        keyboard = body(CONSOLE, 'Con_VrUpdatePointersAndKeyboard')
        self.assertIn('Con_VrActivateKey( hoveredKey, hand )', keyboard)
        self.assertIn('Con_VrActivateKey( vrConsoleHeldKey[hand], hand )', keyboard)

    def test_console_covers_roomscale_and_final_command(self):
        create = body(INPUT, 'CL_CreateCmd')
        self.assertIn('vr.spatial_console_visible', create)
        self.assertIn('KEYCATCH_CONSOLE', create)
        self.assertLess(create.index('new_move.pos_forward = new_move.pos_side = 0.0f'),
                        create.index('CL_JoystickMove'))
        finish = create[create.index('CL_FinishMove'):]
        self.assertIn('if ( consoleOwnsInput )', finish)
        self.assertIn('cmd.forwardmove = cmd.rightmove = cmd.upmove = 0', finish)
        self.assertIn('cmd.buttons = 0', finish)
        self.assertIn('cmd.generic_cmd = 0', finish)

    def test_cast_owns_trigger_before_attack_and_resets_history(self):
        handle = body(CONTROLLERS, 'HandleInput_Default')
        self.assertLess(handle.index('forceCast.Update'),
                        handle.index('vr.primaryVelocityTriggeredAttack ='))
        self.assertIn('vr.forceGestureHistorySampleCount = 0', handle)
        self.assertIn('!vr.dual_saber_casting && (vr.secondaryswingvelocity', handle)
        self.assertIn('forceCast.Consume()', handle)
        self.assertIn('forceCast.CanGesture()', handle)
        self.assertIn('forceCast.SelectedPowerHeld()', handle)
        self.assertIn('forceGripDown && !forceGripConsumed', handle)
        self.assertNotIn('castEvent.tap', handle)

    def test_offhand_server_damage_is_suppressed_without_affecting_primary(self):
        saber = (ROOT / 'code/game/wp_saber.cpp').read_text()
        # The legacy disabled stub precedes the real function.
        saber = saber[saber.index('#define MAX_SABER_SWING_INC'):]
        trace = body(saber, 'WP_SaberDamageTrace')
        self.assertIn('saberNum == 1 && vr->dual_saber_casting', trace)
        self.assertIn('blade.muzzlePoint, blade.muzzlePointOld', trace)
        self.assertLess(trace.index('vr->dual_saber_casting'), trace.index('numVictims = 0'))

    def test_saber_motion_release_is_not_gated_on_active_blades(self):
        handle = body(CONTROLLERS, 'HandleInput_Default')
        release = handle[handle.index('if (saberMotionAttackHeld && !saberMotionAllowed)'):]
        self.assertIn('sendButtonAction("+attack", false)', release[:400])
        self.assertIn('saberMotionAttackHeld = false', release[:400])
        self.assertIn('vr.velocitytriggered && vr.velocitytriggeractive', handle)
        self.assertIn('vr.primaryVelocityTriggeredAttack = saberMotionAllowed &&', handle)
        self.assertIn('vr.secondaryVelocityTriggeredAttack = saberMotionAllowed && vr.dualsabers', handle)
        self.assertNotIn('if (vr.velocitytriggeractive)', handle)

    def test_wheel_cancellation_releases_time_without_committing(self):
        handle = body(CONTROLLERS, 'HandleInput_Default')
        chord = handle[handle.index('if (dualForceMode && forceTriggerDown && forceGripDown)'):
                       handle.index('const bool forceInputAllowed')]
        self.assertIn('sendButtonActionSimple("itemselectorcancel")', chord)
        self.assertNotIn('itemselectorselect', chord)
        for tree in ('code', 'codeJK2'):
            cg = ROOT / tree / 'cgame'
            weapons = (cg / 'cg_weapons.cpp').read_text()
            cancel = body(weapons, 'CG_ItemSelectorCancel_f')
            release = body(weapons, 'CG_ItemSelectorReleaseTime')
            self.assertIn('itemSelectorTimeScale.End', release)
            self.assertIn('cg.itemSelectorTime = 0', release)
            self.assertNotIn('cg.itemSelectorSelection', release)
            self.assertIn('CG_ItemSelectorReleaseTime()', cancel)
            self.assertIn('cg.itemSelectorSelection = ST_NONE', cancel)
            self.assertNotIn('cgi_SendConsoleCommand', cancel)
            self.assertNotIn('cg.forcepowerSelect =', cancel)
            select = body(weapons, 'CG_ItemSelectorSelect_f')
            self.assertLess(select.index('const int selection'),
                            select.index('CG_ItemSelectorCancel_f'))
            self.assertIn('itemSelectorTimeScale.Begin', body(weapons, 'CG_DrawItemSelector'))
            self.assertIn('CG_ItemSelectorReleaseTime', body((cg / 'cg_view.cpp').read_text(),
                                                            'CG_DrawActiveFrame'))
            self.assertIn('CG_ItemSelectorCancel_f', body((cg / 'cg_main.cpp').read_text(),
                                                         'CG_Shutdown'))
            self.assertIn('{ "itemselectorcancel", CG_ItemSelectorCancel_f }',
                          (cg / 'cg_consolecmds.cpp').read_text())


if __name__ == '__main__':
    unittest.main()
