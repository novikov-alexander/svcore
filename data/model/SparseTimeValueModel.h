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

#ifndef _SPARSE_TIME_VALUE_MODEL_H_
#define _SPARSE_TIME_VALUE_MODEL_H_

#include "SparseValueModel.h"
#include "base/PlayParameterRepository.h"
#include "base/RealTime.h"

/**
 * Time/value point type for use in a SparseModel or SparseValueModel.
 * With this point type, the model basically represents a wiggly-line
 * plot with points at arbitrary intervals of the model resolution.
 */

struct TimeValuePoint
{
public:
    TimeValuePoint(long _frame) : frame(_frame), value(0.0f) { }
    TimeValuePoint(long _frame, float _value, QString _label) : 
	frame(_frame), value(_value), label(_label) { }

    int getDimensions() const { return 2; }
    
    long frame;
    float value;
    QString label;

    QString getLabel() const { return label; }
    
    void toXml(QTextStream &stream, QString indent = "",
               QString extraAttributes = "") const
    {
        stream << QString("%1<point frame=\"%2\" value=\"%3\" label=\"%4\" %5/>\n")
	    .arg(indent).arg(frame).arg(value).arg(label).arg(extraAttributes);
    }

    QString toDelimitedDataString(QString delimiter, size_t sampleRate) const
    {
        QStringList list;
        list << RealTime::frame2RealTime(frame, sampleRate).toString().c_str();
        list << QString("%1").arg(value);
        if (label != "") list << label;
        return list.join(delimiter);
    }

    struct Comparator {
	bool operator()(const TimeValuePoint &p1,
			const TimeValuePoint &p2) const {
	    if (p1.frame != p2.frame) return p1.frame < p2.frame;
	    if (p1.value != p2.value) return p1.value < p2.value;
	    return p1.label < p2.label;
	}
    };
    
    struct OrderComparator {
	bool operator()(const TimeValuePoint &p1,
			const TimeValuePoint &p2) const {
	    return p1.frame < p2.frame;
	}
    };
};


class SparseTimeValueModel : public SparseValueModel<TimeValuePoint>
{
    Q_OBJECT
    
public:
    SparseTimeValueModel(size_t sampleRate, size_t resolution,
			 bool notifyOnAdd = true) :
	SparseValueModel<TimeValuePoint>(sampleRate, resolution,
					 notifyOnAdd)
    {
        // not yet playable
    }

    SparseTimeValueModel(size_t sampleRate, size_t resolution,
			 float valueMinimum, float valueMaximum,
			 bool notifyOnAdd = true) :
	SparseValueModel<TimeValuePoint>(sampleRate, resolution,
					 valueMinimum, valueMaximum,
					 notifyOnAdd)
    {
        // not yet playable
    }

    QString getTypeName() const { return tr("Sparse Time-Value"); }

    /**
     * TabularModel methods.  
     */
    
    virtual int getColumnCount() const
    {
        return 4;
    }

    virtual QString getHeading(int column) const
    {
        switch (column) {
        case 0: return tr("Time");
        case 1: return tr("Frame");
        case 2: return tr("Value");
        case 3: return tr("Label");
        default: return tr("Unknown");
        }
    }

    virtual QVariant getData(int row, int column, int role) const
    {
        PointListIterator i = getPointListIteratorForRow(row);
        if (i == m_points.end()) return QVariant();

        switch (column) {
        case 0: {
            if (role == SortRole) return int(i->frame);
            RealTime rt = RealTime::frame2RealTime(i->frame, getSampleRate());
            return rt.toText().c_str();
        }
        case 1: return int(i->frame);
        case 2:
            if (role == Qt::EditRole || role == SortRole) return i->value;
            else return QString("%1 %2").arg(i->value).arg(getScaleUnits());
        case 3: return i->label;
        default: return QVariant();
        }
    }

    virtual Command *getSetDataCommand(int row, int column, const QVariant &value, int role)
    {
        if (role != Qt::EditRole) return false;
        PointListIterator i = getPointListIteratorForRow(row);
        if (i == m_points.end()) return false;
        EditCommand *command = new EditCommand(this, tr("Edit Data"));

        Point point(*i);
        command->deletePoint(point);

        switch (column) {
        case 0: case 1: point.frame = value.toInt(); break; 
        case 2: point.value = value.toDouble(); break;
        case 3: point.label = value.toString(); break;
        }

        command->addPoint(point);
        return command->finish();
    }

    virtual bool isColumnTimeValue(int column) const
    {
        return (column < 2); 
    }

    virtual SortType getSortType(int column) const
    {
        if (column == 3) return SortAlphabetical;
        return SortNumeric;
    }
};


#endif


    
