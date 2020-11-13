#include "rtc_connection.h"

#include <api/peer_connection_interface.h>
#include <api/scoped_refptr.h>
#include <rtc_base/ref_counted_object.h>
#include <rtc_base/logging.h>

class RTCStatsCallback : public webrtc::RTCStatsCollectorCallback {
public:
	typedef std::function<void(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report)> ResultCallback;

	static RTCStatsCallback* Create(ResultCallback result_callback) {
		return new rtc::RefCountedObject<RTCStatsCallback>(
			std::move(result_callback));
	}

	void OnStatsDelivered(const rtc::scoped_refptr<const webrtc::RTCStatsReport>& report) override {
		std::move(result_callback_)(report);
	}

protected:
	RTCStatsCallback(ResultCallback result_callback) : result_callback_(std::move(result_callback)) {}
	~RTCStatsCallback() override = default;

private:
	ResultCallback result_callback_;
};

class CreateSessionDescriptionThunk : public webrtc::CreateSessionDescriptionObserver {
public:
	typedef RTCConnection::OnCreateSuccessFunc OnSuccessFunc;
	typedef RTCConnection::OnCreateFailureFunc OnFailureFunc;

	static rtc::scoped_refptr<CreateSessionDescriptionThunk> Create(OnSuccessFunc on_success, OnFailureFunc on_failure) {
		return new rtc::RefCountedObject<CreateSessionDescriptionThunk>(std::move(on_success), std::move(on_failure));
	}

protected:
	CreateSessionDescriptionThunk(OnSuccessFunc on_success,
		OnFailureFunc on_failure)
		: on_success_(std::move(on_success)),
		on_failure_(std::move(on_failure)) {}
	void OnSuccess(webrtc::SessionDescriptionInterface* desc) override {
		auto f = std::move(on_success_);
		if (f) {
			f(desc);
		}
	}
	void OnFailure(webrtc::RTCError error) override {
		RTC_LOG(LS_ERROR) << "Failed to create session description : " << webrtc::ToString(error.type()) << ": " << error.message();
		auto f = std::move(on_failure_);
		if (f) {
			f(error);
		}
	}

private:
	OnSuccessFunc on_success_;
	OnFailureFunc on_failure_;
};


class SetSessionDescriptionThunk : public webrtc::SetSessionDescriptionObserver {
public:
	typedef RTCConnection::OnSetSuccessFunc OnSuccessFunc;
	typedef RTCConnection::OnSetFailureFunc OnFailureFunc;

	static rtc::scoped_refptr<SetSessionDescriptionThunk> Create(OnSuccessFunc on_success, OnFailureFunc on_failure) {
		return new rtc::RefCountedObject<SetSessionDescriptionThunk>(std::move(on_success), std::move(on_failure));
	}

protected:
	SetSessionDescriptionThunk(OnSuccessFunc on_success, OnFailureFunc on_failure) : on_success_(std::move(on_success)), 
		on_failure_(std::move(on_failure)) {
	}

	void OnSuccess() override {
		auto f = std::move(on_success_);
		if (f) {
			f();
		}
	}

	void OnFailure(webrtc::RTCError error) override {
		RTC_LOG(LS_ERROR) << "Failed to set session description : " << webrtc::ToString(error.type()) << ": " << error.message();
		auto f = std::move(on_failure_);
		if (f) {
			f(error);
		}
	}

private:
	OnSuccessFunc on_success_;
	OnFailureFunc on_failure_;
};

RTCConnection::~RTCConnection() {
	connection_->Close();
}

void RTCConnection::CreateOffer(bool publish, OnCreateSuccessFunc on_success, OnCreateFailureFunc on_failure) {
	using RTCOfferAnswerOptions = webrtc::PeerConnectionInterface::RTCOfferAnswerOptions;

	RTCOfferAnswerOptions options  = RTCOfferAnswerOptions();
	if (publish) {
		options.offer_to_receive_video = 0;
		options.offer_to_receive_audio = 0;
	} else {
		options.offer_to_receive_video = RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;
		options.offer_to_receive_audio = RTCOfferAnswerOptions::kOfferToReceiveMediaTrue;
	}

	auto with_set_local_desc = [this, on_success = std::move(on_success)](webrtc::SessionDescriptionInterface* desc) {
		std::string sdp;
		desc->ToString(&sdp);
		RTC_LOG(LS_INFO) << "Created session description : " << sdp;
		connection_->SetLocalDescription(SetSessionDescriptionThunk::Create(nullptr, nullptr), desc);
		if (on_success) {
			on_success(desc);
		}
	};

	connection_->CreateOffer(CreateSessionDescriptionThunk::Create(std::move(with_set_local_desc),
		std::move(on_failure)), options);
}

void RTCConnection::SetOffer(const std::string sdp, OnSetSuccessFunc on_success, OnSetFailureFunc on_failure) {
	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> session_description = webrtc::CreateSessionDescription(webrtc::SdpType::kOffer, sdp, &error);
	if (!session_description) {
		RTC_LOG(LS_ERROR) << __FUNCTION__ << "Failed to create session description: " << error.description.c_str() << "\nline: " << error.line.c_str();
		return;
	}
	connection_->SetRemoteDescription(SetSessionDescriptionThunk::Create(std::move(on_success), 
		std::move(on_failure)), session_description.release());
}

