#include "trimmer.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <vector>

extern "C" {
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/opt.h>
#include <libavutil/pixdesc.h>
#include <libswscale/swscale.h>
}

namespace trimmer {
namespace {

namespace fs = std::filesystem;

fs::path u8(const std::string &s)
{
	return fs::path(std::u8string(reinterpret_cast<const char8_t *>(s.data()), s.size()));
}

std::string av_err(int code)
{
	char buf[AV_ERROR_MAX_STRING_SIZE] = {};
	av_strerror(code, buf, sizeof(buf));
	return buf;
}

constexpr int64_t kNoTs = AV_NOPTS_VALUE;

struct Input {
	AVFormatContext *ctx = nullptr;
	int video = -1;
	std::vector<int> map; // input stream -> output stream, -1 = dropped
	int64_t start = 0;    // AV_TIME_BASE units
	int64_t end = 0;

	~Input() { avformat_close_input(&ctx); }
};

struct Output {
	AVFormatContext *ctx = nullptr;
	std::string tmpPath;
	bool header = false;

	~Output()
	{
		if (ctx) {
			if (ctx->pb && !(ctx->oformat->flags & AVFMT_NOFILE))
				avio_closep(&ctx->pb);
			avformat_free_context(ctx);
		}
		if (!tmpPath.empty()) {
			std::error_code ec;
			fs::remove(u8(tmpPath), ec);
		}
	}
};

struct Window {
	int64_t cut = 0; // AV_TIME_BASE units
	int64_t end = 0;
	double available = 0.0;
	bool bufferShort = false;
};

struct PacketDeleter {
	void operator()(AVPacket *p) const { av_packet_free(&p); }
};
using PacketPtr = std::unique_ptr<AVPacket, PacketDeleter>;

struct FrameDeleter {
	void operator()(AVFrame *f) const { av_frame_free(&f); }
};
using FramePtr = std::unique_ptr<AVFrame, FrameDeleter>;

struct CodecDeleter {
	void operator()(AVCodecContext *c) const { avcodec_free_context(&c); }
};
using CodecPtr = std::unique_ptr<AVCodecContext, CodecDeleter>;

int64_t packet_time(const AVPacket *pkt, AVRational tb)
{
	int64_t ts = pkt->pts != kNoTs ? pkt->pts : pkt->dts;
	if (ts == kNoTs)
		return kNoTs;
	return av_rescale_q(ts, tb, AV_TIME_BASE_Q);
}

// Reads every packet to find where the file really ends. Only used when the
// container doesn't carry a trustworthy duration.
int64_t scan_end(AVFormatContext *ctx)
{
	int64_t end = kNoTs;
	PacketPtr pkt(av_packet_alloc());
	while (av_read_frame(ctx, pkt.get()) >= 0) {
		AVStream *st = ctx->streams[pkt->stream_index];
		int64_t ts = pkt->pts != kNoTs ? pkt->pts : pkt->dts;
		if (ts != kNoTs) {
			int64_t t = av_rescale_q(ts + std::max<int64_t>(pkt->duration, 0), st->time_base, AV_TIME_BASE_Q);
			end = end == kNoTs ? t : std::max(end, t);
		}
		av_packet_unref(pkt.get());
	}
	avformat_seek_file(ctx, -1, INT64_MIN, 0, INT64_MAX, 0);
	return end;
}

bool open_input(const std::string &path, Input &in, std::string &err)
{
	int ret = avformat_open_input(&in.ctx, path.c_str(), nullptr, nullptr);
	if (ret < 0) {
		err = "Couldn't open the replay: " + av_err(ret);
		return false;
	}
	ret = avformat_find_stream_info(in.ctx, nullptr);
	if (ret < 0) {
		err = "Couldn't read the replay: " + av_err(ret);
		return false;
	}

	in.video = av_find_best_stream(in.ctx, AVMEDIA_TYPE_VIDEO, -1, -1, nullptr, 0);
	if (in.video >= 0 && (in.ctx->streams[in.video]->disposition & AV_DISPOSITION_ATTACHED_PIC))
		in.video = -1;

	int next = 0;
	in.map.assign(in.ctx->nb_streams, -1);
	for (unsigned i = 0; i < in.ctx->nb_streams; i++) {
		AVStream *st = in.ctx->streams[i];
		AVMediaType type = st->codecpar->codec_type;
		if (st->disposition & AV_DISPOSITION_ATTACHED_PIC)
			continue;
		if ((type == AVMEDIA_TYPE_VIDEO && (int)i == in.video) || type == AVMEDIA_TYPE_AUDIO)
			in.map[i] = next++;
	}
	if (next == 0) {
		err = "The replay has no audio or video.";
		return false;
	}

	in.start = in.ctx->start_time != kNoTs ? in.ctx->start_time : 0;
	if (in.ctx->duration > 0 && in.ctx->duration_estimation_method != AVFMT_DURATION_FROM_BITRATE) {
		in.end = in.start + in.ctx->duration;
	} else {
		int64_t end = scan_end(in.ctx);
		if (end == kNoTs) {
			err = "Couldn't work out how long the replay is.";
			return false;
		}
		in.end = end;
	}
	return true;
}

Window make_window(const Input &in, const Request &req)
{
	Window w;
	w.available = (in.end - in.start) / (double)AV_TIME_BASE;
	int64_t offset = (int64_t)(std::max(req.endOffset, 0.0) * AV_TIME_BASE);
	// Never trim more off the end than would leave less than a second.
	offset = std::min(offset, std::max<int64_t>(in.end - in.start - AV_TIME_BASE, 0));
	w.end = in.end - offset;
	w.cut = w.end - (int64_t)(req.length * AV_TIME_BASE);
	if (w.cut <= in.start) {
		// Half a second of slack: a 30s clip from a "30s" buffer isn't short.
		w.bufferShort = (in.start - w.cut) > AV_TIME_BASE / 2;
		w.cut = in.start;
	}
	return w;
}

const char *muxer_name(const std::string &format)
{
	if (format == "mkv")
		return "matroska";
	if (format == "mov")
		return "mov";
	return "mp4";
}

bool is_mov_family(const std::string &format)
{
	return format == "mp4" || format == "mov";
}

bool create_output(const Request &req, Output &out, std::string &err)
{
	out.tmpPath = req.output + ".part";
	int ret = avformat_alloc_output_context2(&out.ctx, nullptr, muxer_name(req.format), out.tmpPath.c_str());
	if (ret < 0 || !out.ctx) {
		err = "Couldn't create the clip: " + av_err(ret);
		return false;
	}
	out.ctx->avoid_negative_ts = AVFMT_AVOID_NEG_TS_MAKE_NON_NEGATIVE;
	return true;
}

// Only tags that still hold after cutting (mkv also carries a DURATION tag, which would be stale).
void copy_stream_tags(AVStream *os, const AVStream *is)
{
	for (const char *key : {"title", "language", "handler_name"}) {
		if (const AVDictionaryEntry *e = av_dict_get(is->metadata, key, nullptr, 0))
			av_dict_set(&os->metadata, key, e->value, 0);
	}
}

AVStream *add_copy_stream(Output &out, const Request &req, AVStream *is, std::string &err)
{
	AVStream *os = avformat_new_stream(out.ctx, nullptr);
	if (!os) {
		err = "Out of memory.";
		return nullptr;
	}
	avcodec_parameters_copy(os->codecpar, is->codecpar);
	os->codecpar->codec_tag = 0;
	if (is_mov_family(req.format) && os->codecpar->codec_id == AV_CODEC_ID_HEVC)
		os->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1'); // plays in QuickTime / Apple apps
	os->time_base = is->time_base;
	os->disposition = is->disposition;
	copy_stream_tags(os, is);
	return os;
}

bool write_header(Output &out, const Request &req, std::string &err)
{
	int ret = 0;
	if (!(out.ctx->oformat->flags & AVFMT_NOFILE)) {
		ret = avio_open(&out.ctx->pb, out.tmpPath.c_str(), AVIO_FLAG_WRITE);
		if (ret < 0) {
			err = "Couldn't write to the clip folder: " + av_err(ret);
			return false;
		}
	}
	AVDictionary *opts = nullptr;
	if (is_mov_family(req.format))
		av_dict_set(&opts, "movflags", "+faststart", 0);
	ret = avformat_write_header(out.ctx, &opts);
	av_dict_free(&opts);
	if (ret < 0) {
		err = "The " + req.format + " container can't hold this replay's codecs (" + av_err(ret) +
		      "). Try MKV.";
		return false;
	}
	out.header = true;
	return true;
}

bool finish(Output &out, const Request &req, std::string &err)
{
	int ret = av_write_trailer(out.ctx);
	if (out.ctx->pb && !(out.ctx->oformat->flags & AVFMT_NOFILE))
		avio_closep(&out.ctx->pb);
	if (ret < 0) {
		err = "Couldn't finish the clip: " + av_err(ret);
		return false;
	}
	std::error_code ec;
	fs::rename(u8(out.tmpPath), u8(req.output), ec);
	if (ec) {
		err = "Couldn't rename the clip: " + ec.message();
		return false;
	}
	out.tmpPath.clear();
	return true;
}

void seek_to(Input &in, int64_t cut)
{
	if (cut <= in.start)
		return;
	// Lands on the keyframe at or before `cut` (default stream = video).
	if (av_seek_frame(in.ctx, -1, cut, AVSEEK_FLAG_BACKWARD) < 0)
		avformat_seek_file(in.ctx, -1, INT64_MIN, in.start, in.start, 0);
}

class Progress {
public:
	Progress(const Request &req, const Window &w) : fn(req.progress), from(w.cut), span(std::max<int64_t>(w.end - w.cut, 1)) {}

