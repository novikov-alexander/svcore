/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006-2016 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "HelperExecPath.h"

#include "Debug.h"

#include <QCoreApplication>
#include <QFile>
#include <QDir>
#include <QFileInfo>

QStringList
HelperExecPath::getTags()
{
    (void)m_type; // some compilation paths don't refer to this and
                  // that can cause a warning
    
    if (sizeof(void *) == 4) {
        // 32-bit, gets the most basic treatment
        return { "32", "" };
    }

#ifdef Q_OS_MAC
#if (defined(__aarch64__) || defined(__arm__) || defined(_M_ARM64))
    if (m_type == NativeArchitectureOnly) {
        return { "arm64", "" };
    } else {
        return { "arm64", "", "x86_64", "translated" };
    }
#elif (defined(__x86_64__) || defined(__i386__) || defined(_M_IX86) || defined(_M_X64))
    return { "x86_64", "" };
#else
#warning "Unknown Mac architecture - can't determine whether we're arm64 or x86_64"
    return { "" };
#endif
#else // not Q_OS_MAC
    if (m_type == NativeArchitectureOnly) {
        return { "64", "" };
    } else {
        return { "64", "", "32", "translated" };
    }
#endif
}

static bool
isGood(QString path)
{
    return QFile(path).exists() && QFileInfo(path).isExecutable();
}

QList<HelperExecPath::HelperExec>
HelperExecPath::getHelperExecutables(QString basename)
{
    QStringList dummy;
    return search(basename, dummy);
}

QString
HelperExecPath::getHelperExecutable(QString basename)
{
    auto execs = getHelperExecutables(basename);
    if (execs.empty()) return "";
    else return execs[0].executable;
}

QStringList
HelperExecPath::getHelperDirPaths()
{
    // Helpers are expected to exist in one of the following, in order
    // from most strongly preferred to least:
    //
    // 1. (on Mac only) in <mydir>/../Resources
    //
    // 2. (on non-Windows non-Mac platforms only) in
    // <mydir>/../lib/application-name/
    //
    // 3. (on non-Mac platforms only) in <mydir>/helpers
    //
    // 4. in <mydir>

    QStringList dirs;
    QString appName = QCoreApplication::applicationName();
    QString myDir = QCoreApplication::applicationDirPath();
    QString binaryName = QFileInfo(QCoreApplication::arguments().at(0))
        .fileName();
        
#ifdef Q_OS_MAC
    dirs.push_back(myDir + "/../Resources");
#else
#ifndef Q_OS_WIN32
    if (binaryName != "") {
        dirs.push_back(myDir + "/../lib/" + binaryName);
    }
    dirs.push_back(myDir + "/../lib/" + appName);
#endif
    dirs.push_back(myDir + "/helpers");
#endif
    dirs.push_back(myDir);
    return dirs;
}

QStringList
HelperExecPath::getBundledPluginPaths()
{
    // Plugins are expected to exist in one of the following, in order
    // from most strongly preferred to least:
    //
    // 1. (on Mac only) in <mydir>/../Resources
    //
    // 2. (on non-Windows non-Mac platforms only) in
    // <mydir>/../lib/application-name/plugins/
    //
    // 3. (on non-Mac platforms only) in <mydir>/plugins/

    QStringList dirs;
    QString appName = QCoreApplication::applicationName();
    QString myDir = QCoreApplication::applicationDirPath();
    QString binaryName = QFileInfo(QCoreApplication::arguments().at(0))
        .fileName();
        
#ifdef Q_OS_MAC
    dirs.push_back(myDir + "/../Resources");
#else
#ifndef Q_OS_WIN32
    if (binaryName != "") {
        dirs.push_back(myDir + "/../lib/" + binaryName + "/plugins");
    }
    dirs.push_back(myDir + "/../lib/" + appName + "/plugins");
#endif
    dirs.push_back(myDir + "/plugins");
#endif
    return dirs;
}

QStringList
HelperExecPath::getHelperCandidatePaths(QString basename)
{
    QStringList candidates;
    (void)search(basename, candidates);
    return candidates;
}

QList<HelperExecPath::HelperExec>
HelperExecPath::search(QString basename, QStringList &candidates)
{
    QString extension = "";
#ifdef _WIN32
    extension = ".exe";
#endif

    QList<HelperExec> executables;
    QStringList dirs = getHelperDirPaths();
    QStringList tags = getTags();

    SVDEBUG << "HelperExecPath::search(" << basename << "): dirs = "
            << dirs.join(",") << ", tags = " << tags.join(",") << endl;
    
    for (QString t: tags) {
        for (QString d: dirs) {
            QString path = d + QDir::separator() + basename;
            if (t != QString()) path += "-" + t;
            path += extension;
            candidates.push_back(path);
            if (isGood(path)) {
                executables.push_back({ path, t });
                break;
            }
        }
    }

    return executables;
}

