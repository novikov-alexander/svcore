/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006-2007 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "CodedAudioFileReader.h"

#include "WavFileReader.h"
#include "base/TempDirectory.h"
#include "base/Exceptions.h"
#include "base/Profiler.h"
#include "base/Serialiser.h"
#include "base/Resampler.h"
#include "base/StorageAdviser.h"

#include <stdint.h>
#include <iostream>
#include <QDir>
#include <QMutexLocker>

using namespace std;

CodedAudioFileReader::CodedAudioFileReader(CacheMode cacheMode,
                                           sv_samplerate_t targetRate,
                                           bool normalised) :
    m_cacheMode(cacheMode),
    m_initialised(false),
    m_serialiser(0),
    m_fileRate(0),
    m_cacheFileWritePtr(0),
    m_cacheFileReader(0),
    m_cacheWriteBuffer(0),
    m_cacheWriteBufferIndex(0),
    m_cacheWriteBufferSize(16384),
    m_resampler(0),
    m_resampleBuffer(0),
    m_fileFrameCount(0),
    m_normalised(normalised),
    m_max(0.f),
    m_gain(1.f)
{
    SVDEBUG << "CodedAudioFileReader:: cache mode: " << cacheMode
            << " (" << (cacheMode == CacheInTemporaryFile
                        ? "CacheInTemporaryFile" : "CacheInMemory") << ")"
            << ", rate: " << targetRate
            << (targetRate == 0 ? " (use source rate)" : "")
            << ", normalised: " << normalised << endl;

    m_frameCount = 0;
    m_sampleRate = targetRate;
}

CodedAudioFileReader::~CodedAudioFileReader()
{
    QMutexLocker locker(&m_cacheMutex);

    if (m_serialiser) endSerialised();
    
    if (m_cacheFileWritePtr) sf_close(m_cacheFileWritePtr);

    SVDEBUG << "CodedAudioFileReader::~CodedAudioFileReader: deleting cache file reader" << endl;

    delete m_cacheFileReader;
    delete[] m_cacheWriteBuffer;
    
    if (m_cacheFileName != "") {
        SVDEBUG << "CodedAudioFileReader::~CodedAudioFileReader: deleting cache file " << m_cacheFileName << endl;
        if (!QFile(m_cacheFileName).remove()) {
            SVDEBUG << "WARNING: CodedAudioFileReader::~CodedAudioFileReader: Failed to delete cache file \"" << m_cacheFileName << "\"" << endl;
        }
    }

    delete m_resampler;
    delete[] m_resampleBuffer;

    if (!m_data.empty()) {
        StorageAdviser::notifyDoneAllocation
            (StorageAdviser::MemoryAllocation,
             (m_data.size() * sizeof(float)) / 1024);
    }
}

void
CodedAudioFileReader::startSerialised(QString id)
{
    SVDEBUG << "CodedAudioFileReader(" << this << ")::startSerialised: id = " << id << endl;

    delete m_serialiser;
    m_serialiser = new Serialiser(id);
}

void
CodedAudioFileReader::endSerialised()
{
    SVDEBUG << "CodedAudioFileReader(" << this << ")::endSerialised: id = " << (m_serialiser ? m_serialiser->getId() : "(none)") << endl;

    delete m_serialiser;
    m_serialiser = 0;
}

