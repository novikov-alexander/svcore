/* -*- c-basic-offset: 4 indent-tabs-mode: nil -*-  vi:set ts=8 sts=4 sw=4: */

/*
    Sonic Visualiser
    An audio file viewer and annotation editor.
    Centre for Digital Music, Queen Mary, University of London.
    This file copyright 2007 QMUL.
    
    This program is free software; you can redistribute it and/or
    modify it under the terms of the GNU General Public License as
    published by the Free Software Foundation; either version 2 of the
    License, or (at your option) any later version.  See the file
    COPYING included with this distribution for more information.
*/

#ifndef SV_PATH_MODEL_H
#define SV_PATH_MODEL_H

#include "Model.h"
#include "SparseModel.h"
#include "base/RealTime.h"
#include "base/BaseTypes.h"

#include <QStringList>


struct PathPoint
{
    PathPoint(sv_frame_t _frame) : frame(_frame), mapframe(_frame) { }
    PathPoint(sv_frame_t _frame, sv_frame_t _mapframe) :
        frame(_frame), mapframe(_mapframe) { }

    int getDimensions() const { return 2; }

    sv_frame_t frame;
    sv_frame_t mapframe;

    QString getLabel() const { return ""; }

    void toXml(QTextStream &stream, QString indent = "",
               QString extraAttributes = "") const {
        stream << QString("%1<point frame=\"%2\" mapframe=\"%3\" %4/>\n")
            .arg(indent).arg(frame).arg(mapframe).arg(extraAttributes);
    }
        
    QString toDelimitedDataString(QString delimiter, DataExportOptions,
                                  sv_samplerate_t sampleRate) const {
        QStringList list;
        list << RealTime::frame2RealTime(frame, sampleRate).toString().c_str();
        list << QString("%1").arg(mapframe);
        return list.join(delimiter);
    }

    struct Comparator {
        bool operator()(const PathPoint &p1, const PathPoint &p2) const {
            if (p1.frame != p2.frame) return p1.frame < p2.frame;
            return p1.mapframe < p2.mapframe;
        }
    };
    
    struct OrderComparator {
        bool operator()(const PathPoint &p1, const PathPoint &p2) const {
            return p1.frame < p2.frame;
        }
    };
};

class PathModel : public SparseModel<PathPoint>
{
public:
    PathModel(sv_samplerate_t sampleRate, int resolution, bool notify = true) :
        SparseModel<PathPoint>(sampleRate, resolution, notify) { }

    void toXml(QTextStream &out,
                       QString indent = "",
                       QString extraAttributes = "") const override
    {
        SparseModel<PathPoint>::toXml
            (out, 
             indent,
             QString("%1 subtype=\"path\"")
             .arg(extraAttributes));
    }

    /**
     * TabularModel is inherited via SparseModel, but we don't need it here.
     */
    QString getHeading(int) const override { return ""; }
    bool isColumnTimeValue(int) const override { return false; }
    SortType getSortType(int) const override { return SortNumeric; }

};


#endif