	void update(int64_t t)
	{
		if (!fn || t == kNoTs)
			return;
		double p = std::clamp((t - from) / (double)span, 0.0, 1.0);
		if (p - last >= 0.01) {
			last = p;
			fn(p);
		}
	}

private:
	std::function<void(double)> fn;
	int64_t from;
	int64_t span;
	double last = 0.0;
};

// Shifts a packet so `base` becomes zero and writes it. Returns false on a mux error.
bool write_shifted(Input &in, Output &out, AVPacket *pkt, int64_t base, std::string &err)
{
	AVStream *is = in.ctx->streams[pkt->stream_index];
	AVStream *os = out.ctx->streams[in.map[pkt->stream_index]];
	int64_t off = av_rescale_q(base, AV_TIME_BASE_Q, is->time_base);
	if (pkt->pts != kNoTs)
		pkt->pts -= off;
	if (pkt->dts != kNoTs)
		pkt->dts -= off;
	av_packet_rescale_ts(pkt, is->time_base, os->time_base);
	pkt->stream_index = os->index;
	pkt->pos = -1;
	int ret = av_interleaved_write_frame(out.ctx, pkt);
	if (ret < 0) {
		err = "Couldn't write the clip: " + av_err(ret);
		return false;
	}
	return true;
}

// True when a packet should be dropped because it lies before `base`.
bool before_base(Input &in, const AVPacket *pkt, int64_t base)
{
	AVStream *is = in.ctx->streams[pkt->stream_index];
	int64_t ts = pkt->pts != kNoTs ? pkt->pts : pkt->dts;
	if (ts == kNoTs)
		return false;
	int64_t b = av_rescale_q(base, AV_TIME_BASE_Q, is->time_base);
	if (pkt->stream_index == in.video)
		return ts < b;
	return ts + std::max<int64_t>(pkt->duration, 0) <= b;
}

/* ------------------------------------------------------------------------ */
/* Fast: stream copy from the last keyframe at or before the cut.            */

Result run_fast(Input &in, const Request &req)
{
	Result res;
	Window w = make_window(in, req);
	res.available = w.available;
	res.bufferShort = w.bufferShort;

	Output out;
	if (!create_output(req, out, res.error))
		return res;
	for (unsigned i = 0; i < in.ctx->nb_streams; i++) {
		if (in.map[i] >= 0 && !add_copy_stream(out, req, in.ctx->streams[i], res.error))
			return res;
	}

	seek_to(in, w.cut);

	Progress progress(req, w);
	std::vector<PacketPtr> pending;
	int64_t keyframe = kNoTs;
	int64_t base = kNoTs;
	int64_t lastWritten = kNoTs;
	size_t mapped = std::count_if(in.map.begin(), in.map.end(), [](int m) { return m >= 0; });
	std::vector<bool> pastEnd(in.ctx->nb_streams, false);
	size_t pastEndCount = 0;

	auto write = [&](AVPacket *pkt) -> bool {
		int64_t t = packet_time(pkt, in.ctx->streams[pkt->stream_index]->time_base);
		if (t != kNoTs && t >= w.end) {
			if (!pastEnd[pkt->stream_index]) {
				pastEnd[pkt->stream_index] = true;
				pastEndCount++;
			}
			return true;
		}
		if (before_base(in, pkt, base))
			return true;
		if (t != kNoTs) {
			int64_t tEnd = t + av_rescale_q(std::max<int64_t>(pkt->duration, 0),
							in.ctx->streams[pkt->stream_index]->time_base, AV_TIME_BASE_Q);
			lastWritten = lastWritten == kNoTs ? tEnd : std::max(lastWritten, tEnd);
		}
		progress.update(t);
		return write_shifted(in, out, pkt, base, res.error);
	};

	auto begin = [&](int64_t at) -> bool {
		base = at;
		if (!write_header(out, req, res.error))
			return false;
		for (auto &p : pending) {
			if (!write(p.get()))
				return false;
		}
		pending.clear();
		return true;
	};

	if (in.video < 0 && !begin(w.cut))
		return res;

	PacketPtr pkt(av_packet_alloc());
	while (pastEndCount < mapped && av_read_frame(in.ctx, pkt.get()) >= 0) {
		int si = pkt->stream_index;
		if (si >= (int)in.map.size() || in.map[si] < 0) {
			av_packet_unref(pkt.get());
			continue;
		}

		if (base == kNoTs) {
			int64_t t = packet_time(pkt.get(), in.ctx->streams[si]->time_base);
			bool key = si == in.video && (pkt->flags & AV_PKT_FLAG_KEY);
			if (key && (keyframe == kNoTs || t <= w.cut)) {
				pending.clear();
				keyframe = t;
			}
			if (keyframe == kNoTs) {
				av_packet_unref(pkt.get());
				continue;
			}
			pending.emplace_back(av_packet_clone(pkt.get()));
			av_packet_unref(pkt.get());
			if (si == in.video && t != kNoTs && t > w.cut && !begin(keyframe))
				return res;
			continue;
		}

		bool ok = write(pkt.get());
		av_packet_unref(pkt.get());
		if (!ok)
			return res;
	}

	if (base == kNoTs) {
		if (keyframe == kNoTs) {
			res.error = "The replay has no keyframe to start the clip on.";
			return res;
		}
		if (!begin(keyframe))
			return res;
	}

	if (!finish(out, req, res.error))
		return res;

	res.ok = true;
	res.duration = lastWritten != kNoTs ? (lastWritten - base) / (double)AV_TIME_BASE : 0.0;
	return res;
}

/* ------------------------------------------------------------------------ */
/* Precise: re-encode video from the exact cut, copy audio.                  */

struct EncoderChoice {
	const char *name;
	bool hardware;
};

std::vector<EncoderChoice> encoder_candidates(AVCodecID id)
{
	std::vector<EncoderChoice> h264 = {{"h264_nvenc", true}, {"h264_amf", true}, {"h264_qsv", true}, {"libx264", false}};
	std::vector<EncoderChoice> hevc = {{"hevc_nvenc", true}, {"hevc_amf", true}, {"hevc_qsv", true}};
	std::vector<EncoderChoice> av1 = {{"av1_nvenc", true}, {"av1_amf", true}, {"av1_qsv", true}};

	std::vector<EncoderChoice> list;
	if (id == AV_CODEC_ID_AV1)
		list.insert(list.end(), av1.begin(), av1.end());
	if (id == AV_CODEC_ID_AV1 || id == AV_CODEC_ID_HEVC)
		list.insert(list.end(), hevc.begin(), hevc.end());
	list.insert(list.end(), h264.begin(), h264.end());
	return list;
}

AVPixelFormat pick_pix_fmt(AVCodecContext *ctx, const AVCodec *codec, AVPixelFormat want)
{
	const void *cfg = nullptr;
	int n = 0;
	if (avcodec_get_supported_config(ctx, codec, AV_CODEC_CONFIG_PIX_FORMAT, 0, &cfg, &n) < 0 || !cfg || n == 0)
		return want;
	auto fmts = static_cast<const AVPixelFormat *>(cfg);
	for (int i = 0; i < n; i++) {
		if (fmts[i] == want)
			return want;
	}
	const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get(want);
	bool deep = desc && desc->comp[0].depth > 8;
	for (AVPixelFormat pref : deep ? std::initializer_list<AVPixelFormat>{AV_PIX_FMT_P010LE, AV_PIX_FMT_YUV420P10LE}
				       : std::initializer_list<AVPixelFormat>{AV_PIX_FMT_NV12, AV_PIX_FMT_YUV420P}) {
		for (int i = 0; i < n; i++) {
			if (fmts[i] == pref)
				return pref;
		}
	}
	for (int i = 0; i < n; i++) {
		if (fmts[i] == AV_PIX_FMT_NV12 || fmts[i] == AV_PIX_FMT_YUV420P)
			return fmts[i];
	}
	return fmts[0];
}

CodecPtr open_encoder(const AVFrame *frame, AVStream *is, AVRational fps, int64_t bitrate, bool globalHeader,
		      std::string &usedName)
{
	for (const EncoderChoice &choice : encoder_candidates(is->codecpar->codec_id)) {
		const AVCodec *codec = avcodec_find_encoder_by_name(choice.name);
		if (!codec)
			continue;
		CodecPtr enc(avcodec_alloc_context3(codec));
		enc->width = frame->width;
		enc->height = frame->height;
		enc->sample_aspect_ratio = frame->sample_aspect_ratio;
		enc->time_base = is->time_base;
		enc->framerate = fps;
		enc->pix_fmt = pick_pix_fmt(enc.get(), codec, (AVPixelFormat)frame->format);
		enc->color_range = frame->color_range;
		enc->color_primaries = frame->color_primaries;
		enc->color_trc = frame->color_trc;
		enc->colorspace = frame->colorspace;
		enc->chroma_sample_location = frame->chroma_location;
		enc->gop_size = fps.num > 0 && fps.den > 0 ? std::max(1, 2 * fps.num / fps.den) : 120;
		enc->max_b_frames = 0;
		enc->thread_count = 0;
		if (globalHeader)
			enc->flags |= AV_CODEC_FLAG_GLOBAL_HEADER;

		AVDictionary *opts = nullptr;
		if (std::strcmp(choice.name, "libx264") == 0) {
			av_dict_set(&opts, "preset", "veryfast", 0);
			av_dict_set(&opts, "crf", "17", 0);
		} else if (std::strstr(choice.name, "nvenc")) {
			enc->bit_rate = bitrate;
			enc->rc_max_rate = bitrate * 2;
			enc->rc_buffer_size = (int)std::min<int64_t>(bitrate * 2, INT32_MAX);
			av_dict_set(&opts, "preset", "p5", 0);
			av_dict_set(&opts, "rc", "vbr", 0);
			av_dict_set(&opts, "cq", "19", 0);
		} else {
			enc->bit_rate = bitrate;
			enc->rc_max_rate = bitrate * 2;
			enc->rc_buffer_size = (int)std::min<int64_t>(bitrate * 2, INT32_MAX);
		}

		int ret = avcodec_open2(enc.get(), codec, &opts);
		av_dict_free(&opts);
		if (ret >= 0) {
			usedName = choice.name;
			return enc;
		}
	}
	return nullptr;
}

enum class PreciseOutcome { Done, Failed, Unsupported };

PreciseOutcome run_precise(Input &in, const Request &req, Result &res)
{
	Window w = make_window(in, req);
	res.available = w.available;
	res.bufferShort = w.bufferShort;
	if (in.video < 0)
		return PreciseOutcome::Unsupported;

	AVStream *vs = in.ctx->streams[in.video];
	const AVCodec *decoder = avcodec_find_decoder(vs->codecpar->codec_id);
	if (!decoder)
		return PreciseOutcome::Unsupported;
	CodecPtr dec(avcodec_alloc_context3(decoder));
	avcodec_parameters_to_context(dec.get(), vs->codecpar);
	dec->pkt_timebase = vs->time_base;
	dec->thread_count = 0;
	if (avcodec_open2(dec.get(), decoder, nullptr) < 0)
		return PreciseOutcome::Unsupported;

	Output out;
	if (!create_output(req, out, res.error))
		return PreciseOutcome::Failed;
	AVStream *videoOut = nullptr;
	for (unsigned i = 0; i < in.ctx->nb_streams; i++) {
		if (in.map[i] < 0)
			continue;
		if ((int)i == in.video) {
			videoOut = avformat_new_stream(out.ctx, nullptr);
			videoOut->disposition = vs->disposition;
			copy_stream_tags(videoOut, vs);
		} else if (!add_copy_stream(out, req, in.ctx->streams[i], res.error)) {
			return PreciseOutcome::Failed;
		}
	}

	AVRational fps = av_guess_frame_rate(in.ctx, vs, nullptr);
	int64_t bitrate = vs->codecpar->bit_rate > 0 ? vs->codecpar->bit_rate
			  : in.ctx->bit_rate > 0      ? in.ctx->bit_rate * 9 / 10
						      : 20'000'000;
	bool globalHeader = out.ctx->oformat->flags & AVFMT_GLOBALHEADER;

	seek_to(in, w.cut);

	Progress progress(req, w);
	const int64_t base = w.cut;
	const int64_t baseVideo = av_rescale_q(base, AV_TIME_BASE_Q, vs->time_base);
	CodecPtr enc;
	SwsContext *sws = nullptr;
	FramePtr converted;
	std::vector<PacketPtr> queuedAudio;
	bool videoDone = false;
	bool sawKey = false;
	int64_t lastWritten = kNoTs;
	std::vector<bool> pastEnd(in.ctx->nb_streams, false);

	struct SwsGuard {
		SwsContext *&s;
		~SwsGuard() { sws_freeContext(s); }
	} swsGuard{sws};

	auto note_written = [&](int64_t t) {
		if (t != kNoTs)
			lastWritten = lastWritten == kNoTs ? t : std::max(lastWritten, t);
	};

	auto drain_encoder = [&]() -> bool {
		PacketPtr ep(av_packet_alloc());
		while (true) {
			int ret = avcodec_receive_packet(enc.get(), ep.get());
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				return true;
			if (ret < 0) {
				res.error = "Encoding failed: " + av_err(ret);
				return false;
			}
			av_packet_rescale_ts(ep.get(), enc->time_base, videoOut->time_base);
			ep->stream_index = videoOut->index;
			ret = av_interleaved_write_frame(out.ctx, ep.get());
			if (ret < 0) {
				res.error = "Couldn't write the clip: " + av_err(ret);
				return false;
			}
		}
	};

	auto handle_frame = [&](AVFrame *frame) -> PreciseOutcome {
		int64_t ts = frame->best_effort_timestamp != kNoTs ? frame->best_effort_timestamp : frame->pts;
		if (ts == kNoTs)
			return PreciseOutcome::Done;
		int64_t t = av_rescale_q(ts, vs->time_base, AV_TIME_BASE_Q);
		if (t < base)
			return PreciseOutcome::Done;
		if (t >= w.end) {
			videoDone = true;
			return PreciseOutcome::Done;
		}

		if (!enc) {
			enc = open_encoder(frame, vs, fps, bitrate, globalHeader, res.encoder);
			if (!enc)
				return PreciseOutcome::Unsupported;
			avcodec_parameters_from_context(videoOut->codecpar, enc.get());
			if (is_mov_family(req.format) && videoOut->codecpar->codec_id == AV_CODEC_ID_HEVC)
				videoOut->codecpar->codec_tag = MKTAG('h', 'v', 'c', '1');
			videoOut->time_base = enc->time_base;
			videoOut->avg_frame_rate = fps;
			if (!write_header(out, req, res.error))
				return PreciseOutcome::Failed;
			for (auto &p : queuedAudio) {
				if (!write_shifted(in, out, p.get(), base, res.error))
					return PreciseOutcome::Failed;
			}
			queuedAudio.clear();
		}

		AVFrame *src = frame;
		if (frame->format != enc->pix_fmt) {
			if (!converted) {
				converted.reset(av_frame_alloc());
				converted->format = enc->pix_fmt;
				converted->width = enc->width;
				converted->height = enc->height;
				if (av_frame_get_buffer(converted.get(), 0) < 0) {
					res.error = "Out of memory.";
					return PreciseOutcome::Failed;
				}
			}
			sws = sws_getCachedContext(sws, frame->width, frame->height, (AVPixelFormat)frame->format,
						   enc->width, enc->height, enc->pix_fmt, SWS_BILINEAR, nullptr, nullptr,
						   nullptr);
			if (!sws || av_frame_make_writable(converted.get()) < 0 ||
			    sws_scale(sws, frame->data, frame->linesize, 0, frame->height, converted->data,
				      converted->linesize) < 0) {
				res.error = "Pixel format conversion failed.";
				return PreciseOutcome::Failed;
			}
			av_frame_copy_props(converted.get(), frame);
			src = converted.get();
		}

		src->pts = ts - baseVideo;
		src->pict_type = AV_PICTURE_TYPE_NONE;
		int ret = avcodec_send_frame(enc.get(), src);
		if (ret < 0) {
			res.error = "Encoding failed: " + av_err(ret);
			return PreciseOutcome::Failed;
		}
		note_written(t + av_rescale_q(std::max<int64_t>(frame->duration, 0), vs->time_base, AV_TIME_BASE_Q));
		progress.update(t);
		return drain_encoder() ? PreciseOutcome::Done : PreciseOutcome::Failed;
	};

	FramePtr frame(av_frame_alloc());
	auto drain_decoder = [&]() -> PreciseOutcome {
		while (true) {
			int ret = avcodec_receive_frame(dec.get(), frame.get());
			if (ret == AVERROR(EAGAIN) || ret == AVERROR_EOF)
				return PreciseOutcome::Done;
			if (ret < 0) {
				res.error = "Decoding failed: " + av_err(ret);
				return PreciseOutcome::Failed;
			}
			PreciseOutcome o = handle_frame(frame.get());
			av_frame_unref(frame.get());
			if (o != PreciseOutcome::Done)
				return o;
		}
	};

	auto audio_finished = [&]() {
		for (unsigned i = 0; i < in.ctx->nb_streams; i++) {
			if (in.map[i] >= 0 && (int)i != in.video && !pastEnd[i])
				return false;
		}
		return true;
	};

	PacketPtr pkt(av_packet_alloc());
	while (!(videoDone && audio_finished()) && av_read_frame(in.ctx, pkt.get()) >= 0) {
		int si = pkt->stream_index;
		if (si >= (int)in.map.size() || in.map[si] < 0) {
			av_packet_unref(pkt.get());
			continue;
		}

		if (si == in.video) {
			if (videoDone || (!sawKey && !(pkt->flags & AV_PKT_FLAG_KEY))) {
				av_packet_unref(pkt.get());
				continue;
			}
			sawKey = true;
			int ret = avcodec_send_packet(dec.get(), pkt.get());
			av_packet_unref(pkt.get());
			if (ret < 0 && ret != AVERROR(EAGAIN))
				continue; // skip a corrupt packet rather than failing the whole clip
			PreciseOutcome o = drain_decoder();
			if (o != PreciseOutcome::Done)
				return o;
			continue;
		}

		int64_t t = packet_time(pkt.get(), in.ctx->streams[si]->time_base);
		if (t != kNoTs && t >= w.end) {
			pastEnd[si] = true;
			av_packet_unref(pkt.get());
			continue;
		}
		if (before_base(in, pkt.get(), base)) {
			av_packet_unref(pkt.get());
			continue;
		}
		if (!out.header) {
			queuedAudio.emplace_back(av_packet_clone(pkt.get()));
			av_packet_unref(pkt.get());
			continue;
		}
		bool ok = write_shifted(in, out, pkt.get(), base, res.error);
		av_packet_unref(pkt.get());
		if (!ok)
			return PreciseOutcome::Failed;
	}

	if (!videoDone) {
		avcodec_send_packet(dec.get(), nullptr);
		PreciseOutcome o = drain_decoder();
		if (o != PreciseOutcome::Done)
			return o;
	}
	if (!enc) {
		res.error = "No video frames fell inside the clip.";
		return PreciseOutcome::Failed;
	}
	avcodec_send_frame(enc.get(), nullptr);
	if (!drain_encoder())
		return PreciseOutcome::Failed;
	if (!finish(out, req, res.error))
		return PreciseOutcome::Failed;

	res.ok = true;
	res.duration = lastWritten != kNoTs ? (lastWritten - base) / (double)AV_TIME_BASE : 0.0;
	return PreciseOutcome::Done;
}

} // namespace

Result trim(const Request &req)
{
	Result res;
	{
		Input in;
		if (!open_input(req.input, in, res.error))
			return res;
		if (req.mode == Mode::Fast)
			return run_fast(in, req);

		PreciseOutcome o = run_precise(in, req, res);
		if (o != PreciseOutcome::Unsupported)
			return res;
	}

	// Precise wasn't possible (no decoder/encoder for this codec): cut on keyframes instead.
	Input in;
	if (!open_input(req.input, in, res.error))
		return res;
	Result fast = run_fast(in, req);
	fast.fellBack = true;
	return fast;
}

} // namespace trimmer
