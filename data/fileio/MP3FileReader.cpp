
/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifdef HAVE_MAD

#include "MP3FileReader.h"
#include "system/System.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#include <iostream>

#ifdef HAVE_ID3TAG
#include <id3tag.h>
#endif
#define DEBUG_ID3TAG 1

#include <QApplication>
#include <QFileInfo>
#include <QProgressDialog>

MP3FileReader::MP3FileReader(FileSource source, DecodeMode decodeMode, 
                             CacheMode mode, size_t targetRate) :
    CodedAudioFileReader(mode, targetRate),
    m_source(source),
    m_path(source.getLocalFilename()),
    m_decodeThread(0)
{
    m_channelCount = 0;
    m_fileRate = 0;
    m_fileSize = 0;
    m_bitrateNum = 0;
    m_bitrateDenom = 0;
    m_cancelled = false;
    m_completion = 0;
    m_done = false;
    m_progress = 0;

    struct stat stat;
    if (::stat(m_path.toLocal8Bit().data(), &stat) == -1 || stat.st_size == 0) {
	m_error = QString("File %1 does not exist.").arg(m_path);
	return;
    }

    m_fileSize = stat.st_size;

    int fd = -1;
    if ((fd = ::open(m_path.toLocal8Bit().data(), O_RDONLY
#ifdef _WIN32
                     | O_BINARY
#endif
                     , 0)) < 0) {
	m_error = QString("Failed to open file %1 for reading.").arg(m_path);
	return;
    }	

    m_filebuffer = 0;
    m_samplebuffer = 0;
    m_samplebuffersize = 0;

    try {
        m_filebuffer = new unsigned char[m_fileSize];
    } catch (...) {
        m_error = QString("Out of memory");
        ::close(fd);
	return;
    }
    
    ssize_t sz = 0;
    size_t offset = 0;
    while (offset < m_fileSize) {
        sz = ::read(fd, m_filebuffer + offset, m_fileSize - offset);
        if (sz < 0) {
            m_error = QString("Read error for file %1 (after %2 bytes)")
                .arg(m_path).arg(offset);
            delete[] m_filebuffer;
            ::close(fd);
            return;
        } else if (sz == 0) {
            std::cerr << QString("MP3FileReader::MP3FileReader: Warning: reached EOF after only %1 of %2 bytes")
                .arg(offset).arg(m_fileSize).toStdString() << std::endl;
            m_fileSize = offset;
            break;
        }
        offset += sz;
    }

    ::close(fd);

    loadTags();

    if (decodeMode == DecodeAtOnce) {

        if (dynamic_cast<QApplication *>(QCoreApplication::instance())) {
            m_progress = new QProgressDialog
                (QObject::tr("Decoding %1...").arg(QFileInfo(m_path).fileName()),
                 QObject::tr("Stop"), 0, 100);
            m_progress->hide();
        }

        if (!decode(m_filebuffer, m_fileSize)) {
            m_error = QString("Failed to decode file %1.").arg(m_path);
        }
        
        delete[] m_filebuffer;
        m_filebuffer = 0;

        if (isDecodeCacheInitialised()) finishDecodeCache();

	delete m_progress;
	m_progress = 0;

    } else {

        m_decodeThread = new DecodeThread(this);
        m_decodeThread->start();

        while (m_channelCount == 0 && !m_done) {
            usleep(10);
        }
    }
}

MP3FileReader::~MP3FileReader()
{
    if (m_decodeThread) {
        m_cancelled = true;
        m_decodeThread->wait();
        delete m_decodeThread;
    }
}