void RTCConnection::CreateAnswer(OnCreateSuccessFunc on_success, OnCreateFailureFunc on_failure) {
	auto with_set_local_desc = [this, on_success = std::move(on_success)](
		webrtc::SessionDescriptionInterface* desc) {
		std::string sdp;
		desc->ToString(&sdp);
		RTC_LOG(LS_INFO) << "Created session description : " << sdp;
		connection_->SetLocalDescription(SetSessionDescriptionThunk::Create(nullptr, nullptr), desc);
		if (on_success) {
			on_success(desc);
		}
	};

	connection_->CreateAnswer(CreateSessionDescriptionThunk::Create(std::move(with_set_local_desc), 
		std::move(on_failure)), webrtc::PeerConnectionInterface::RTCOfferAnswerOptions());
}

void RTCConnection::SetAnswer(const std::string sdp, OnSetSuccessFunc on_success, OnSetFailureFunc on_failure) {
	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::SessionDescriptionInterface> session_description = webrtc::CreateSessionDescription(webrtc::SdpType::kAnswer, sdp, &error);
	if (!session_description) {
		RTC_LOG(LS_ERROR) << __FUNCTION__ << "Failed to create session description: " << error.description.c_str() << "\nline: " << error.line.c_str();
		return;
	}

	connection_->SetRemoteDescription(SetSessionDescriptionThunk::Create(std::move(on_success), 
		std::move(on_failure)), session_description.release());
}

void RTCConnection::AddIceCandidate(const std::string sdp_mid, const int sdp_mlineindex, const std::string sdp) {
	webrtc::SdpParseError error;
	std::unique_ptr<webrtc::IceCandidateInterface> candidate(webrtc::CreateIceCandidate(sdp_mid, sdp_mlineindex, sdp, &error));
	if (!candidate.get()) {
		RTC_LOG(LS_ERROR) << "Can't parse received candidate message: " << error.description.c_str() << "\nline: " << error.line.c_str();
		return;
	}

	connection_->AddIceCandidate(
		std::move(candidate), [sdp](webrtc::RTCError error) {
			RTC_LOG(LS_WARNING) << __FUNCTION__ << " Failed to apply the received candidate. type="
				<< webrtc::ToString(error.type()) << " message=" << error.message()
				<< " sdp=" << sdp;
		});
}

void RTCConnection::GetStats(std::function<void(const rtc::scoped_refptr<const webrtc::RTCStatsReport>&)> callback) {
	connection_->GetStats(RTCStatsCallback::Create(std::move(callback)));
}

std::string RtpTransceiverDirectionToString(webrtc::RtpTransceiverDirection dir) {
	switch (dir) {
	case webrtc::RtpTransceiverDirection::kSendRecv:
		return "kSendRecv";
	case webrtc::RtpTransceiverDirection::kSendOnly:
		return "kSendOnly";
	case webrtc::RtpTransceiverDirection::kRecvOnly:
		return "kRecvOnly";
	case webrtc::RtpTransceiverDirection::kInactive:
		return "kInactive";
	case webrtc::RtpTransceiverDirection::kStopped:
		return "kStopped";
	default:
		return "UNKNOWN";
	}
}

void RTCConnection::SetEncodingParameters(std::vector<webrtc::RtpEncodingParameters> encodings) {
	for (auto transceiver : connection_->GetTransceivers()) {
		RTC_LOG(LS_INFO) << "transceiver mid=" << transceiver->mid().value_or("nullopt") << " direction="
			<< RtpTransceiverDirectionToString(transceiver->direction())
			<< " current_direction="
			<< (transceiver->current_direction() ? RtpTransceiverDirectionToString(*transceiver->current_direction()) : "nullopt")
			<< " media_type="
			<< cricket::MediaTypeToString(transceiver->media_type())
			<< " sender_encoding_count="
			<< transceiver->sender()->GetParameters().encodings.size();
	}

	rtc::scoped_refptr<webrtc::RtpTransceiverInterface> video_transceiver;
	for (auto transceiver : connection_->GetTransceivers()) {
		if (transceiver->media_type() == cricket::MediaType::MEDIA_TYPE_VIDEO) {
			video_transceiver = transceiver;
			break;
		}
	}

	if (video_transceiver == nullptr) {
		RTC_LOG(LS_ERROR) << "video transceiver not found";
		return;
	}

	rtc::scoped_refptr<webrtc::RtpSenderInterface> sender = video_transceiver->sender();
	webrtc::RtpParameters parameters = sender->GetParameters();
	parameters.encodings = std::move(encodings);
	sender->SetParameters(parameters);
}

rtc::scoped_refptr<webrtc::PeerConnectionInterface> RTCConnection::GetConnection() const {
	return connection_;
}

bool RTCConnection::GetOffer(std::string& sdp) {
	if (connection_) {
		const webrtc::SessionDescriptionInterface* desc = connection_->local_description();
		if (desc) {
			return desc->ToString(&sdp);
		}
	}
	return false;
}