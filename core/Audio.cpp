#include "Audio.hpp"
#include "Cranked.hpp"

using namespace cranked;

SoundSource_32::SoundSource_32(Cranked &cranked, ResourceType type, void *address) : NativeResource(cranked, type, address) {
    cranked.audio.sourceMappings[address] = this;
}

SoundSource_32::SoundSource_32(const SoundSource_32 &other, ResourceType type, void *address) : SoundSource_32(other.cranked, type, address) {
    leftVolume = other.leftVolume;
    rightVolume = other.rightVolume;
    playing = other.playing;
}

SoundSource_32::~SoundSource_32() {
    cranked.audio.signalMappings.erase(address);
}

AudioSample_32::AudioSample_32(Cranked &cranked, int size)
    : NativeResource(cranked, ResourceType::AudioSample, this), data(vheap_vector(size, cranked.heap.allocator<uint8>())) {}

SoundEffect_32::SoundEffect_32(Cranked &cranked, ResourceType type, void *address) : NativeResource(cranked, type, address) {
    cranked.audio.effectMappings[address] = this;
}

SoundEffect_32::~SoundEffect_32() {
    cranked.audio.effectMappings.erase(address);
}

SoundChannel_32::SoundChannel_32(Cranked &cranked)
    : NativeResource(cranked, ResourceType::Channel, this), wetSignal(cranked.heap.construct<SoundChannelSignal_32>(this, true)), drySignal(cranked.heap.construct<SoundChannelSignal_32>(this, false)) {}

SoundChannelSignal_32::SoundChannelSignal_32(SoundChannel channel, bool wet)
    : PDSynthSignalValue_32(channel->cranked, ResourceType::ChannelSignal, this), channel(channel), wet(wet) {}

AudioPlayerBase::AudioPlayerBase(Cranked &cranked, ResourceType type, void *address) : SoundSource_32(cranked, type, address) {}

AudioPlayerBase::AudioPlayerBase(const AudioPlayerBase &other, ResourceType type, void *address) : AudioPlayerBase(other.cranked, type, address) {
    rate = other.rate;
}

FilePlayer_32::FilePlayer_32(Cranked &cranked) : AudioPlayerBase(cranked, ResourceType::FilePlayer, this) {}

SamplePlayer_32::SamplePlayer_32(Cranked &cranked) : AudioPlayerBase(cranked, ResourceType::SamplePlayer, this) {}

SamplePlayer SamplePlayer_32::copy() {
    auto player = cranked.audio.allocateSource<SamplePlayer_32>();
    player->sample = sample;
    player->leftVolume = leftVolume;
    player->rightVolume = rightVolume;
    player->rate = rate;
    return player;
}

PDSynthSignalValue_32::PDSynthSignalValue_32(Cranked &cranked, ResourceType type, void *address) : NativeResource(cranked, type, address) {
    cranked.audio.signalMappings[address] = this;
}

PDSynthSignalValue_32::~PDSynthSignalValue_32() {
    cranked.audio.signalMappings.erase(address);
}

PDSynthSignal_32::PDSynthSignal_32(Cranked &cranked, ResourceType type, void *address) : PDSynthSignalValue_32(cranked, type, address) {
    cranked.audio.signalMappings[this] = dynamic_cast<SynthSignalValue>(this);
}

PDSynthSignal_32::~PDSynthSignal_32() {
    cranked.audio.signalMappings.erase(this);
}

SequenceTrack_32::SequenceTrack_32(Cranked &cranked) : SoundSource_32(cranked, ResourceType::Track, this) {}

SoundSequence_32::SoundSequence_32(Cranked &cranked) : SoundSource_32(cranked, ResourceType::Sequence, this) {}

TwoPoleFilter_32::TwoPoleFilter_32(Cranked &cranked) : SoundEffect_32(cranked, ResourceType::TwoPoleFilter, this) {}

OnePoleFilter_32::OnePoleFilter_32(Cranked &cranked) : SoundEffect_32(cranked, ResourceType::OnePoleFilter, this) {}

BitCrusher_32::BitCrusher_32(Cranked &cranked) : SoundEffect_32(cranked, ResourceType::BitCrusher, this) {}

