#pragma once

#include "bgfx_capi.h"

#include <ogg/ogg.h>
#include <theora/theoradec.h>

#include <string>
#include <vector>

class IReader;

// Decodes OGG/Theora video files (OGM container) and uploads frames to a bgfx texture.
class BgfxVideoDecoder
{
public:
    BgfxVideoDecoder();
    ~BgfxVideoDecoder();

    // Open an OGM/Theora file. Returns true on success.
    bool Open(const char* path);

    // Close and release resources.
    void Close();

    // Returns true if a texture with decoded video is available.
    bool HasTexture() const;

    // Decode one more frame (if needed) and upload to bgfx texture.
    // Returns true if a new frame was decoded.
    bool DecodeFrame();

    // Get the bgfx texture handle for the current frame.
    bgfx_texture_handle_t GetTexture() const;

    // Video dimensions.
    unsigned int GetWidth() const { return m_width; }
    unsigned int GetHeight() const { return m_height; }

    // Duration in seconds (approximate, from granule position).
    double GetDuration() const { return m_duration; }

    // Current playback position in seconds.
    double GetTime() const;

    // Seek to time (approximate).
    void Seek(double time);

    // Playback control.
    void Play();
    void Stop();
    bool IsPlaying() const { return m_playing; }
    // True when the data ended (DecodeFrame returned false) — call Restart().
    bool IsEnded() const { return m_ended; }
    // Reopen the file and start from the first frame (looping).
    void Restart();

    // Sync to time (in milliseconds) — decode frames up to this time.
    void Sync(unsigned int timeMs);

private:
    bool UploadFrame(th_ycbcr_buffer yuv);
    void ConvertYUV420ToRGB(const th_ycbcr_buffer yuv, std::vector<unsigned char>& rgb);
    // Reset the decoding state (keeps the bgfx texture and buffer).
    void ResetStream();

    // File (opened via the X-Ray virtual filesystem)
    IReader* m_reader;
    char m_path[260];

    // OGG
    ogg_sync_state m_oggSync;
    ogg_stream_state m_oggStream;
    ogg_page m_oggPage;
    ogg_packet m_oggPacket;
    int m_serial;

    // Theora
    th_info m_thInfo;
    th_comment m_thComment;
    th_dec_ctx* m_thDecoder;
    th_setup_info* m_thSetup;

    // State
    bool m_opened;
    bool m_playing;
    bool m_headerDone;
    bool m_firstPacket;
    bool m_ended;

    // Video info
    unsigned int m_width;
    unsigned int m_height;
    double m_duration;
    ogg_int64_t m_granuleShift;
    ogg_int64_t m_lastGranule;

    // Number of decoded frames (for time-based sync)
    ogg_int64_t m_frameCount;

    // bgfx texture
    bgfx_texture_handle_t m_texture;
    std::vector<unsigned char> m_rgbBuffer;

    // Timing
    double m_playStart;
};
