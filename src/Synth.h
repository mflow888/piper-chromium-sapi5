#pragma once

#include <cstdint>
#include <string>
#include <vector>

using SynthCancelFn = bool (*)(void* context);

bool SynthPrepare(const std::wstring& voiceId, std::wstring& error);
int SynthSampleRate(const std::wstring& voiceId);
bool SynthSpeak(const std::wstring& voiceId,
                const std::wstring& text,
                float speed,
                float gain,
                std::vector<int16_t>& pcm,
                std::wstring& error,
                bool* canceled = nullptr,
                SynthCancelFn cancel = nullptr,
                void* cancelArg = nullptr);
