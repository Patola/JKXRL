#include "VrInput.h"
#include "VrCvars.h"
#include <client/client.h>

void TBXR_Vibrate( int duration, int chan, float intensity )
{
	if (re.VR_ApplyHaptic != nullptr)
	{
		const float scale = vr_haptic_intensity != nullptr
			? vr_haptic_intensity->value : 1.0f;
		const float amplitude = Com_Clamp( 0.0f, 1.0f, intensity * scale );
		// Preserve the inherited channel mask: bit 0 is the right hand and
		// bit 1 is the left hand.
		if (chan & 1)
		{
			re.VR_ApplyHaptic( 1, duration, amplitude );
		}
		if (chan & 2)
		{
			re.VR_ApplyHaptic( 0, duration, amplitude );
		}
		return;
	}
}