RingModulator_32::RingModulator_32(Cranked &cranked) : SoundEffect_32(cranked, ResourceType::RingModulator, this) {}

DelayLine_32::DelayLine_32(Cranked &cranked, int length, bool stereo)
    : SoundEffect_32(cranked, ResourceType::DelayLine, this), stereo(stereo), data((stereo ? 2 : 1) * length) {}

DelayLineTap_32::DelayLineTap_32(Cranked &cranked, DelayLine_32 *delayLine, int delay)
    : SoundSource_32(cranked, ResourceType::DelayLineTap, this), delayLine(delayLine), delayFrames(delay) {}

Overdrive_32::Overdrive_32(Cranked &cranked) : SoundEffect_32(cranked, ResourceType::Overdrive, this) {}

CustomSoundSource_32::CustomSoundSource_32(Cranked &cranked, cref_t func, void *userdata, bool stereo)
    : SoundSource_32(cranked, ResourceType::CustomSource, this), func(func), userdata(userdata), stereo(stereo) {}

CustomSoundEffect_32::CustomSoundEffect_32(Cranked &cranked, cref_t func, void *userdata)
    : SoundEffect_32(cranked, ResourceType::CustomEffect, this), func(func), userdata(userdata) {}

CustomSoundSignal_32::CustomSoundSignal_32(Cranked &cranked, cref_t stepFunc, cref_t noteOnFunc, cref_t noteOffFunc, cref_t deallocFunc, void *userdata)
    : PDSynthSignal_32(cranked, ResourceType::CustomSource, this), stepFunc(stepFunc), noteOnFunc(noteOnFunc), noteOffFunc(noteOffFunc), deallocFunc(deallocFunc), userdata(userdata) {}

PDSynthLFO_32::PDSynthLFO_32(Cranked &cranked, LFOType type) : PDSynthSignal_32(cranked, ResourceType::LFO, this), type(type) {}

PDSynthEnvelope_32::PDSynthEnvelope_32(Cranked &cranked, float attack, float decay, float sustain, float release)
        : PDSynthSignal_32(cranked, ResourceType::Envelope, this), attack(attack), decay(decay), sustain(sustain), release(release) {}

PDSynth_32::PDSynth_32(Cranked &cranked) : SoundSource_32(cranked, ResourceType::Synth, this), envelope(cranked.heap.construct<PDSynthEnvelope_32>(cranked)) {}

ControlSignal_32::ControlSignal_32(Cranked &cranked) : PDSynthSignal_32(cranked, ResourceType::ControlSignal, this) {}

PDSynthInstrument_32::PDSynthInstrument_32(Cranked &cranked) : SoundSource_32(cranked, ResourceType::Instrument, this) {}

Audio::Audio(Cranked &cranked) : cranked(cranked), heap(cranked.heap) {}

AudioSample Audio::loadSample(const string &path) {
    auto audio = cranked.rom->getAudio(path);
    // Store decoded 16-bit PCM so playback doesn't need to know the source format
    auto sample = heap.construct<AudioSample_32>(cranked, (int)(audio.samples.size() * sizeof(int16)));
    memcpy(sample->data.data(), audio.samples.data(), audio.samples.size() * sizeof(int16));
    sample->soundFormat = soundFormatIsStereo(audio.soundFormat) ? SoundFormat::Stereo16bit : SoundFormat::Mono16bit;
    sample->sampleRate = audio.sampleRate;
    return sample;
}

