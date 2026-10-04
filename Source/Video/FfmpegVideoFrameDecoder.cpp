#include "FfmpegVideoFrameDecoder.h"
#include "../Export/VideoExporter.h"
#include "../Export/FfmpegProcess.h"

FfmpegVideoFrameDecoder::FfmpegVideoFrameDecoder(juce::File videoFile, int w, int h, double f)
    : file(std::move(videoFile)), width(w), height(h), fps(f > 0.0 ? f : 25.0)
{
    decodeThread = std::make_unique<DecodeThread>(*this);
    decodeThread->startThread(juce::Thread::Priority::normal);
}

FfmpegVideoFrameDecoder::~FfmpegVideoFrameDecoder()
{
    if (decodeThread != nullptr)
    {
        decodeThread->signalThreadShouldExit();
        decodeThread->stopThread(3000);
    }
    killProcess();
}

void FfmpegVideoFrameDecoder::killProcess()
{
    if (process != nullptr)
        FfmpegProcess::killAndReap(*process, "FfmpegVideoFrameDecoder");
    process.reset();
}

void FfmpegVideoFrameDecoder::seekTo(double seconds)
{
    {
        juce::ScopedLock sl(seekLock);
        hasPendingSeek = true;
        pendingSeekSeconds = juce::jmax(0.0, seconds);
    }

    // Cleared here, synchronously, rather than left for the decode thread to
    // get around to -- otherwise there's a window between this call returning
    // and the decode thread noticing hasPendingSeek where a caller polling
    // getFrameNear() can still see stale pre-seek frames sitting in the
    // buffer and mistake one for the seek having resolved.
    juce::ScopedLock sl(bufferLock);
    ringBuffer.clear();
}

std::shared_ptr<const FfmpegVideoFrameDecoder::Frame> FfmpegVideoFrameDecoder::getFrameNear(double targetSeconds)
{
    juce::ScopedLock sl(bufferLock);

    std::shared_ptr<Frame> best;
    while (!ringBuffer.empty() && ringBuffer.front()->timestampSeconds <= targetSeconds)
    {
        best = ringBuffer.front();
        ringBuffer.pop_front();
    }

    return best;
}