void
CodedAudioFileReader::initialiseDecodeCache()
{
    QMutexLocker locker(&m_cacheMutex);

    SVDEBUG << "CodedAudioFileReader::initialiseDecodeCache: file rate = " << m_fileRate << endl;

    if (m_fileRate == 0) {
        SVDEBUG << "CodedAudioFileReader::initialiseDecodeCache: ERROR: File sample rate unknown (bug in subclass implementation?)" << endl;
        throw FileOperationFailed("(coded file)", "File sample rate unknown (bug in subclass implementation?)");
    }
    if (m_sampleRate == 0) {
        m_sampleRate = m_fileRate;
        SVDEBUG << "CodedAudioFileReader::initialiseDecodeCache: rate (from file) = " << m_fileRate << endl;
    }
    if (m_fileRate != m_sampleRate) {
        SVDEBUG << "CodedAudioFileReader: resampling " << m_fileRate << " -> " <<  m_sampleRate << endl;
        m_resampler = new Resampler(Resampler::FastestTolerable,
                                    m_channelCount,
                                    m_cacheWriteBufferSize);
        double ratio = m_sampleRate / m_fileRate;
        m_resampleBuffer = new float
            [lrint(ceil(double(m_cacheWriteBufferSize) * m_channelCount * ratio + 1))];
    }

    m_cacheWriteBuffer = new float[m_cacheWriteBufferSize * m_channelCount];
    m_cacheWriteBufferIndex = 0;

    if (m_cacheMode == CacheInTemporaryFile) {

        try {
            QDir dir(TempDirectory::getInstance()->getPath());
            m_cacheFileName = dir.filePath(QString("decoded_%1.wav")
                                           .arg((intptr_t)this));

            SF_INFO fileInfo;
            int fileRate = int(round(m_sampleRate));
            if (m_sampleRate != sv_samplerate_t(fileRate)) {
                SVDEBUG << "CodedAudioFileReader: WARNING: Non-integer sample rate "
                     << m_sampleRate << " presented for writing, rounding to " << fileRate
                     << endl;
            }
            fileInfo.samplerate = fileRate;
            fileInfo.channels = m_channelCount;

            // Previously we were writing SF_FORMAT_PCM_16 and in a
            // comment I wrote: "No point in writing 24-bit or float;
            // generally this class is used for decoding files that
            // have come from a 16 bit source or that decode to only
            // 16 bits anyway." That was naive -- we want to preserve
            // the original values to the same float precision that we
            // use internally. Saving PCM_16 obviously doesn't
            // preserve values for sources at bit depths greater than
            // 16, but it also doesn't always do so for sources at bit
            // depths less than 16.
            //
            // (This came to light with a bug in libsndfile 1.0.26,
            // which always reports every file as non-seekable, so
            // that coded readers were being used even for WAV
            // files. This changed the values that came from PCM_8 WAV
            // sources, breaking Sonic Annotator's output comparison
            // tests.)
            //
            // So: now we write floats.
            fileInfo.format = SF_FORMAT_WAV | SF_FORMAT_FLOAT;
    
            m_cacheFileWritePtr = sf_open(m_cacheFileName.toLocal8Bit(),
                                          SFM_WRITE, &fileInfo);

            if (m_cacheFileWritePtr) {

                // Ideally we would do this now only if we were in a
                // threaded mode -- creating the reader later if we're
                // not threaded -- but we don't have access to that
                // information here

                m_cacheFileReader = new WavFileReader(m_cacheFileName);

                if (!m_cacheFileReader->isOK()) {
                    SVDEBUG << "ERROR: CodedAudioFileReader::initialiseDecodeCache: Failed to construct WAV file reader for temporary file: " << m_cacheFileReader->getError() << endl;
                    delete m_cacheFileReader;
                    m_cacheFileReader = 0;
                    m_cacheMode = CacheInMemory;
                    sf_close(m_cacheFileWritePtr);
                }

            } else {
                SVDEBUG << "CodedAudioFileReader::initialiseDecodeCache: failed to open cache file \"" << m_cacheFileName << "\" (" << m_channelCount << " channels, sample rate " << m_sampleRate << " for writing, falling back to in-memory cache" << endl;
                m_cacheMode = CacheInMemory;
            }

        } catch (DirectoryCreationFailed f) {
            SVDEBUG << "CodedAudioFileReader::initialiseDecodeCache: failed to create temporary directory! Falling back to in-memory cache" << endl;
            m_cacheMode = CacheInMemory;
        }
    }

    if (m_cacheMode == CacheInMemory) {
        m_data.clear();
    }

    m_initialised = true;
}

