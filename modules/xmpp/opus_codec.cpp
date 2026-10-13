#include "opus_codec.h"

#include <string.h>
#include <vector>

#include "opus.h"
#include "opusfile.h"
#include "ogg/ogg.h"

OpusCodec::OpusCodec() {}
OpusCodec::~OpusCodec() {}

void OpusCodec::_bind_methods() {
	ClassDB::bind_method(D_METHOD("encode", "pcm", "rate", "channels", "bitrate"), &OpusCodec::encode);
	ClassDB::bind_method(D_METHOD("decode", "ogg"), &OpusCodec::decode);
}

static void _append_page(std::vector<unsigned char> &p_out, ogg_page &p_page) {
	p_out.insert(p_out.end(), p_page.header, p_page.header + p_page.header_len);
	p_out.insert(p_out.end(), p_page.body, p_page.body + p_page.body_len);
}

// PCM16 intercalado -> Ogg Opus (mono o estéreo). 20 ms por paquete.
PoolByteArray OpusCodec::encode(PoolByteArray p_pcm, int p_rate, int p_channels, int p_bitrate) {
	PoolByteArray out;
	if (p_rate <= 0 || p_channels <= 0 || p_pcm.size() < 2) {
		return out;
	}
	int err = OPUS_OK;
	OpusEncoder *enc = opus_encoder_create(p_rate, p_channels, OPUS_APPLICATION_VOIP, &err);
	if (enc == nullptr || err != OPUS_OK) {
		return out;
	}
	opus_encoder_ctl(enc, OPUS_SET_BITRATE(p_bitrate > 0 ? p_bitrate : 24000));
	opus_encoder_ctl(enc, OPUS_SET_SIGNAL(OPUS_SIGNAL_VOICE));
	int lookahead = 0;
	opus_encoder_ctl(enc, OPUS_GET_LOOKAHEAD(&lookahead));
	// Ogg Opus cuenta pre-skip y granulepos en muestras a 48 kHz, no al sample
	// rate de entrada (opusfile rechaza el archivo si no coinciden).
	int preskip = (int)((int64_t)lookahead * 48000 / p_rate);

	PoolByteArray::Read rd = p_pcm.read();
	const opus_int16 *pcm = (const opus_int16 *)rd.ptr();
	int total = p_pcm.size() / 2;
	int samples = total / p_channels; // muestras por canal

	ogg_stream_state os;
	ogg_stream_init(&os, 0x78787431); // serial fijo "xxt1"

	std::vector<unsigned char> buf;
	ogg_page og;
	ogg_packet op;

	// --- OpusHead ---
	unsigned char head[19];
	memcpy(head, "OpusHead", 8);
	head[8] = 1;
	head[9] = (unsigned char)p_channels;
	head[10] = preskip & 0xFF;
	head[11] = (preskip >> 8) & 0xFF;
	head[12] = p_rate & 0xFF;
	head[13] = (p_rate >> 8) & 0xFF;
	head[14] = (p_rate >> 16) & 0xFF;
	head[15] = (p_rate >> 24) & 0xFF;
	head[16] = 0;
	head[17] = 0;
	head[18] = 0;
	memset(&op, 0, sizeof(op));
	op.packet = head;
	op.bytes = 19;
	op.b_o_s = 1;
	ogg_stream_packetin(&os, &op);
	while (ogg_stream_flush(&os, &og)) {
		_append_page(buf, og);
	}

	// --- OpusTags ---
	const char *vendor = "xat";
	int vlen = (int)strlen(vendor);
	std::vector<unsigned char> tags;
	tags.resize(8 + 4 + vlen + 4);
	memcpy(&tags[0], "OpusTags", 8);
	tags[8] = vlen & 0xFF;
	tags[9] = (vlen >> 8) & 0xFF;
	tags[10] = (vlen >> 16) & 0xFF;
	tags[11] = (vlen >> 24) & 0xFF;
	memcpy(&tags[12], vendor, vlen);
	tags[12 + vlen] = 0;
	tags[13 + vlen] = 0;
	tags[14 + vlen] = 0;
	tags[15 + vlen] = 0;
	memset(&op, 0, sizeof(op));
	op.packet = &tags[0];
	op.bytes = tags.size();
	ogg_stream_packetin(&os, &op);
	while (ogg_stream_flush(&os, &og)) {
		_append_page(buf, og);
	}

	// --- Audio ---
	int frame = p_rate / 50; // 20 ms
	if (frame < 1) {
		frame = 1;
	}
	std::vector<opus_int16> pad;
	int64_t granule = preskip;
	int g48 = (int)((int64_t)frame * 48000 / p_rate);
	unsigned char pkt[8000];
	int frames_since_flush = 0;
	int pos = 0;
	while (pos < samples) {
		int n = samples - pos;
		bool last = n <= frame;
		if (n > frame) {
			n = frame;
		}
		const opus_int16 *src;
		if (n < frame) {
			// Rellenar el último frame parcial con silencio.
			pad.assign((size_t)frame * p_channels, 0);
			memcpy(&pad[0], pcm + (size_t)pos * p_channels, (size_t)n * p_channels * sizeof(opus_int16));
			src = &pad[0];
		} else {
			src = pcm + (size_t)pos * p_channels;
		}
		int len = opus_encode(enc, src, frame, pkt, sizeof(pkt));
		if (len < 0) {
			break;
		}
		granule += g48;
		memset(&op, 0, sizeof(op));
		op.packet = pkt;
		op.bytes = len;
		op.granulepos = granule;
		op.e_o_s = last ? 1 : 0;
		ogg_stream_packetin(&os, &op);
		pos += n;
		if (last || ++frames_since_flush >= 50) {
			while (ogg_stream_flush(&os, &og)) {
				_append_page(buf, og);
			}
			frames_since_flush = 0;
		}
	}
	// Volcar lo que quede.
	while (ogg_stream_flush(&os, &og)) {
		_append_page(buf, og);
	}
	ogg_stream_clear(&os);
	opus_encoder_destroy(enc);

	out.resize(buf.size());
	if (buf.size() > 0) {
		PoolByteArray::Write w = out.write();
		memcpy(w.ptr(), &buf[0], buf.size());
	}
	return out;
}