void
MP3FileReader::loadTags()
{
    m_title = "";

#ifdef HAVE_ID3TAG

    id3_file *file = id3_file_open(m_path.toLocal8Bit().data(),
                                   ID3_FILE_MODE_READONLY);
    if (!file) return;

    // We can do this a lot more elegantly, but we'll leave that for
    // when we implement support for more than just the one tag!
    
    id3_tag *tag = id3_file_tag(file);
    if (!tag) {
#ifdef DEBUG_ID3TAG
        std::cerr << "MP3FileReader::loadTags: No ID3 tag found" << std::endl;
#endif
        id3_file_close(file);
        return;
    }

    m_title = loadTag(tag, "TIT2"); // work title
    if (m_title == "") m_title = loadTag(tag, "TIT1");

    m_maker = loadTag(tag, "TPE1"); // "lead artist"
    if (m_maker == "") m_maker = loadTag(tag, "TPE2");

    id3_file_close(file);

#else
#ifdef DEBUG_ID3TAG
    std::cerr << "MP3FileReader::loadTags: ID3 tag support not compiled in"
              << std::endl;
#endif
#endif
}

QString
MP3FileReader::loadTag(void *vtag, const char *name)
{
#ifdef HAVE_ID3TAG
    id3_tag *tag = (id3_tag *)vtag;

    id3_frame *frame = id3_tag_findframe(tag, name, 0);
    if (!frame) {
#ifdef DEBUG_ID3TAG
        std::cerr << "MP3FileReader::loadTags: No \"" << name << "\" in ID3 tag" << std::endl;
#endif
        return "";
    }
        
    if (frame->nfields < 2) {
        std::cerr << "MP3FileReader::loadTags: WARNING: Not enough fields (" << frame->nfields << ") for \"" << name << "\" in ID3 tag" << std::endl;
        return "";
    }

    unsigned int nstrings = id3_field_getnstrings(&frame->fields[1]);
    if (nstrings == 0) {
#ifdef DEBUG_ID3TAG
        std::cerr << "MP3FileReader::loadTags: No data for \"" << name << "\" in ID3 tag" << std::endl;
#endif
        return "";
    }

    id3_ucs4_t const *ustr = id3_field_getstrings(&frame->fields[1], 0);
    if (!ustr) {
#ifdef DEBUG_ID3TAG
        std::cerr << "MP3FileReader::loadTags: Invalid or absent data for \"" << name << "\" in ID3 tag" << std::endl;
#endif
        return "";
    }
        
    id3_utf8_t *u8str = id3_ucs4_utf8duplicate(ustr);
    if (!u8str) {
        std::cerr << "MP3FileReader::loadTags: ERROR: Internal error: Failed to convert UCS4 to UTF8 in ID3 title" << std::endl;
        return "";
    }
        
    QString rv = QString::fromUtf8((const char *)u8str);
    free(u8str);

#ifdef DEBUG_ID3TAG
	std::cerr << "MP3FileReader::loadTags: tag \"" << name << "\" -> \""
	<< rv.toStdString() << "\"" << std::endl;
#endif


    return rv;

#else
    return "";
#endif
}

void
MP3FileReader::DecodeThread::run()
{
    if (!m_reader->decode(m_reader->m_filebuffer, m_reader->m_fileSize)) {
        m_reader->m_error = QString("Failed to decode file %1.").arg(m_reader->m_path);
    }

    delete[] m_reader->m_filebuffer;
    m_reader->m_filebuffer = 0;

    if (m_reader->m_samplebuffer) {
        for (size_t c = 0; c < m_reader->m_channelCount; ++c) {
            delete[] m_reader->m_samplebuffer[c];
        }
        delete[] m_reader->m_samplebuffer;
        m_reader->m_samplebuffer = 0;
    }

    if (m_reader->isDecodeCacheInitialised()) m_reader->finishDecodeCache();

    m_reader->m_done = true;
    m_reader->m_completion = 100;

    m_reader->endSerialised();
} 

bool
MP3FileReader::decode(void *mm, size_t sz)
{
    DecoderData data;
    struct mad_decoder decoder;

    data.start = (unsigned char const *)mm;
    data.length = (unsigned long)sz;
    data.reader = this;

    mad_decoder_init(&decoder, &data, input, 0, 0, output, error, 0);
    mad_decoder_run(&decoder, MAD_DECODER_MODE_SYNC);
    mad_decoder_finish(&decoder);

    m_done = true;
    return true;
}

enum mad_flow
MP3FileReader::input(void *dp, struct mad_stream *stream)
{
    DecoderData *data = (DecoderData *)dp;

    if (!data->length) return MAD_FLOW_STOP;
    mad_stream_buffer(stream, data->start, data->length);
    data->length = 0;

    return MAD_FLOW_CONTINUE;
}

