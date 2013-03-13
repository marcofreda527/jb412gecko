/* Copyright 2012 Mozilla Foundation and Mozilla contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <android/log.h>

#include "mozilla/Hal.h"
#include "AudioManager.h"
#include "gonk/AudioSystem.h"
#include "nsIObserverService.h"
#include "mozilla/Services.h"
#include "AudioChannelService.h"

using namespace mozilla::dom::gonk;
using namespace android;
using namespace mozilla::hal;
using namespace mozilla;

#define LOG(args...)  __android_log_print(ANDROID_LOG_INFO, "AudioManager" , ## args)

#define HEADPHONES_STATUS_CHANGED "headphones-status-changed"
#define HEADPHONES_STATUS_ON      NS_LITERAL_STRING("on").get()
#define HEADPHONES_STATUS_OFF     NS_LITERAL_STRING("off").get()
#define HEADPHONES_STATUS_UNKNOWN NS_LITERAL_STRING("unknown").get()
#define BLUETOOTH_SCO_STATUS_CHANGED "bluetooth-sco-status-changed"

// Refer AudioService.java from Android
static int sMaxStreamVolumeTbl[AUDIO_STREAM_CNT] = {
  5,   // voice call
  15,  // system
  15,  // ring
  15,  // music
  15,  // alarm
  15,  // notification
  15,  // BT SCO
  15,  // enforced audible
  15,  // DTMF
  15,  // TTS
  15,  // FM
};

// A bitwise variable for recording what kind of headset is attached.
static int sHeadsetState;
static int kBtSampleRate = 8000;

static bool
IsDeviceOn(audio_devices_t device)
{
  return false;
}

NS_IMPL_ISUPPORTS2(AudioManager, nsIAudioManager, nsIObserver)

static AudioSystem::audio_devices
GetRoutingMode(int aType) {
  if (aType == nsIAudioManager::FORCE_SPEAKER) {
    return AudioSystem::DEVICE_OUT_SPEAKER;
  } else if (aType == nsIAudioManager::FORCE_HEADPHONES) {
    return AudioSystem::DEVICE_OUT_WIRED_HEADSET;
  } else if (aType == nsIAudioManager::FORCE_BT_SCO) {
    return AudioSystem::DEVICE_OUT_BLUETOOTH_SCO;
  } else if (aType == nsIAudioManager::FORCE_BT_A2DP) {
    return AudioSystem::DEVICE_OUT_BLUETOOTH_A2DP;
  } else {
    return AudioSystem::DEVICE_IN_DEFAULT;
  }
}

static void
InternalSetAudioRoutesICS(SwitchState aState)
{
}

static void
InternalSetAudioRoutesGB(SwitchState aState)
{
}

static void
InternalSetAudioRoutes(SwitchState aState)
{
}

nsresult
AudioManager::Observe(nsISupports* aSubject,
                      const char* aTopic,
                      const PRUnichar* aData)
{
    return NS_OK;
}

static void
NotifyHeadphonesStatus(SwitchState aState)
{
  nsCOMPtr<nsIObserverService> obs = mozilla::services::GetObserverService();
  if (obs) {
    if (aState == SWITCH_STATE_ON) {
      obs->NotifyObservers(nullptr, HEADPHONES_STATUS_CHANGED, HEADPHONES_STATUS_ON);
    } else if (aState == SWITCH_STATE_OFF) {
      obs->NotifyObservers(nullptr, HEADPHONES_STATUS_CHANGED, HEADPHONES_STATUS_OFF);
    } else {
      obs->NotifyObservers(nullptr, HEADPHONES_STATUS_CHANGED, HEADPHONES_STATUS_UNKNOWN);
    }
  }
}

class HeadphoneSwitchObserver : public SwitchObserver
{
public:
  void Notify(const SwitchEvent& aEvent) {
    InternalSetAudioRoutes(aEvent.status());
    NotifyHeadphonesStatus(aEvent.status());
  }
};

AudioManager::AudioManager() : mPhoneState(PHONE_STATE_CURRENT),
                 mObserver(new HeadphoneSwitchObserver()),
                 mFMChannelIsMuted(0)
{
  RegisterSwitchObserver(SWITCH_HEADPHONES, mObserver);

  InternalSetAudioRoutes(GetCurrentSwitchState(SWITCH_HEADPHONES));
  NotifyHeadphonesStatus(GetCurrentSwitchState(SWITCH_HEADPHONES));

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_FAILED(obs->AddObserver(this, BLUETOOTH_SCO_STATUS_CHANGED, false))) {
    NS_WARNING("Failed to add bluetooth-sco-status-changed oberver!");
  }

  for (int loop = 0; loop < AUDIO_STREAM_CNT; loop++) {
    AudioSystem::initStreamVolume(static_cast<audio_stream_type_t>(loop), 0,
                                  sMaxStreamVolumeTbl[loop]);
  }
  // Force publicnotification to output at maximal volume
  AudioSystem::setStreamVolumeIndex(static_cast<audio_stream_type_t>(AUDIO_STREAM_ENFORCED_AUDIBLE),
                                    sMaxStreamVolumeTbl[AUDIO_STREAM_ENFORCED_AUDIBLE], AUDIO_DEVICE_OUT_SPEAKER);
}

AudioManager::~AudioManager() {
  UnregisterSwitchObserver(SWITCH_HEADPHONES, mObserver);

  nsCOMPtr<nsIObserverService> obs = services::GetObserverService();
  if (NS_FAILED(obs->RemoveObserver(this, BLUETOOTH_SCO_STATUS_CHANGED))) {
    NS_WARNING("Failed to add bluetooth-sco-status-changed oberver!");
  }
}

NS_IMETHODIMP
AudioManager::GetMicrophoneMuted(bool* aMicrophoneMuted)
{
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetMicrophoneMuted(bool aMicrophoneMuted)
{
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetMasterVolume(float* aMasterVolume)
{
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetMasterVolume(float aMasterVolume)
{

  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetMasterMuted(bool* aMasterMuted)
{
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetMasterMuted(bool aMasterMuted)
{
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetPhoneState(int32_t* aState)
{
  *aState = mPhoneState;
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetPhoneState(int32_t aState)
{
  if (mPhoneState == aState) {
    return NS_OK;
  }


  mPhoneState = aState;

  if (mPhoneAudioAgent) {
    mPhoneAudioAgent->StopPlaying();
    mPhoneAudioAgent = nullptr;
  }

  if (aState == PHONE_STATE_IN_CALL || aState == PHONE_STATE_RINGTONE) {
    mPhoneAudioAgent = do_CreateInstance("@mozilla.org/audiochannelagent;1");
    MOZ_ASSERT(mPhoneAudioAgent);
    if (aState == PHONE_STATE_IN_CALL) {
      // Telephony doesn't be paused by any other channels.
      mPhoneAudioAgent->Init(AUDIO_CHANNEL_TELEPHONY, nullptr);
    } else {
      mPhoneAudioAgent->Init(AUDIO_CHANNEL_RINGER, nullptr);
    }

    // Telephony can always play.
    bool canPlay;
    mPhoneAudioAgent->StartPlaying(&canPlay);
  }

  return NS_OK;
}

//
// Kids, don't try this at home.  We want this to link and work on
// both GB and ICS.  Problem is, the symbol exported by audioflinger
// is different on the two gonks.
//
// So what we do here is weakly link to both of them, and then call
// whichever symbol resolves at dynamic link time (if any).
//
NS_IMETHODIMP
AudioManager::SetForceForUse(int32_t aUsage, int32_t aForce)
{
  status_t status = 0;


  return status ? NS_ERROR_FAILURE : NS_OK;
}

NS_IMETHODIMP
AudioManager::GetForceForUse(int32_t aUsage, int32_t* aForce) {
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetFmRadioAudioEnabled(bool *aFmRadioAudioEnabled)
{
  *aFmRadioAudioEnabled = IsDeviceOn(AUDIO_DEVICE_OUT_FM);
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetFmRadioAudioEnabled(bool aFmRadioAudioEnabled)
{
    return NS_OK;
}

NS_IMETHODIMP
AudioManager::SetStreamVolumeIndex(int32_t aStream, int32_t aIndex) {
  AudioSystem::setStreamVolumeIndex(static_cast<audio_stream_type_t>(aStream),
                                    aIndex, AUDIO_DEVICE_OUT_SPEAKER);
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetStreamVolumeIndex(int32_t aStream, int32_t* aIndex) {
  return NS_OK;
}

NS_IMETHODIMP
AudioManager::GetMaxStreamVolumeIndex(int32_t aStream, int32_t* aMaxIndex) {
  *aMaxIndex = sMaxStreamVolumeTbl[aStream];
  return NS_OK;
}

