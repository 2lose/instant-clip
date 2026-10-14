// Runs the trimmer outside OBS: trim-test <input> <output> <format> <seconds> [endOffset] [precise]
// Put OBS's bin\64bit on PATH so the FFmpeg DLLs resolve.
#include "trimmer.hpp"

#include <algorithm>
#include <chrono>
#include <vector>
#include <cstdio>
#include <cstdlib>
#include <cstring>

extern "C" {
#include <libavformat/avformat.h>
}

// trim-test probe <file>: per-stream first/last timestamps and video keyframe times.
static int probe(const char *path)
{
	AVFormatContext *ctx = nullptr;
	if (avformat_open_input(&ctx, path, nullptr, nullptr) < 0 || avformat_find_stream_info(ctx, nullptr) < 0)
		return 1;
	std::printf("%s: duration %.3fs, %u streams\n", path, ctx->duration / (double)AV_TIME_BASE, ctx->nb_streams);
	std::vector<double> first(ctx->nb_streams, -1), last(ctx->nb_streams, -1);
	std::vector<double> keys;
	AVPacket *pkt = av_packet_alloc();
	while (av_read_frame(ctx, pkt) >= 0) {
		AVStream *st = ctx->streams[pkt->stream_index];
		double t = pkt->pts * av_q2d(st->time_base);
		if (first[pkt->stream_index] < 0 || t < first[pkt->stream_index])
			first[pkt->stream_index] = t;
		last[pkt->stream_index] = std::max(last[pkt->stream_index], t + pkt->duration * av_q2d(st->time_base));
		if (st->codecpar->codec_type == AVMEDIA_TYPE_VIDEO && (pkt->flags & AV_PKT_FLAG_KEY))
			keys.push_back(t);
		av_packet_unref(pkt);
	}
	for (unsigned i = 0; i < ctx->nb_streams; i++)
		std::printf("  #%u %s %s: %.3f .. %.3f\n", i, av_get_media_type_string(ctx->streams[i]->codecpar->codec_type),
			    avcodec_get_name(ctx->streams[i]->codecpar->codec_id), first[i], last[i]);
	std::printf("  keyframes:");
	for (double k : keys)
		std::printf(" %.2f", k);
	std::printf("\n");
	av_packet_free(&pkt);
	avformat_close_input(&ctx);
	return 0;
}

int main(int argc, char **argv)
{
	if (argc == 3 && std::strcmp(argv[1], "probe") == 0)
		return probe(argv[2]);
	if (argc < 5) {
		std::fprintf(stderr, "usage: trim-test <input> <output> <mp4|mkv|mov> <seconds> [endOffset] [precise]\n");
		return 2;
	}
	trimmer::Request req;
	req.input = argv[1];
	req.output = argv[2];
	req.format = argv[3];
	req.length = std::atof(argv[4]);
	req.endOffset = argc > 5 ? std::atof(argv[5]) : 0.0;
	req.mode = argc > 6 && std::strcmp(argv[6], "precise") == 0 ? trimmer::Mode::Precise : trimmer::Mode::Fast;
	int lastPct = -1;
	req.progress = [&](double p) {
		int pct = (int)(p * 100);
		if (pct / 25 != lastPct / 25)
			std::printf("  progress %d%%\n", pct);
		lastPct = pct;
	};

	auto t0 = std::chrono::steady_clock::now();
	trimmer::Result r = trimmer::trim(req);
	double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - t0).count();

	std::printf("ok=%d duration=%.3f available=%.3f short=%d fellBack=%d encoder=%s time=%.0fms\n", r.ok,
		    r.duration, r.available, r.bufferShort, r.fellBack, r.encoder.c_str(), ms);
	if (!r.ok)
		std::printf("error: %s\n", r.error.c_str());
	return r.ok ? 0 : 1;
}