// Ogg Opus -> PCM16 intercalado + rate + channels.
Dictionary OpusCodec::decode(PoolByteArray p_ogg) {
	Dictionary res;
	if (p_ogg.size() == 0) {
		return res;
	}
	PoolByteArray::Read rd = p_ogg.read();
	int err = 0;
	OggOpusFile *of = op_open_memory((const unsigned char *)rd.ptr(), (size_t)p_ogg.size(), &err);
	if (of == nullptr) {
		return res;
	}
	int channels = op_channel_count(of, -1);
	if (channels <= 0) {
		channels = 1;
	}
	const OpusHead *oh = op_head(of, -1);
	// opusfile decodifica siempre a 48 kHz (independiente del sample rate de
	// entrada declarado en OpusHead), así que el PCM resultante es a 48 kHz.
	int rate = 48000;
	(void)oh;

	const int block = 5760; // máx. muestras por canal por lectura
	std::vector<opus_int16> pcm;
	std::vector<opus_int16> chunk((size_t)block * channels);
	int li = 0;
	while (true) {
		int n = op_read(of, &chunk[0], block * channels, &li);
		if (n <= 0) {
			break;
		}
		pcm.insert(pcm.end(), chunk.begin(), chunk.begin() + (size_t)n * channels);
	}
	op_free(of);

	PoolByteArray out;
	out.resize((int)(pcm.size() * sizeof(opus_int16)));
	if (!pcm.empty()) {
		PoolByteArray::Write w = out.write();
		memcpy(w.ptr(), &pcm[0], pcm.size() * sizeof(opus_int16));
	}
	res["pcm"] = out;
	res["rate"] = rate;
	res["channels"] = channels;
	return res;
}
