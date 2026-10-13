#ifndef XMPP_OPUS_CODEC_H
#define XMPP_OPUS_CODEC_H

#include "core/reference.h"
#include "core/pool_vector.h"
#include "core/dictionary.h"

// Códec Opus para notas de voz (XEP-0166-ish). Expone a GDScript:
//   encode(pcm16, rate, channels, bitrate) -> PoolByteArray (Ogg Opus)
//   decode(PoolByteArray ogg) -> { pcm: PoolByteArray, rate, channels }
// Usa la libopus/libogg/libopusfile vendorizadas en el motor.
class OpusCodec : public Reference {
	GDCLASS(OpusCodec, Reference);

protected:
	static void _bind_methods();

public:
	OpusCodec();
	~OpusCodec();

	PoolByteArray encode(PoolByteArray p_pcm, int p_rate, int p_channels, int p_bitrate);
	Dictionary decode(PoolByteArray p_ogg);
};

#endif // XMPP_OPUS_CODEC_H
