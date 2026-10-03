#!/usr/bin/env python3
"""Execute both games' actual interpolation and the renderer console anchor."""
from pathlib import Path
import subprocess
import tempfile
import unittest
from check_vulkan_upload_boundary import body

ROOT = Path(__file__).resolve().parents[1] / 'OpenJK'


class TeleportView(unittest.TestCase):
    def test_console_captures_destination_not_smoothed_departure(self):
        renderer = (ROOT / 'code/rd-vulkan/vk_backend.cpp').read_text()
        for game in ('code', 'codeJK2'):
            with self.subTest(game=game):
                source = (ROOT / game / 'cgame/cg_predict.cpp').read_text()
                harness = r'''
#include <cassert>
#include <cmath>
#include <cstdio>
using qboolean=int;
constexpr int qfalse=0, qtrue=1, EF_TELEPORT_BIT=4, ET_MOVER=7;
using vec3_t=float[3];
void VectorCopy(const float* a,float* b){for(int i=0;i<3;++i)b[i]=a[i];}
void VectorAdd(const float* a,const float* b,float* c){for(int i=0;i<3;++i)c[i]=a[i]+b[i];}
void VectorSet(float* a,float x,float y,float z){a[0]=x;a[1]=y;a[2]=z;}
void VectorMA(const float* a,float s,const float* b,float* c){for(int i=0;i<3;++i)c[i]=a[i]+s*b[i];}
float VectorNormalize(float* a){float n=std::sqrt(a[0]*a[0]+a[1]*a[1]+a[2]*a[2]);if(n)for(int i=0;i<3;++i)a[i]/=n;return n;}
void CrossProduct(const float* a,const float* b,float* c){for(int i=0;i<3;++i)c[i]=a[(i+1)%3]*b[(i+2)%3]-a[(i+2)%3]*b[(i+1)%3];}
float LerpAngle(float a,float b,float f){return a+(b-a)*f;}
struct playerState_t {int eFlags=0,bobCycle=0,groundEntityNum=0; vec3_t origin={},viewangles={},velocity={};};
struct snapshot_t {int serverTime=100;playerState_t ps;};
struct usercmd_t {};
int cgi_GetCurrentCmdNumber(){return 0;}
void cgi_GetUserCmd(int,usercmd_t*){}
bool CG_CheckModifyUCmd(usercmd_t*,float*){return false;}
void PM_UpdateViewAngles(playerState_t*,usercmd_t*,void*){}
struct State {int eType=0,pos=0;operator bool()const{return true;} State* operator->(){return this;}};
struct centity_t {State currentState,nextState;} cg_entities[2];
void EvaluateTrajectory(const int*,int,float* out){VectorSet(out,0,0,0);}
struct Cvar {float value;} cg_smoothPlayerPos{0.5f},cg_smoothPlayerPlat{0.75f},cg_smoothPlayerPlatAccel{3.25f};
struct {playerState_t predicted_player_state; snapshot_t *snap=nullptr,*nextSnap=nullptr;
    bool nextFrameTeleport=false,validPPS=true;int time=100,frametime=16;} cg;
struct {bool smooth_active=true;} client_camera;
bool in_camera=false,in_misccamera=false;
void CG_InterpolatePlayerState(qboolean grabAngles){
''' + body(source, 'CG_InterpolatePlayerState') + r'''
}
struct Refdef {vec3_t vieworg={},viewaxis[3]={{1,0,0},{0,1,0},{0,0,1}};};
struct {bool sceneWorldRenderedThisFrame=true,haveWorldRefdef=true,spatialConsoleGamePoseValid=false;
    Refdef worldRefdef;vec3_t spatialConsoleGameForward={},spatialConsoleGameRight={},
    spatialConsoleGameUp={},spatialConsoleGameCenter={};float spatialConsoleGameDistance=0;} vk;
float VK_RefdefWorldScale(const Refdef&){return 33.5f;}
void VK_CaptureSpatialConsoleGamePose(){
''' + body(renderer, 'VK_CaptureSpatialConsoleGamePose') + r'''
}
void DrawAndCapture(){
    CG_InterpolatePlayerState(qtrue);
    VectorCopy(cg.predicted_player_state.origin,vk.worldRefdef.vieworg);
    vk.worldRefdef.vieworg[2]+=24;
    VK_CaptureSpatialConsoleGamePose();
}
void Expect(float x,float y,float z){
    if(std::fabs(vk.worldRefdef.vieworg[0]-x)>0.001f ||
       std::fabs(vk.worldRefdef.vieworg[1]-y)>0.001f ||
       std::fabs(vk.worldRefdef.vieworg[2]-(z+24))>0.001f) {
        std::fprintf(stderr,"camera captured (%.2f %.2f %.2f), expected (%.2f %.2f %.2f)\n",
            vk.worldRefdef.vieworg[0],vk.worldRefdef.vieworg[1],vk.worldRefdef.vieworg[2],x,y,z+24);
        assert(false);
    }
    assert(vk.spatialConsoleGamePoseValid);
    assert(std::fabs(vk.spatialConsoleGameCenter[0]-x-201*vk.worldRefdef.viewaxis[0][0])<0.001f);
    assert(std::fabs(vk.spatialConsoleGameCenter[1]-y-201*vk.worldRefdef.viewaxis[0][1])<0.001f);
}
int main(){
    snapshot_t current;cg.snap=&current;
    VectorSet(cg.predicted_player_state.origin,2292,430,-202);
    VectorSet(current.ps.origin,-440,1096,101);
    current.ps.eFlags=EF_TELEPORT_BIT;
    DrawAndCapture();Expect(-440,1096,101);
    assert(!client_camera.smooth_active);
    // Same flag on a following frame must not be mistaken for another teleport.
    VectorSet(current.ps.origin,-420,1096,101);
    client_camera.smooth_active=true;
    DrawAndCapture();Expect(-430,1096,101);
    assert(client_camera.smooth_active);
    // Short reverse teleport, including a changed view yaw.
    VectorSet(current.ps.origin,-440,1400,101);current.ps.eFlags=0;
    VectorSet(vk.worldRefdef.viewaxis[0],0,-1,0);
    DrawAndCapture();Expect(-440,1400,101);
    assert(!client_camera.smooth_active);
    // Same-position/yaw-only teleport still resets smoothing.
    current.ps.eFlags=EF_TELEPORT_BIT;
    VectorSet(vk.worldRefdef.viewaxis[0],0,1,0);
    DrawAndCapture();Expect(-440,1400,101);
    // Future teleport must not interpolate toward the destination prematurely.
    snapshot_t future=current;future.serverTime=200;future.ps.origin[0]=5000;
    future.ps.eFlags=0;cg.nextSnap=&future;cg.time=150;cg.nextFrameTeleport=true;
    DrawAndCapture();Expect(-440,1400,101);
    cg.snap=&future;cg.nextSnap=nullptr;cg.nextFrameTeleport=false;cg.time=200;
    DrawAndCapture();Expect(5000,1400,101);
    // Teleport off a mover also bypasses its separate smoothing branch.
    future.ps.eFlags=EF_TELEPORT_BIT;future.ps.groundEntityNum=1;
    cg_entities[1].currentState.eType=ET_MOVER;future.ps.origin[0]=20;
    DrawAndCapture();Expect(20,1400,101);
    future.ps.origin[0]=40;
    DrawAndCapture();Expect(25,1400,101); // Ordinary platform smoothing remains 0.75.
    in_camera=true;client_camera.smooth_active=true;future.ps.eFlags=0;
    DrawAndCapture();Expect(40,1400,101);assert(client_camera.smooth_active);
    in_camera=false;in_misccamera=true;future.ps.eFlags=EF_TELEPORT_BIT;
    DrawAndCapture();Expect(40,1400,101);assert(client_camera.smooth_active);
}
'''
                with tempfile.TemporaryDirectory(prefix='jkxr-teleport-view-') as directory:
                    executable = str(Path(directory) / 'probe')
                    subprocess.run(['c++', '-std=c++17', '-fsanitize=address,undefined',
                                    '-x', 'c++', '-', '-o', executable], input=harness,
                                   text=True, check=True)
                    subprocess.run([executable], check=True)


if __name__ == '__main__':
    unittest.main()