enum mad_flow
MP3FileReader::output(void *dp,
		      struct mad_header const *header,
		      struct mad_pcm *pcm)
{
    DecoderData *data = (DecoderData *)dp;
    return data->reader->accept(header, pcm);
}

enum mad_flow
MP3FileReader::accept(struct mad_header const *header,
		      struct mad_pcm *pcm)
{
    int channels = pcm->channels;
    int frames = pcm->length;

    if (header) {
        m_bitrateNum += header->bitrate;
        m_bitrateDenom ++;
    }

    if (frames < 1) return MAD_FLOW_CONTINUE;

    if (m_channelCount == 0) {

        m_fileRate = pcm->samplerate;
        m_channelCount = channels;

        initialiseDecodeCache();

        if (m_cacheMode == CacheInTemporaryFile) {
            m_completion = 1;
            std::cerr << "MP3FileReader::accept: channel count " << m_channelCount << ", file rate " << m_fileRate << ", about to start serialised section" << std::endl;
            startSerialised("MP3FileReader::Decode");
        }
    }
    
    if (m_bitrateDenom > 0) {
        double bitrate = m_bitrateNum / m_bitrateDenom;
        double duration = double(m_fileSize * 8) / bitrate;
        double elapsed = double(m_frameCount) / m_sampleRate;
        double percent = ((elapsed * 100.0) / duration);
        int progress = int(percent);
        if (progress < 1) progress = 1;
        if (progress > 99) progress = 99;
        m_completion = progress;
        if (m_progress) {
            if (progress > m_progress->value()) {
                m_progress->setValue(progress);
                m_progress->show();
                m_progress->raise();
                qApp->processEvents();
                if (m_progress->wasCanceled()) {
                    m_cancelled = true;
                }
            }
        }
    }

    if (m_cancelled) return MAD_FLOW_STOP;

    if (!isDecodeCacheInitialised()) {
        initialiseDecodeCache();
    }

    if (m_samplebuffersize < frames) {
        if (!m_samplebuffer) {
            m_samplebuffer = new float *[channels];
            for (int c = 0; c < channels; ++c) {
                m_samplebuffer[c] = 0;
            }
        }
        for (int c = 0; c < channels; ++c) {
            delete[] m_samplebuffer[c];
            m_samplebuffer[c] = new float[frames];
        }
        m_samplebuffersize = frames;
    }

    int activeChannels = int(sizeof(pcm->samples) / sizeof(pcm->samples[0]));

    for (int ch = 0; ch < channels; ++ch) {

        for (int i = 0; i < frames; ++i) {

	    mad_fixed_t sample = 0;
	    if (ch < activeChannels) {
		sample = pcm->samples[ch][i];
	    }
	    float fsample = float(sample) / float(MAD_F_ONE);
            
            m_samplebuffer[ch][i] = fsample;
	}
    }

    addSamplesToDecodeCache(m_samplebuffer, frames);

    return MAD_FLOW_CONTINUE;
}

enum mad_flow
MP3FileReader::error(void *dp,
		     struct mad_stream *stream,
		     struct mad_frame *)
{
    DecoderData *data = (DecoderData *)dp;

    fprintf(stderr, "decoding error 0x%04x (%s) at byte offset %u\n",
	    stream->error, mad_stream_errorstr(stream),
	    stream->this_frame - data->start);

    return MAD_FLOW_CONTINUE;
}

void
MP3FileReader::getSupportedExtensions(std::set<QString> &extensions)
{
    extensions.insert("mp3");
}

bool
MP3FileReader::supportsExtension(QString extension)
{
    std::set<QString> extensions;
    getSupportedExtensions(extensions);
    return (extensions.find(extension.toLower()) != extensions.end());
}

bool
MP3FileReader::supportsContentType(QString type)
{
    return (type == "audio/mpeg");
}

bool
MP3FileReader::supports(FileSource &source)
{
    return (supportsExtension(source.getExtension()) ||
            supportsContentType(source.getContentType()));
}


#endif
