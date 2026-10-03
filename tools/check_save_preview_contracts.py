#!/usr/bin/env python3
"""Compile the actual save-reader failure path and guard capture/UI routing."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'


class SavePreviewContracts(unittest.TestCase):
    def test_post_save_refresh_selects_file_and_reloads_preview(self):
        source = (ROOT/'code/ui/ui_main.cpp').read_text()
        self.assertIn('save %s\\nui_refreshSaveGames %s\\n', source)
        save = source.split('else if (Q_stricmp(name, "savegame") == 0)')[1].split('else if')[0]
        self.assertNotIn('s_savegame.saveFileCnt = -1', save)
        fixture = r'''
#include <cassert>
#include <cstring>
#include <strings.h>
struct { int saveFileCnt, currentLine; } s_savegame;
struct { const char* currentSaveFileName; } s_savedata[4];
int reads, loads, adjusted, selected;
const char* preview;
void ReadSaveDirectory() { ++reads; preview=nullptr; }
int Q_stricmp(const char* a,const char* b) { return strcasecmp(a,b); }
void UI_HandleLoadSelection() {
    assert(reads==1);
    ++loads;
    selected=s_savegame.currentLine;
    if(s_savegame.saveFileCnt) preview=s_savedata[selected].currentSaveFileName;
}
void UI_AdjustSaveGameListBox(int row) { assert(loads==1); adjusted=row; }
void Refresh(const char* savedFile) {
''' + body(source, 'UI_RefreshSaveGames') + r'''
}
void Reset(int count,int row) {
    s_savegame={count,row}; reads=loads=0; selected=adjusted=-1; preview="stale";
}
int main() {
    s_savedata[0]={"new"}; s_savedata[1]={"old"}; s_savedata[2]={"overwritten"};
    Reset(3,1); Refresh("new");
    assert(selected==0 && adjusted==0 && std::strcmp(preview,"new")==0);
    Reset(3,0); Refresh("overwritten");
    assert(selected==2 && adjusted==2 && std::strcmp(preview,"overwritten")==0);
    Reset(3,1); Refresh("failed");
    assert(selected==1 && adjusted==1 && std::strcmp(preview,"old")==0);
    Reset(0,0); Refresh("failed");
    assert(adjusted==0 && preview==nullptr);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            src, exe = Path(directory)/'refresh.cpp', Path(directory)/'refresh'
            src.write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                            str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_jko_menu_strings_load_with_actual_striped_parser(self):
        source = (ROOT/'code/qcommon/strip.cpp').read_text()
        declarations = (ROOT/'code/server/server.h').read_text().split('// glue\n')[1].split('#endif')[0]
        parser = source[source.index('cvar_t\t*sp_language;'):source.index('// A map of loaded string packages')]
        fixture = r'''
#include <cassert>
#include <cstring>
#include <strings.h>
#include <string>
#include <map>
#include <list>
#include <fstream>
#include <iterator>
#include <cstdio>
using byte=unsigned char;
struct cvar_t { int integer=0; float value=0; };
enum { SP_LANGUAGE_ENGLISH, SP_LANGUAGE_FRENCH, SP_LANGUAGE_GERMAN,
SP_LANGUAGE_BRITISH, SP_LANGUAGE_KOREAN, SP_LANGUAGE_TAIWANESE,
SP_LANGUAGE_ITALIAN, SP_LANGUAGE_SPANISH, SP_LANGUAGE_JAPANESE, SP_LANGUAGE_10 };
int Q_stricmp(const char* a,const char* b) { return strcasecmp(a,b); }
int Q_stricmpn(const char* a,const char* b,int n) { return strncasecmp(a,b,n); }
''' + declarations + parser + r'''
int main(int argc,char** argv) {
    cvar_t zero;
    sp_language=sp_show_strip=sp_leet=&zero;
    std::ifstream file(argv[1]);
    std::string data{std::istreambuf_iterator<char>(file),{}};
    int size=data.size();
    cStringPackageSingle package("menus_vr");
    assert(package.Load(data.data(),size));
    assert(std::strcmp(package.GetReference(), "MENUS_VR")==0);
    for(const char* key : {"MENUS_VR_OVERWRITE_GAME_ITEM", "MENUS_VR_CONSOLE_ANIMATION_DESC"}) {
        int id=package.FindStringID(key);
        std::fprintf(stderr,"%s = %d: %s\n",key,id,id<0?"MISSING":package.FindString(id)->GetText());
        assert(id>=0 && package.FindString(id)->GetText()[0]);
    }
}
'''
        with tempfile.TemporaryDirectory() as directory:
            src, exe = Path(directory)/'strip.cpp', Path(directory)/'strip'
            src.write_text(fixture)
            subprocess.run(['c++', '-std=c++17', str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe), str(ROOT.parent/'z_vr_assets_jko/strip/menus_vr.sp')], check=True)

    def test_reader_consumes_empty_chunk_and_rejects_unwritten_outputs(self):
        source = (ROOT/'code/server/sv_savegame.cpp').read_text()
        fixture = r'''
#include <cassert>
#include <cstdint>
#include <cstdlib>
#include <cstring>
using byte=unsigned char;
constexpr int SG_SCR_WIDTH=512, SG_SCR_HEIGHT=512, TAG_TEMP_WORKSPACE=0, qfalse=0;
#define INT_ID(a,b,c,d) ((a<<24)|(b<<16)|(c<<8)|d)
size_t length; int chunks, allocations, decodes, mode, loadWidth;
bool chunkOK;
void* Z_Malloc(int n,int,int) { ++allocations; return std::malloc(n); }
void Z_Free(void* p) { if(p) { --allocations; std::free(p); } }
void SCR_SetScreenshot(const byte*,int w,int) { loadWidth=w; }
namespace ojk {
struct SavedGame { static SavedGame& get_instance() { static SavedGame g; return g; } };
struct SavedGameHelper {
    SavedGameHelper(SavedGame*) {}
    template<typename T> bool try_read_chunk(int,size_t& value) { ++chunks; value=length; return true; }
    bool try_read_chunk(int,byte*,int) { ++chunks; return chunkOK; }
}; }
void Decode(byte*,size_t,byte** pic,int* width,int* height) {
    ++decodes;
    if(mode==0) return; // old stub: writes nothing
    *width=*height=512;
    if(mode==1) return; // dimensions are not proof of a valid image
    *pic=(byte*)Z_Malloc(512*512*4,0,0);
    std::memset(*pic,42,512*512*4);
}
struct { decltype(&Decode) LoadJPGFromBuffer=Decode; } re;
bool Read(bool set_as_loading_screen,void* screenshot_ptr) {
''' + body(source, 'SG_ReadScreenshot') + r'''
}
void Reset(size_t len,int decoder=0) {
    length=len; mode=decoder; chunks=allocations=decodes=0; loadWidth=99; chunkOK=true;
}
int main() {
    Reset(0); assert(!Read(true,nullptr));
    assert(chunks==2 && decodes==0 && allocations==0 && loadWidth==0);
    Reset(123); assert(!Read(false,nullptr)); assert(chunks==2 && allocations==0);
    Reset(123,1); assert(!Read(false,nullptr)); assert(allocations==0);
    Reset(123,2); assert(Read(true,nullptr)); assert(loadWidth==512 && allocations==0);
    static byte dest[512*512*4];
    Reset(123,2); assert(Read(false,dest)); assert(dest[0]==42 && allocations==0);
    Reset(123,2); chunkOK=false; assert(!Read(false,nullptr)); assert(decodes==0 && allocations==0);
    Reset(4*1024*1024+1); assert(!Read(false,nullptr)); assert(allocations==0 && decodes==0);
}
'''
        with tempfile.TemporaryDirectory() as directory:
            src, exe = Path(directory)/'reader.cpp', Path(directory)/'reader'
            src.write_text(fixture)
            subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                            str(src), '-o', str(exe)], check=True)
            subprocess.run([str(exe)], check=True)

    def test_capture_is_on_demand_and_inside_image_ownership(self):
        source = (ROOT/'code/rd-vulkan/vk_backend.cpp').read_text()
        draw = body(source, 'VK_RecordTestPattern')
        self.assertIn('eye == 0 && savePreview.pending && !clearOnly && vk.sceneWorldRenderedThisFrame', draw)
        self.assertLess(draw.index('VK_RecordSavePreview(imageIndex)'), draw.index('vkEndCommandBuffer'))
        submit = body(source, 'VK_RenderEyes')
        self.assertLess(submit.index('vkQueueWaitIdle'), submit.index('savePreview.ready = ready && savePreview.recorded'))
        self.assertLess(submit.index('savePreview.ready ='), submit.index('xrReleaseSwapchainImage'))
        request = body(source, 'VK_Backend_RequestSavePreview')
        self.assertIn('vk.frameBegun || vk.stereoCommandsRecording', request)
        read = body(source, 'VK_Backend_ReadSavePreview')
        self.assertIn('savePreview.ready &&', read)
        self.assertIn('VK_ClearSavePreview()', read)
        self.assertIn('savePreview.thumbnail ? size_t(width)*height', request)
        rects = body(source, 'VK_RecordScreenRects')
        self.assertIn('if (savePreview.pending && vk.sceneWorldRenderedThisFrame) return;', rects)

    def test_capture_format_and_preview_validity_are_explicit(self):
        source = (ROOT/'code/client/cl_scrn.cpp').read_text()
        capture = body(source, 'SCR_PrecacheScreenshot')
        self.assertLess(capture.index('re.RequestSavePreview('), capture.index('SCR_UpdateScreen()'))
        self.assertLess(capture.index('SCR_UpdateScreen()'), capture.index('re.ReadSavePreview('))
        self.assertIn('capturing || scrUpdateDepth', capture)
        self.assertIn('saveScreenDataValid = qfalse', body(source, 'SCR_SetScreenshot'))
        ui = (ROOT/'code/ui/ui_main.cpp').read_text()
        self.assertNotIn('if (screenShotBuf[0])', ui)
        self.assertIn('if (screenShotValid)', ui)
        read = body(ui, 'UI_HandleLoadSelection')
        self.assertLess(read.index('screenShotValid = false'), read.index('if (!valid)'))


if __name__ == '__main__':
    unittest.main()
