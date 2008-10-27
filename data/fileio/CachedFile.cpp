/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2008 QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "CachedFile.h"

#include "base/TempDirectory.h"
#include "base/ProgressReporter.h"
#include "base/Exceptions.h"

#include "FileSource.h"

#include <QFileInfo>
#include <QSettings>
#include <QVariant>
#include <QMap>
#include <QDir>
#include <QCryptographicHash>

#include <iostream>

QString
CachedFile::getLocalFilenameFor(QUrl url)
{
    QDir dir(getCacheDirectory());

    QString filename =
        QString::fromLocal8Bit
        (QCryptographicHash::hash(url.toString().toLocal8Bit(),
                                  QCryptographicHash::Sha1).toHex());

    return dir.filePath(filename);
}

QString
CachedFile::getCacheDirectory()
{
    QDir dir = TempDirectory::getInstance()->getContainingPath();

    QString cacheDirName("cache");

    QFileInfo fi(dir.filePath(cacheDirName));

    if ((fi.exists() && !fi.isDir()) ||
        (!fi.exists() && !dir.mkdir(cacheDirName))) {

        throw DirectoryCreationFailed(fi.filePath());
    }

    return fi.filePath();
}

CachedFile::CachedFile(QString url, ProgressReporter *reporter) :
    m_url(url),
    m_localFilename(getLocalFilenameFor(m_url)),
    m_reporter(reporter),
    m_ok(false)
{
    std::cerr << "CachedFile::CachedFile: url is \""
              << url.toStdString() << "\"" << std::endl;
    check();
}

CachedFile::CachedFile(QUrl url, ProgressReporter *reporter) :
    m_url(url),
    m_localFilename(getLocalFilenameFor(m_url)),
    m_reporter(reporter),
    m_ok(false)
{
    std::cerr << "CachedFile::CachedFile: url is \""
              << url.toString().toStdString() << "\"" << std::endl;
    check();
}

CachedFile::~CachedFile()
{
}

bool
CachedFile::isOK() const
{
    return m_ok;
}

QString
CachedFile::getLocalFilename() const
{
    return m_localFilename;
}

void
CachedFile::check()
{
    //!!! n.b. obvious race condition here if different CachedFile
    // objects for same url used in more than one thread -- need to
    // lock appropriately.  also consider race condition between
    // separate instances of the program

    if (!QFileInfo(m_localFilename).exists()) {
        std::cerr << "CachedFile::check: Local file does not exist, making a note that it hasn't been retrieved" << std::endl;
        updateLastRetrieval(false); // empirically!
    }

    QDateTime lastRetrieval = getLastRetrieval();

    if (lastRetrieval.isValid()) {
        std::cerr << "CachedFile::check: Valid last retrieval at "
                  << lastRetrieval.toString().toStdString() << std::endl;
        // this will not be the case if the file is missing, after
        // updateLastRetrieval(false) was called above
        m_ok = true;
        if (lastRetrieval.addDays(2) < QDateTime::currentDateTime()) { //!!!
            std::cerr << "CachedFile::check: Out of date; trying to retrieve again" << std::endl;
            // doesn't matter if retrieval fails -- we just don't
            // update the last retrieval time

            //!!! but we do want an additional last-attempted
            // timestamp so as to ensure we aren't retrying the
            // retrieval every single time if it isn't working

            if (retrieve()) {
                std::cerr << "CachedFile::check: Retrieval succeeded" << std::endl;
                updateLastRetrieval(true);
            } else {
                std::cerr << "CachedFile::check: Retrieval failed, will try again later (using existing file for now)" << std::endl;
            }                
        }
    } else {
        std::cerr << "CachedFile::check: No valid last retrieval" << std::endl;
        // there is no acceptable file
        if (retrieve()) {
            std::cerr << "CachedFile::check: Retrieval succeeded" << std::endl;
            m_ok = true;
            updateLastRetrieval(true);
        } else {
            std::cerr << "CachedFile::check: Retrieval failed, remaining in invalid state" << std::endl;
            // again, we don't need to do anything here -- the last
            // retrieval timestamp is already invalid
        }
    }
}

bool
CachedFile::retrieve()
{
    //!!! need to work by retrieving the file to another name, and
    //!!! then "atomically" moving it to its proper place (I'm not
    //!!! sure we can do an atomic move to replace an existing file
    //!!! using Qt classes, but a plain delete then copy is probably
    //!!! good enough)

    FileSource fs(m_url, m_reporter);

    if (!fs.isOK() || !fs.isAvailable()) {
        return false;
    }

    fs.waitForData();

    if (!fs.isOK()) {
        return false;
    }

    QString tempName = fs.getLocalFilename();
    QFile tempFile(tempName);
    if (!tempFile.exists()) {
        std::cerr << "CachedFile::retrieve: ERROR: FileSource reported success, but local temporary file \"" << tempName.toStdString() << "\" does not exist" << std::endl;
        return false;
    }

    QFile previous(m_localFilename);
    if (previous.exists()) {
        if (!previous.remove()) {
            std::cerr << "CachedFile::retrieve: ERROR: Failed to remove previous copy of cached file at \"" << m_localFilename.toStdString() << "\"" << std::endl;
            return false;
        }
    }

    //!!! This is not ideal, could leave us with nothing (old file
    //!!! removed, new file not able to be copied in because e.g. no
    //!!! disk space left)

    if (!tempFile.copy(m_localFilename)) {
        std::cerr << "CachedFile::retrieve: ERROR: Failed to copy newly retrieved file from \"" << tempName.toStdString() << "\" to \"" << m_localFilename.toStdString() << "\"" << std::endl;
        return false;
    }

    return true;
}

QDateTime
CachedFile::getLastRetrieval()
{
    QSettings settings;
    settings.beginGroup("FileCache");

    QString key("last-retrieval-times");

    QMap<QString, QVariant> timeMap = settings.value(key).toMap();
    QDateTime lastTime = timeMap[m_localFilename].toDateTime();

    settings.endGroup();
    return lastTime;
}

void
CachedFile::updateLastRetrieval(bool successful)
{
    //!!! note !successful does not mean "we failed to update the
    //!!! file" (and so it remains the same as before); it means "the
    //!!! file is not there at all"
    
    QSettings settings;
    settings.beginGroup("FileCache");

    QString key("last-retrieval-times");

    QMap<QString, QVariant> timeMap = settings.value(key).toMap();

    QDateTime dt;
    if (successful) dt = QDateTime::currentDateTime();

    timeMap[m_localFilename] = dt;
    settings.setValue(key, timeMap);

    settings.endGroup();
}


