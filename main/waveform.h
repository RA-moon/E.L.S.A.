#pragma once

float getAsymmetricIntensity(float frameIndex, float center, float widthBehind, float widthAhead);
void waveformInitFalloff();
void waveformSetFalloff(float noseP1, float noseP2, float tailP1, float tailP2);