// Shared playback for sample-backed players: mixes into left/right and advances the cursor,
// handling loop ranges, repeat counts (0 = endless, -1 = ping-pong), and completion
static void playSampleAudio(AudioPlayerBase &player, AudioSample_32 *sample, int16 *left, int16 *right, int length) {
    if (!player.playing or player.paused or !sample or sample->data.empty() or sample->sampleRate == 0)
        return;
    bool stereo = soundFormatIsStereo(sample->soundFormat);
    auto pcm = (const int16 *)sample->data.data();
    int totalFrames = (int)(sample->data.size() / (stereo ? 4 : 2));
    int start = player.loopStart > 0 ? min(player.loopStart, totalFrames) : 0;
    int end = player.loopEnd > 0 ? min(player.loopEnd, totalFrames) : totalFrames;
    if (end <= start) {
        player.playing = false;
        return;
    }
    double step = (double)player.rate * sample->sampleRate / AUDIO_SAMPLING_RATE;
    if (step <= 0)
        return;
    if (player.samplePosition < start)
        player.samplePosition = start;
    for (int i = 0; i < length; i++) {
        int index = (int)player.samplePosition;
        if (player.playbackDirection > 0 ? index >= end : index <= start) {
            if (player.repeat == -1) { // Ping-pong
                player.playbackDirection = -player.playbackDirection;
                player.samplePosition = player.playbackDirection > 0 ? start : end - 1;
                index = (int)player.samplePosition;
            } else if (player.repeat == 0 or ++player.loops < player.repeat) {
                player.samplePosition = start;
                index = start;
            } else {
                player.playing = false;
                player.completionPending = true;
                break;
            }
        }
        if (stereo) {
            left[i] += (int16)(pcm[index * 2] * player.leftVolume);
            right[i] += (int16)(pcm[index * 2 + 1] * player.rightVolume);
        } else {
            auto value = pcm[index];
            left[i] += (int16)(value * player.leftVolume);
            right[i] += (int16)(value * player.rightVolume);
        }
        player.samplePosition += player.playbackDirection > 0 ? step : -step;
    }
    player.sampleOffset = (int)player.samplePosition;
}

void SamplePlayer_32::sampleAudio(int16 *left, int16 *right, int length) {
    playSampleAudio(*this, sample.get(), left, right, length);
}

void FilePlayer_32::sampleAudio(int16 *left, int16 *right, int length) {
    playSampleAudio(*this, bufferedSample.get(), left, right, length);
}

void Audio::sampleAudio(int16 *samples, int len) {
    memset(samples, 0, len * 2 * sizeof(int16));
    vector<int32> accumulatorLeft(len), accumulatorRight(len);
    vector<int16> sourceLeft(len), sourceRight(len);
    vector<SoundSource> finished;

    auto processChannel = [&](SoundChannel_32 *channel) {
        if (!channel)
            return;
        float volume = channel->volume;
        float pan = channel->pan;
        float leftGain = volume * min(1.0f, 2.0f * (1.0f - pan));
        float rightGain = volume * min(1.0f, 2.0f * pan);
        for (auto &sourceRef : channel->sources) {
            SoundSource source = sourceRef.get();
            if (!source or !source->playing)
                continue;
            ranges::fill(sourceLeft, 0);
            ranges::fill(sourceRight, 0);
            source->sampleAudio(sourceLeft.data(), sourceRight.data(), len);
            for (int i = 0; i < len; i++) {
                accumulatorLeft[i] += (int32)((float)sourceLeft[i] * leftGain);
                accumulatorRight[i] += (int32)((float)sourceRight[i] * rightGain);
            }
            if (source->completionPending)
                finished.emplace_back(source);
        }
    };

    processChannel(mainChannel);
    for (auto &channel : channels)
        processChannel(channel.get());

    for (int i = 0; i < len; i++) {
        samples[i * 2] = (int16)max(min(accumulatorLeft[i], 32767), -32768);
        samples[i * 2 + 1] = (int16)max(min(accumulatorRight[i], 32767), -32768);
    }

    sampleTime += len;

    for (SoundSource source : finished) {
        source->completionPending = false;
        if (source->completionCallback and cranked.nativeEngine.isLoaded())
            cranked.nativeEngine.invokeEmulatedFunction<void, ArgType::void_t, ArgType::ptr_t, ArgType::uint32_t>(
                    source->completionCallback, source, source->completionCallbackUserdata);
    }
}

void Audio::reset() {
    sampleTime = 0;
    lastError = 0;
    mainChannel.reset();
    for (SoundChannelRef &active : vector(channels.begin(), channels.end()))
        active.reset();
    channels.clear();
}

void Audio::init() {
    mainChannel = cranked.heap.construct<SoundChannel_32>(cranked);
}
