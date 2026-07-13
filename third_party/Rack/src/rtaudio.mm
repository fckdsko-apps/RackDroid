#include <rtaudio.hpp>
#import <AVFoundation/AVFoundation.h>

namespace rack {


bool rtaudioIsMicrophoneBlocked() {
	// authorizationStatusForMediaType is only available on Mac 10.14+.
	if (@available(macOS 10.14, *)) {
		AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeAudio];
		return status == AVAuthorizationStatusRestricted || status == AVAuthorizationStatusDenied;
	}
	// On earlier versions, assume microphone is not blocked.
	return false;
}


} // namespace rack
