#ifndef TBXR_COMMON_H
#define TBXR_COMMON_H

#include <openxr.h>

#ifndef NDEBUG
#define ALOGV(...) Com_Printf(__VA_ARGS__)
#else
#define ALOGV(...)
#endif

typedef enum xrButton_ {
    xrButton_A = 0x00000001, // Set for trigger pulled on the Gear VR and Go Controllers
    xrButton_B = 0x00000002,
    xrButton_RThumb = 0x00000004,
    xrButton_RShoulder = 0x00000008,
    xrButton_X = 0x00000100,
    xrButton_Y = 0x00000200,
    xrButton_LThumb = 0x00000400,
    xrButton_LShoulder = 0x00000800,
    xrButton_Up = 0x00010000,
    xrButton_Down = 0x00020000,
    xrButton_Left = 0x00040000,
    xrButton_Right = 0x00080000,
    xrButton_Enter = 0x00100000,
    xrButton_Back = 0x00200000,
    xrButton_GripTrigger = 0x04000000,
    xrButton_Trigger = 0x20000000,
    xrButton_Joystick = 0x80000000,

    //Define additional controller touch points (not button presses)
    xrButton_ThumbRest = 0x00000010,

    xrButton_EnumSize = 0x7fffffff
} xrButton;

typedef struct {
    uint32_t Buttons;
    uint32_t Touches;
    float IndexTrigger;
    float GripTrigger;
    XrVector2f Joystick;
} ovrInputStateTrackedRemote;

typedef struct {
    bool Active;
    XrPosef Pose;
    XrPosef GripPose;
    XrSpaceVelocity Velocity;
} ovrTrackedController;

typedef enum control_scheme {
    RIGHT_HANDED_DEFAULT = 0,
    LEFT_HANDED_DEFAULT = 10,
    WEAPON_ALIGN = 99
} control_scheme_t;

typedef struct {
    float M[4][4];
} ovrMatrix4f;


// Controller profile shared with the renderer-owned OpenXR input session.
struct ovrApp
{
    int controllersPresent = -1;
};
extern ovrApp gAppState;

//Functions that need to be implemented by the game specific code
void VR_FrameSetup();
bool VR_UseScreenLayer();
float VR_GetScreenLayerDistance();
void VR_ProcessControllerInput();
void VR_SetHMDOrientation(float pitch, float yaw, float roll );
void VR_SetHMDPosition(float x, float y, float z );
void VR_HapticEvent(const char* event, int position, int flags, int intensity, float angle, float yHeight );
void VR_HapticUpdateEvent(const char* event, int intensity, float angle );
void VR_HapticEndFrame();
void VR_HapticStopEvent(const char* event);
void VR_HapticEnable();
void VR_HapticDisable();



double TBXR_GetTimeInMilliSeconds();
int TBXR_GetRefresh();
void TBXR_Vibrate(int duration, int channel, float intensity);
void TBXR_FrameSetup();

#define VIVE_CONTROLLERS 10
#define INDEX_CONTROLLERS 11
#define PICO_CONTROLLERS 12
#define TOUCH_CONTROLLERS 13

#endif
