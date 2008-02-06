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

#include "AlignmentModel.h"

#include "SparseTimeValueModel.h"

AlignmentModel::AlignmentModel(Model *reference,
                               Model *aligned,
                               Model *inputModel,
			       SparseTimeValueModel *path) :
    m_reference(reference),
    m_aligned(aligned),
    m_inputModel(inputModel),
    m_rawPath(path),
    m_path(0),
    m_reversePath(0),
    m_pathBegun(false),
    m_pathComplete(false)
{
    if (m_rawPath) {

        connect(m_rawPath, SIGNAL(modelChanged()),
                this, SLOT(pathChanged()));

        connect(m_rawPath, SIGNAL(modelChanged(size_t, size_t)),
                this, SLOT(pathChanged(size_t, size_t)));
        
        connect(m_rawPath, SIGNAL(completionChanged()),
                this, SLOT(pathCompletionChanged()));
    }

    constructPath();
    constructReversePath();
}

AlignmentModel::~AlignmentModel()
{
    delete m_inputModel;
    delete m_rawPath;
    delete m_path;
    delete m_reversePath;
}

bool
AlignmentModel::isOK() const
{
    if (m_rawPath) return m_rawPath->isOK();
    else return true;
}

size_t
AlignmentModel::getStartFrame() const
{
    size_t a = m_reference->getStartFrame();
    size_t b = m_aligned->getStartFrame();
    return std::min(a, b);
}

size_t
AlignmentModel::getEndFrame() const
{
    size_t a = m_reference->getEndFrame();
    size_t b = m_aligned->getEndFrame();
    return std::max(a, b);
}

size_t
AlignmentModel::getSampleRate() const
{
    return m_reference->getSampleRate();
}

Model *
AlignmentModel::clone() const
{
    return new AlignmentModel
        (m_reference, m_aligned,
         m_inputModel ? m_inputModel->clone() : 0,
         m_rawPath ? static_cast<SparseTimeValueModel *>(m_rawPath->clone()) : 0);
}

bool
AlignmentModel::isReady(int *completion) const
{
    if (!m_pathBegun) {
        if (completion) *completion = 0;
        return false;
    }
    if (m_pathComplete || !m_rawPath) {
        if (completion) *completion = 100;
        return true;
    }
    return m_rawPath->isReady(completion);
}

const ZoomConstraint *
AlignmentModel::getZoomConstraint() const
{
    return 0;
}

const Model *
AlignmentModel::getReferenceModel() const
{
    return m_reference;
}

const Model *
AlignmentModel::getAlignedModel() const
{
    return m_aligned;
}

size_t
AlignmentModel::toReference(size_t frame) const
{
//    std::cerr << "AlignmentModel::toReference(" << frame << ")" << std::endl;
    if (!m_path) {
        if (!m_rawPath) return frame;
        constructPath();
    }
    return align(m_path, frame);
}

size_t
AlignmentModel::fromReference(size_t frame) const
{
//    std::cerr << "AlignmentModel::fromReference(" << frame << ")" << std::endl;
    if (!m_reversePath) {
        if (!m_rawPath) return frame;
        constructReversePath();
    }
    return align(m_reversePath, frame);
}

void
AlignmentModel::pathChanged()
{
    if (m_pathComplete) {
        std::cerr << "AlignmentModel: deleting raw path model" << std::endl;
        delete m_rawPath;
        m_rawPath = 0;
    }
}

void
AlignmentModel::pathChanged(size_t, size_t)
{
    if (!m_pathComplete) return;
    constructPath();
    constructReversePath();
}    

void
AlignmentModel::pathCompletionChanged()
{
    if (!m_rawPath) return;
    m_pathBegun = true;

    if (!m_pathComplete) {

        int completion = 0;
        m_rawPath->isReady(&completion);

//        std::cerr << "AlignmentModel::pathCompletionChanged: completion = "
//                  << completion << std::endl;

        m_pathComplete = (completion == 100);

        if (m_pathComplete) {

            constructPath();
            constructReversePath();

            delete m_inputModel;
            m_inputModel = 0;
        }
    }

    emit completionChanged();
}