void
CodedAudioFileReader::addSamplesToDecodeCache(float **samples, sv_frame_t nframes)
{
    QMutexLocker locker(&m_cacheMutex);

    if (!m_initialised) return;

    for (sv_frame_t i = 0; i < nframes; ++i) {
        
        for (int c = 0; c < m_channelCount; ++c) {

            float sample = samples[c][i];
        
            m_cacheWriteBuffer[m_cacheWriteBufferIndex++] = sample;

            if (m_cacheWriteBufferIndex ==
                m_cacheWriteBufferSize * m_channelCount) {

                pushBuffer(m_cacheWriteBuffer, m_cacheWriteBufferSize, false);
                m_cacheWriteBufferIndex = 0;
            }

            if (m_cacheWriteBufferIndex % 10240 == 0 &&
                m_cacheFileReader) {
                m_cacheFileReader->updateFrameCount();
            }
        }
    }
}

void
CodedAudioFileReader::addSamplesToDecodeCache(float *samples, sv_frame_t nframes)
{
    QMutexLocker locker(&m_cacheMutex);

    if (!m_initialised) return;

    for (sv_frame_t i = 0; i < nframes; ++i) {
        
        for (int c = 0; c < m_channelCount; ++c) {

            float sample = samples[i * m_channelCount + c];
        
            m_cacheWriteBuffer[m_cacheWriteBufferIndex++] = sample;

            if (m_cacheWriteBufferIndex ==
                m_cacheWriteBufferSize * m_channelCount) {

                pushBuffer(m_cacheWriteBuffer, m_cacheWriteBufferSize, false);
                m_cacheWriteBufferIndex = 0;
            }

            if (m_cacheWriteBufferIndex % 10240 == 0 &&
                m_cacheFileReader) {
                m_cacheFileReader->updateFrameCount();
            }
        }
    }
}

void
CodedAudioFileReader::addSamplesToDecodeCache(const vector<float> &samples)
{
    QMutexLocker locker(&m_cacheMutex);

    if (!m_initialised) return;

    for (float sample: samples) {
        
        m_cacheWriteBuffer[m_cacheWriteBufferIndex++] = sample;

        if (m_cacheWriteBufferIndex ==
            m_cacheWriteBufferSize * m_channelCount) {

            pushBuffer(m_cacheWriteBuffer, m_cacheWriteBufferSize, false);
            m_cacheWriteBufferIndex = 0;
        }

        if (m_cacheWriteBufferIndex % 10240 == 0 &&
            m_cacheFileReader) {
            m_cacheFileReader->updateFrameCount();
        }
    }
}

void
CodedAudioFileReader::finishDecodeCache()
{
    QMutexLocker locker(&m_cacheMutex);

    Profiler profiler("CodedAudioFileReader::finishDecodeCache", true);

    if (!m_initialised) {
        SVDEBUG << "WARNING: CodedAudioFileReader::finishDecodeCache: Cache was never initialised!" << endl;
        return;
    }

    pushBuffer(m_cacheWriteBuffer,
               m_cacheWriteBufferIndex / m_channelCount,
               true);

    delete[] m_cacheWriteBuffer;
    m_cacheWriteBuffer = 0;

    delete[] m_resampleBuffer;
    m_resampleBuffer = 0;

    delete m_resampler;
    m_resampler = 0;

    if (m_cacheMode == CacheInTemporaryFile) {

        sf_close(m_cacheFileWritePtr);
        m_cacheFileWritePtr = 0;
        if (m_cacheFileReader) m_cacheFileReader->updateFrameCount();

    } else {
        // I know, I know, we already allocated it...
        StorageAdviser::notifyPlannedAllocation
            (StorageAdviser::MemoryAllocation,
             (m_data.size() * sizeof(float)) / 1024);
    }
}

void
CodedAudioFileReader::pushBuffer(float *buffer, sv_frame_t sz, bool final)
{
    m_fileFrameCount += sz;

    double ratio = 1.0;
    if (m_resampler && m_fileRate != 0) {
        ratio = m_sampleRate / m_fileRate;
    }
        
    if (ratio != 1.0) {
        pushBufferResampling(buffer, sz, ratio, final);
    } else {
        pushBufferNonResampling(buffer, sz);
    }
}