void FfmpegVideoFrameDecoder::runDecodeLoop(juce::Thread& thread)
{
    if (!VideoExporter::isFfmpegAvailable())
    {
        fprintf(stderr, "FfmpegVideoFrameDecoder: ffmpeg not found on PATH\n");
        return;
    }

    while (!thread.threadShouldExit())
    {
        bool doSeek;
        double seekTarget;
        {
            juce::ScopedLock sl(seekLock);
            doSeek = hasPendingSeek;
            seekTarget = pendingSeekSeconds;
            hasPendingSeek = false;
        }

        if (doSeek)
        {
            // The ring buffer was already cleared synchronously in seekTo()
            // itself -- see its comment for why that can't wait until here.
            killProcess();

            juce::StringArray args { "ffmpeg", "-hide_banner", "-loglevel", "error", "-nostdin" };
            // -hwaccel auto was tried twice: once before the relaunch-storm fix
            // (reverted after a full system reboot -- repeatedly SIGKILL-ing a
            // process mid-CUDA-decode is a known-hazardous pattern), and once
            // after it (no crash, but measured head-to-head on a 4K 10-bit HEVC
            // clip it was *slower* overall than pure software: the GPU-decoded
            // frame has to come back to system memory before this process's
            // software scale/pix_fmt conversion can touch it, and that round
            // trip plus per-frame submission overhead outweighed the decode-side
            // saving here. Software-only, since it's both faster and has zero
            // GPU-driver risk for this workload.
            if (seekTarget > 0.0)
            {
                args.add("-ss");
                args.add(juce::String(seekTarget, 3));
                // Explicit rather than relying on ffmpeg's own default: forces
                // decode-forward-to-exact-timestamp after the nearest preceding
                // keyframe, so the first frame handed back really is at
                // seekTarget (frame->timestampSeconds below assumes this) rather
                // than whichever keyframe -ss's normal input-seek lands on.
                args.add("-accurate_seek");
            }
            args.add("-i"); args.add(file.getFullPathName());
            args.add("-an"); args.add("-sn"); args.add("-dn");
            // fast_bilinear trades a little scaling quality (invisible at this
            // preview size) for meaningfully less CPU per frame than the
            // default bicubic -- measured ~15% less wall-clock time on a 4K
            // source scaled down to 720p.
            args.add("-sws_flags"); args.add("fast_bilinear");
            args.add("-f"); args.add("rawvideo");
            args.add("-pix_fmt"); args.add("rgb24");
            args.add("-s"); args.add(juce::String(width) + "x" + juce::String(height));
            args.add("-vsync"); args.add("0");
            args.add("-");

            auto p = std::make_unique<juce::ChildProcess>();
            if (p->start(args, juce::ChildProcess::wantStdOut))
            {
                process = std::move(p);
                seekBaseSeconds = seekTarget;
                frameIndexSinceSeek = 0;
                fprintf(stderr, "FfmpegVideoFrameDecoder: started decode at t=%.2fs (%dx%d @%.2ffps)\n",
                        seekTarget, width, height, fps);
            }
            else
            {
                fprintf(stderr, "FfmpegVideoFrameDecoder: failed to start ffmpeg process\n");
                thread.wait(200); // avoid a hot spin if ffmpeg can't even start
            }
            continue;
        }

        if (process == nullptr)
        {
            thread.wait(20);
            continue;
        }

        const int frameBytes = width * height * 3;
        juce::HeapBlock<uint8_t> raw((size_t) frameBytes);
        int filled = 0;
        bool decodeFailed = false;
        bool preempted = false;

        while (filled < frameBytes)
        {
            if (thread.threadShouldExit())
                return;

            {
                juce::ScopedLock sl(seekLock);
                if (hasPendingSeek) { preempted = true; break; }
            }

            const int n = process->readProcessOutput(raw.getData() + filled, frameBytes - filled);
            if (n <= 0) { decodeFailed = true; break; }
            filled += n;
        }

        if (preempted)
            continue; // loop top will pick up the pending seek

        if (decodeFailed)
        {
            // End of stream, or ffmpeg exited/errored -- stop trying to read
            // until a fresh seek arrives (e.g. the transport looped or the
            // user scrubbed backwards).
            fprintf(stderr, "FfmpegVideoFrameDecoder: read failed/EOF after %lld frame(s) since last seek\n",
                    (long long) frameIndexSinceSeek);
            killProcess();
            continue;
        }

        if (frameIndexSinceSeek == 0)
            fprintf(stderr, "FfmpegVideoFrameDecoder: decoded first frame after seek to t=%.2fs\n", seekBaseSeconds);

        auto frame = std::make_shared<Frame>();
        frame->image = juce::Image(juce::Image::ARGB, width, height, false);
        {
            // Writes PixelARGB directly instead of going through
            // BitmapData::setPixelColour(x, y, Colour(...)) -- at 1280x720x30fps
            // that's ~27.6M Colour constructions + format-dispatch calls/sec on
            // this one background thread; a raw typed pointer write avoids all
            // of that per-pixel overhead while producing the identical result
            // (this image was just created as Image::ARGB, so the format is
            // known and pixelStride == sizeof(PixelARGB) always holds here).
            juce::Image::BitmapData bitmap(frame->image, juce::Image::BitmapData::writeOnly);
            for (int y = 0; y < height; ++y)
            {
                const uint8_t* srcRow = raw.getData() + (size_t) y * (size_t) width * 3;
                auto* destRow = bitmap.getLinePointer(y);
                for (int x = 0; x < width; ++x)
                {
                    auto* px = reinterpret_cast<juce::PixelARGB*>(destRow + x * bitmap.pixelStride);
                    px->setARGB(255, srcRow[x * 3 + 0], srcRow[x * 3 + 1], srcRow[x * 3 + 2]);
                }
            }
        }
        frame->timestampSeconds = seekBaseSeconds + (double) frameIndexSinceSeek / fps;
        ++frameIndexSinceSeek;

        // Push into the ring buffer, waiting for room if full so a stalled
        // consumer doesn't cause unbounded decode-ahead; re-checks for a fresh
        // seek each iteration so it never blocks a pending reseek indefinitely.
        for (;;)
        {
            {
                juce::ScopedLock sl(seekLock);
                if (hasPendingSeek)
                    break; // a newer seek arrived -- drop this frame
            }
            {
                juce::ScopedLock sl(bufferLock);
                if ((int) ringBuffer.size() < kBufferFrames)
                {
                    ringBuffer.push_back(frame);
                    break;
                }
            }
            if (thread.threadShouldExit())
                return;
            thread.wait(10);
        }
    }

    killProcess();
}
