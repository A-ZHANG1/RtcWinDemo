#include "rtc_video_capturer.h"
#include <algorithm>

#include "api/scoped_refptr.h"
#include "api/video/i420_buffer.h"
#include "api/video/video_frame_buffer.h"
#include "api/video/video_rotation.h"

namespace webrtc {

	RtcVideoCapturer::~RtcVideoCapturer() = default;

	void RtcVideoCapturer::OnFrame(const VideoFrame& frame) {
		int cropped_width = 0;
		int cropped_height = 0;
		int out_width = 0;
		int out_height = 0;

		if (!video_adapter_.AdaptFrameResolution(frame.width(), frame.height(), frame.timestamp_us() * 1000,
			&cropped_width, &cropped_height, &out_width, &out_height)) {
			// Drop frame in order to respect frame rate constraint.
			return;
		}

		if (out_height != frame.height() || out_width != frame.width()) {
			// Video adapter has requested a down-scale. Allocate a new buffer and
			// return scaled version.
			// For simplicity, only scale here without cropping.
			rtc::scoped_refptr<I420Buffer> scaled_buffer =
				I420Buffer::Create(out_width, out_height);
			scaled_buffer->ScaleFrom(*frame.video_frame_buffer()->ToI420());
			VideoFrame::Builder new_frame_builder =
				VideoFrame::Builder()
				.set_video_frame_buffer(scaled_buffer)
				.set_rotation(kVideoRotation_0)
				.set_timestamp_us(frame.timestamp_us())
				.set_id(frame.id());
			if (frame.has_update_rect()) {
				VideoFrame::UpdateRect new_rect = frame.update_rect().ScaleWithFrame(
					frame.width(), frame.height(), 0, 0, frame.width(), frame.height(),
					out_width, out_height);
				new_frame_builder.set_update_rect(new_rect);
			}
			broadcaster_.OnFrame(new_frame_builder.build());

		}
		else {
			// No adaptations needed, just return the frame as is.
			broadcaster_.OnFrame(frame);
		}
	}

	rtc::VideoSinkWants RtcVideoCapturer::GetSinkWants() {
		return broadcaster_.wants();
	}

	void RtcVideoCapturer::AddOrUpdateSink(
		rtc::VideoSinkInterface<VideoFrame>* sink,
		const rtc::VideoSinkWants& wants) {
		broadcaster_.AddOrUpdateSink(sink, wants);
		UpdateVideoAdapter();
	}

	void RtcVideoCapturer::RemoveSink(rtc::VideoSinkInterface<VideoFrame>* sink) {
		broadcaster_.RemoveSink(sink);
		UpdateVideoAdapter();
	}

	void RtcVideoCapturer::UpdateVideoAdapter() {
		video_adapter_.OnSinkWants(broadcaster_.wants());
	}
}
