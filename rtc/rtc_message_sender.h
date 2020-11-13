#ifndef __RTC_MESSAGE_SENDER_H__
#define __RTC_MESSAGE_SENDER_H__

#include <string>
#include <api/peer_connection_interface.h>

class RTCMessageSender {
public:
	virtual void OnIceConnectionStateChange(webrtc::PeerConnectionInterface::IceConnectionState new_state) = 0;
	virtual void OnIceCandidate(const std::string sdp_mid, const int sdp_mlineindex, const std::string sdp) = 0;
};

#endif
