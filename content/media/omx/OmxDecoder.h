#include <OMX.h>
#include <stagefright/MediaSource.h>
#include <stagefright/DataSource.h>

#include <utils/RefBase.h>

#include "GonkNativeWindow.h"
#include "GonkIOSurfaceImage.h"
#include "MPAPI.h"
#include "MediaResource.h"
#include "nsBuiltinDecoder.h"

namespace android {
class OmxDecoder;
};

namespace mozilla {
namespace layers {

class VideoGraphicBuffer : public GraphicBufferLocked {
  // XXX change this to an actual smart pointer at some point
  android::MediaBuffer *mMediaBuffer;
  android::wp<android::OmxDecoder> mOmxDecoder;
  public:
    VideoGraphicBuffer(const android::wp<android::OmxDecoder> aOmxDecoder,
                       android::MediaBuffer *aBuffer,
                       SurfaceDescriptor *aDescriptor);
    ~VideoGraphicBuffer();
    void Unlock();
};

}
}

namespace android {

// MediaStreamSource is a DataSource that reads from a MPAPI media stream.
class MediaStreamSource : public DataSource {
  typedef mozilla::MediaResource MediaResource;

  MediaResource *mResource;
  nsBuiltinDecoder *mDecoder;
public:
  MediaStreamSource(MediaResource *aResource,
                    nsBuiltinDecoder *aDecoder);

  virtual status_t initCheck() const;
  virtual ssize_t readAt(off64_t offset, void *data, size_t size);
  virtual ssize_t readAt(off_t offset, void *data, size_t size) {
    return readAt(static_cast<off64_t>(offset), data, size);
  }
  virtual status_t getSize(off_t *size) {
    off64_t size64;
    status_t status = getSize(&size64);
    *size = size64;
    return status;
  }
  virtual status_t getSize(off64_t *size);
  virtual uint32_t flags() {
    return kWantsPrefetching;
  }

  virtual ~MediaStreamSource();

private:
  MediaStreamSource(const MediaStreamSource &);
  MediaStreamSource &operator=(const MediaStreamSource &);
};

class OmxDecoder : public RefBase {
  typedef MPAPI::AudioFrame AudioFrame;
  typedef MPAPI::VideoFrame VideoFrame;
  typedef mozilla::MediaResource MediaResource;

  enum {
    kPreferSoftwareCodecs = 1,
    kSoftwareCodecsOnly = 8,
    kHardwareCodecsOnly = 16,
  };

  nsBuiltinDecoder *mDecoder;
  MediaResource *mResource;
  sp<GonkNativeWindow> mNativeWindow;
  sp<MediaSource> mVideoTrack;
  sp<MediaSource> mVideoSource;
  sp<MediaSource> mAudioTrack;
  sp<MediaSource> mAudioSource;
  int32_t mVideoWidth;
  int32_t mVideoHeight;
  int32_t mVideoColorFormat;
  int32_t mVideoStride;
  int32_t mVideoSliceHeight;
  int32_t mVideoRotation;
  int32_t mAudioChannels;
  int32_t mAudioSampleRate;
  int64_t mDurationUs;
  VideoFrame mVideoFrame;
  AudioFrame mAudioFrame;

  // Lifetime of these should be handled by OMXCodec, as long as we release
  //   them after use: see ReleaseVideoBuffer(), ReleaseAudioBuffer()
  MediaBuffer *mVideoBuffer;
  MediaBuffer *mAudioBuffer;

  // Hold video's MediaBuffers that are released during video seeking.
  // The holded MediaBuffers are released soon after seek completion.
  // OMXCodec does not accept MediaBuffer during seeking. If MediaBuffer is
  //  returned to OMXCodec during seeking, OMXCodec calls assert.
  Vector<MediaBuffer *> mPendingVideoBuffers;
  // Show if OMXCodec is seeking.
  bool mIsVideoSeeking;
  // The lock protects video MediaBuffer release()'s pending operations called
  //  from multiple threads. The pending operations happen only during video
  //  seeking. Holding mSeekLock long time could affect to video rendering.
  // Holding time should be minimum.
  Mutex mSeekLock;

  // 'true' if a read from the audio stream was done while reading the metadata
  bool mAudioMetadataRead;

  void ReleaseVideoBuffer();
  void ReleaseAudioBuffer();
  // Call with mSeekLock held.
  void ReleaseAllPendingVideoBuffersLocked();

  void PlanarYUV420Frame(VideoFrame *aFrame, int64_t aTimeUs, void *aData, size_t aSize, bool aKeyFrame);
  void CbYCrYFrame(VideoFrame *aFrame, int64_t aTimeUs, void *aData, size_t aSize, bool aKeyFrame);
  void SemiPlanarYUV420Frame(VideoFrame *aFrame, int64_t aTimeUs, void *aData, size_t aSize, bool aKeyFrame);
  void SemiPlanarYVU420Frame(VideoFrame *aFrame, int64_t aTimeUs, void *aData, size_t aSize, bool aKeyFrame);
  bool ToVideoFrame(VideoFrame *aFrame, int64_t aTimeUs, void *aData, size_t aSize, bool aKeyFrame);
  bool ToAudioFrame(AudioFrame *aFrame, int64_t aTimeUs, void *aData, size_t aDataOffset, size_t aSize,
                    int32_t aAudioChannels, int32_t aAudioSampleRate);
public:
  OmxDecoder(MediaResource *aResource, nsBuiltinDecoder *aDecoder);
  ~OmxDecoder();

  bool Init();
  bool SetVideoFormat();
  bool SetAudioFormat();

  void GetDuration(int64_t *durationUs) {
    *durationUs = mDurationUs;
  }

  void GetVideoParameters(int32_t *width, int32_t *height) {
    *width = mVideoWidth;
    *height = mVideoHeight;
  }

  void GetAudioParameters(int32_t *numChannels, int32_t *sampleRate) {
    *numChannels = mAudioChannels;
    *sampleRate = mAudioSampleRate;
  }

  bool HasVideo() {
    return mVideoSource != nullptr;
  }

  bool HasAudio() {
    return mAudioSource != nullptr;
  }

  bool ReadVideo(VideoFrame *aFrame, int64_t aSeekTimeUs, 
                 bool aKeyframeSkip = false,
                 bool aDoSeek = false);
  bool ReadAudio(AudioFrame *aFrame, int64_t aSeekTimeUs);

  MediaResource *GetResource() {
    return mResource;
  }

  bool ReleaseVideoBuffer(MediaBuffer *aBuffer);
};

}
