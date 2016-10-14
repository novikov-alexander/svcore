/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2006 Chris Cannam and QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#include "FeatureExtractionPluginFactory.h"
#include "PluginIdentifier.h"

#include <vamp-hostsdk/PluginHostAdapter.h>
#include <vamp-hostsdk/PluginWrapper.h>

#include "system/System.h"

#include "PluginScan.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <iostream>

#include "base/Profiler.h"

using namespace std;

//#define DEBUG_PLUGIN_SCAN_AND_INSTANTIATE 1

static FeatureExtractionPluginFactory *_nativeInstance = 0;

FeatureExtractionPluginFactory *
FeatureExtractionPluginFactory::instance(QString pluginType)
{
    if (pluginType == "vamp") {
	if (!_nativeInstance) {
//	    SVDEBUG << "FeatureExtractionPluginFactory::instance(" << pluginType//		      << "): creating new FeatureExtractionPluginFactory" << endl;
	    _nativeInstance = new FeatureExtractionPluginFactory();
	}
	return _nativeInstance;
    }

    else return 0;
}

FeatureExtractionPluginFactory *
FeatureExtractionPluginFactory::instanceFor(QString identifier)
{
    QString type, soName, label;
    PluginIdentifier::parseIdentifier(identifier, type, soName, label);
    return instance(type);
}

FeatureExtractionPluginFactory::FeatureExtractionPluginFactory() :
    m_transport("piper-cpp/bin/piper-vamp-server"),
    m_client(&m_transport)
{
}

vector<QString>
FeatureExtractionPluginFactory::getAllPluginIdentifiers()
{
    FeatureExtractionPluginFactory *factory;
    vector<QString> rv;
    
    factory = instance("vamp");
    if (factory) {
	vector<QString> tmp = factory->getPluginIdentifiers();
	for (size_t i = 0; i < tmp.size(); ++i) {
//            cerr << "identifier: " << tmp[i] << endl;
	    rv.push_back(tmp[i]);
	}
    }

    // Plugins can change the locale, revert it to default.
    RestoreStartupLocale();

    return rv;
}

vector<QString>
FeatureExtractionPluginFactory::getPluginIdentifiers()
{
    Profiler profiler("FeatureExtractionPluginFactory::getPluginIdentifiers");

    QMutexLocker locker(&m_mutex);

    if (m_pluginData.empty()) {
        populate();
    }

    vector<QString> rv;

    for (const auto &d: m_pluginData) {
        rv.push_back(QString("vamp:") + QString::fromStdString(d.pluginKey));
    }

    return rv;
}

Vamp::Plugin *
FeatureExtractionPluginFactory::instantiatePlugin(QString identifier,
						  sv_samplerate_t inputSampleRate)
{
    Profiler profiler("FeatureExtractionPluginFactory::instantiatePlugin");

    QString type, soname, label;
    PluginIdentifier::parseIdentifier(identifier, type, soname, label);

    piper_vamp::LoadRequest request;
    request.pluginKey = (soname + ":" + label).toStdString();
    request.inputSampleRate = inputSampleRate;
    request.adapterFlags = 0;
    piper_vamp::LoadResponse response = m_client.loadPlugin(request);

    return response.plugin;
}

QString
FeatureExtractionPluginFactory::getPluginCategory(QString identifier)
{
    //!!! (re)implement
//    return m_taxonomy[identifier];
    return QString();
}

void
FeatureExtractionPluginFactory::populate()
{
    piper_vamp::ListResponse lr = m_client.listPluginData();
    m_pluginData = lr.available;
}