void
AlignmentModel::constructPath() const
{
    if (!m_path) {
        if (!m_rawPath) {
            std::cerr << "ERROR: AlignmentModel::constructPath: "
                      << "No raw path available" << std::endl;
            return;
        }
        m_path = new PathModel
            (m_rawPath->getSampleRate(), m_rawPath->getResolution(), false);
    } else {
        if (!m_rawPath) return;
    }
        
    m_path->clear();

    SparseTimeValueModel::PointList points = m_rawPath->getPoints();
        
    for (SparseTimeValueModel::PointList::const_iterator i = points.begin();
         i != points.end(); ++i) {
        long frame = i->frame;
        float value = i->value;
        long rframe = lrintf(value * m_aligned->getSampleRate());
        m_path->addPoint(PathPoint(frame, rframe));
    }

//    std::cerr << "AlignmentModel::constructPath: " << m_path->getPointCount() << " points, at least " << (2 * m_path->getPointCount() * (3 * sizeof(void *) + sizeof(int) + sizeof(PathPoint))) << " bytes" << std::endl;
}

void
AlignmentModel::constructReversePath() const
{
    if (!m_reversePath) {
        if (!m_rawPath) {
            std::cerr << "ERROR: AlignmentModel::constructReversePath: "
                      << "No raw path available" << std::endl;
            return;
        }
        m_reversePath = new PathModel
            (m_rawPath->getSampleRate(), m_rawPath->getResolution(), false);
    } else {
        if (!m_rawPath) return;
    }
        
    m_reversePath->clear();

    SparseTimeValueModel::PointList points = m_rawPath->getPoints();
        
    for (SparseTimeValueModel::PointList::const_iterator i = points.begin();
         i != points.end(); ++i) {
        long frame = i->frame;
        float value = i->value;
        long rframe = lrintf(value * m_aligned->getSampleRate());
        m_reversePath->addPoint(PathPoint(rframe, frame));
    }

//    std::cerr << "AlignmentModel::constructReversePath: " << m_reversePath->getPointCount() << " points, at least " << (2 * m_reversePath->getPointCount() * (3 * sizeof(void *) + sizeof(int) + sizeof(PathPoint))) << " bytes" << std::endl;
}

size_t
AlignmentModel::align(PathModel *path, size_t frame) const
{
    if (!path) return frame;

    // The path consists of a series of points, each with frame equal
    // to the frame on the source model and mapframe equal to the
    // frame on the target model.  Both should be monotonically
    // increasing.

    const PathModel::PointList &points = path->getPoints();

    if (points.empty()) {
//        std::cerr << "AlignmentModel::align: No points" << std::endl;
        return frame;
    }        

    PathModel::Point point(frame);
    PathModel::PointList::const_iterator i = points.lower_bound(point);
    if (i == points.end()) --i;
    while (i != points.begin() && i->frame > long(frame)) --i;

    long foundFrame = i->frame;
    long foundMapFrame = i->mapframe;

    long followingFrame = foundFrame;
    long followingMapFrame = foundMapFrame;

    if (++i != points.end()) {
        followingFrame = i->frame;
        followingMapFrame = i->mapframe;
    }

    if (foundMapFrame < 0) return 0;

    size_t resultFrame = foundMapFrame;

    if (followingFrame != foundFrame && long(frame) > foundFrame) {
        float interp =
            float(frame - foundFrame) /
            float(followingFrame - foundFrame);
        resultFrame += lrintf((followingMapFrame - foundMapFrame) * interp);
    }

//    std::cerr << "AlignmentModel::align: resultFrame = " << resultFrame << std::endl;

    return resultFrame;
}
    