void
CodedAudioFileReader::pushBufferNonResampling(float *buffer, sv_frame_t sz)
{
    float clip = 1.0;
    sv_frame_t count = sz * m_channelCount;

    if (m_normalised) {
        for (sv_frame_t i = 0; i < count; ++i) {
            float v = fabsf(buffer[i]);
            if (v > m_max) {
                m_max = v;
                m_gain = 1.f / m_max;
            }
        }
    } else {
        for (sv_frame_t i = 0; i < count; ++i) {
            if (buffer[i] >  clip) buffer[i] =  clip;
        }
        for (sv_frame_t i = 0; i < count; ++i) {
            if (buffer[i] < -clip) buffer[i] = -clip;
        }
    }

    m_frameCount += sz;

    switch (m_cacheMode) {

    case CacheInTemporaryFile:
        if (sf_writef_float(m_cacheFileWritePtr, buffer, sz) < sz) {
            sf_close(m_cacheFileWritePtr);
            m_cacheFileWritePtr = 0;
            throw InsufficientDiscSpace(TempDirectory::getInstance()->getPath());
        }
        break;

    case CacheInMemory:
        m_dataLock.lock();
        m_data.insert(m_data.end(), buffer, buffer + count);
        m_dataLock.unlock();
        break;
    }
}

void
CodedAudioFileReader::pushBufferResampling(float *buffer, sv_frame_t sz,
                                           double ratio, bool final)
{
    SVDEBUG << "pushBufferResampling: ratio = " << ratio << ", sz = " << sz << ", final = " << final << endl;

    if (sz > 0) {

        sv_frame_t out = m_resampler->resampleInterleaved
            (buffer,
             m_resampleBuffer,
             sz,
             ratio,
             false);

        pushBufferNonResampling(m_resampleBuffer, out);
    }

    if (final) {

        sv_frame_t padFrames = 1;
        if (double(m_frameCount) / ratio < double(m_fileFrameCount)) {
            padFrames = m_fileFrameCount - sv_frame_t(double(m_frameCount) / ratio) + 1;
        }

        sv_frame_t padSamples = padFrames * m_channelCount;

        SVDEBUG << "frameCount = " << m_frameCount << ", equivFileFrames = " << double(m_frameCount) / ratio << ", m_fileFrameCount = " << m_fileFrameCount << ", padFrames= " << padFrames << ", padSamples = " << padSamples << endl;

        float *padding = new float[padSamples];
        for (sv_frame_t i = 0; i < padSamples; ++i) padding[i] = 0.f;

        sv_frame_t out = m_resampler->resampleInterleaved
            (padding,
             m_resampleBuffer,
             padFrames,
             ratio,
             true);

        if (m_frameCount + out > sv_frame_t(double(m_fileFrameCount) * ratio)) {
            out = sv_frame_t(double(m_fileFrameCount) * ratio) - m_frameCount;
        }

        pushBufferNonResampling(m_resampleBuffer, out);
        delete[] padding;
    }
}

vector<float>
CodedAudioFileReader::getInterleavedFrames(sv_frame_t start, sv_frame_t count) const
{
    // Lock is only required in CacheInMemory mode (the cache file
    // reader is expected to be thread safe and manage its own
    // locking)

    if (!m_initialised) {
        SVDEBUG << "CodedAudioFileReader::getInterleavedFrames: not initialised" << endl;
        return {};
    }

    vector<float> frames;
    
    switch (m_cacheMode) {

    case CacheInTemporaryFile:
        if (m_cacheFileReader) {
            frames = m_cacheFileReader->getInterleavedFrames(start, count);
        }
        break;

    case CacheInMemory:
    {
        if (!isOK()) return {};
        if (count == 0) return {};

        sv_frame_t ix0 = start * m_channelCount;
        sv_frame_t ix1 = ix0 + (count * m_channelCount);

        // This lock used to be a QReadWriteLock, but it appears that
        // its lock mechanism is significantly slower than QMutex so
        // it's not a good idea in cases like this where we don't
        // really have threads taking a long time to read concurrently
        m_dataLock.lock();
        sv_frame_t n = sv_frame_t(m_data.size());
        if (ix1 > n) ix1 = n;
        frames = vector<float>(m_data.begin() + ix0, m_data.begin() + ix1);
        m_dataLock.unlock();
    }
    }

    if (m_normalised) {
        for (auto &f: frames) f *= m_gain;
    }

    return frames;
}

