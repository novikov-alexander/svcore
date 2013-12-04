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

#include "FeatureExtractionModelTransformer.h"

#include "plugin/FeatureExtractionPluginFactory.h"
#include "plugin/PluginXml.h"
#include <vamp-hostsdk/Plugin.h>

#include "data/model/Model.h"
#include "base/Window.h"
#include "base/Exceptions.h"
#include "data/model/SparseOneDimensionalModel.h"
#include "data/model/SparseTimeValueModel.h"
#include "data/model/EditableDenseThreeDimensionalModel.h"
#include "data/model/DenseTimeValueModel.h"
#include "data/model/NoteModel.h"
#include "data/model/FlexiNoteModel.h"
#include "data/model/RegionModel.h"
#include "data/model/FFTModel.h"
#include "data/model/WaveFileModel.h"
#include "rdf/PluginRDFDescription.h"

#include "TransformFactory.h"

#include <iostream>

#include <QSettings>

FeatureExtractionModelTransformer::FeatureExtractionModelTransformer(Input in,
                                                                     const Transform &transform) :
    ModelTransformer(in, transform),
    m_plugin(0)
{
//    SVDEBUG << "FeatureExtractionModelTransformer::FeatureExtractionModelTransformer: plugin " << pluginId << ", outputName " << m_transform.getOutput() << endl;

    initialise();
}

FeatureExtractionModelTransformer::FeatureExtractionModelTransformer(Input in,
                                                                     const Transforms &transforms) :
    ModelTransformer(in, transforms),
    m_plugin(0)
{
//    SVDEBUG << "FeatureExtractionModelTransformer::FeatureExtractionModelTransformer: plugin " << pluginId << ", outputName " << m_transform.getOutput() << endl;

    initialise();
}

static bool
areTransformsSimilar(const Transform &t1, const Transform &t2)
{
    Transform t2o(t2);
    t2o.setOutput(t1.getOutput());
    return t1 == t2o;
}

bool
FeatureExtractionModelTransformer::initialise()
{
    // All transforms must use the same plugin, parameters, and
    // inputs: they can differ only in choice of plugin output. So we
    // initialise based purely on the first transform in the list (but
    // first check that they are actually similar as promised)

    for (int j = 1; j < (int)m_transforms.size(); ++j) {
        if (!areTransformsSimilar(m_transforms[0], m_transforms[j])) {
            m_message = tr("Transforms supplied to a single FeatureExtractionModelTransformer instance must be similar in every respect except plugin output");
            return false;
        }
    }

    Transform primaryTransform = m_transforms[0];

    QString pluginId = primaryTransform.getPluginIdentifier();

    FeatureExtractionPluginFactory *factory =
	FeatureExtractionPluginFactory::instanceFor(pluginId);

    if (!factory) {
        m_message = tr("No factory available for feature extraction plugin id \"%1\" (unknown plugin type, or internal error?)").arg(pluginId);
	return false;
    }

    DenseTimeValueModel *input = getConformingInput();
    if (!input) {
        m_message = tr("Input model for feature extraction plugin \"%1\" is of wrong type (internal error?)").arg(pluginId);
        return false;
    }

    m_plugin = factory->instantiatePlugin(pluginId, input->getSampleRate());
    if (!m_plugin) {
        m_message = tr("Failed to instantiate plugin \"%1\"").arg(pluginId);
	return false;
    }

    TransformFactory::getInstance()->makeContextConsistentWithPlugin
        (primaryTransform, m_plugin);

    TransformFactory::getInstance()->setPluginParameters
        (primaryTransform, m_plugin);

    size_t channelCount = input->getChannelCount();
    if (m_plugin->getMaxChannelCount() < channelCount) {
	channelCount = 1;
    }
    if (m_plugin->getMinChannelCount() > channelCount) {
        m_message = tr("Cannot provide enough channels to feature extraction plugin \"%1\" (plugin min is %2, max %3; input model has %4)")
            .arg(pluginId)
            .arg(m_plugin->getMinChannelCount())
            .arg(m_plugin->getMaxChannelCount())
            .arg(input->getChannelCount());
	return false;
    }

    SVDEBUG << "Initialising feature extraction plugin with channels = "
              << channelCount << ", step = " << primaryTransform.getStepSize()
              << ", block = " << primaryTransform.getBlockSize() << endl;

    if (!m_plugin->initialise(channelCount,
                              primaryTransform.getStepSize(),
                              primaryTransform.getBlockSize())) {

        size_t pstep = primaryTransform.getStepSize();
        size_t pblock = primaryTransform.getBlockSize();

///!!! hang on, this isn't right -- we're modifying a copy
        primaryTransform.setStepSize(0);
        primaryTransform.setBlockSize(0);
        TransformFactory::getInstance()->makeContextConsistentWithPlugin
            (primaryTransform, m_plugin);

        if (primaryTransform.getStepSize() != pstep ||
            primaryTransform.getBlockSize() != pblock) {
            
            if (!m_plugin->initialise(channelCount,
                                      primaryTransform.getStepSize(),
                                      primaryTransform.getBlockSize())) {

                m_message = tr("Failed to initialise feature extraction plugin \"%1\"").arg(pluginId);
                return false;

            } else {

                m_message = tr("Feature extraction plugin \"%1\" rejected the given step and block sizes (%2 and %3); using plugin defaults (%4 and %5) instead")
                    .arg(pluginId)
                    .arg(pstep)
                    .arg(pblock)
                    .arg(primaryTransform.getStepSize())
                    .arg(primaryTransform.getBlockSize());
            }

        } else {

            m_message = tr("Failed to initialise feature extraction plugin \"%1\"").arg(pluginId);
            return false;
        }
    }

    if (primaryTransform.getPluginVersion() != "") {
        QString pv = QString("%1").arg(m_plugin->getPluginVersion());
        if (pv != primaryTransform.getPluginVersion()) {
            QString vm = tr("Transform was configured for version %1 of plugin \"%2\", but the plugin being used is version %3")
                .arg(primaryTransform.getPluginVersion())
                .arg(pluginId)
                .arg(pv);
            if (m_message != "") {
                m_message = QString("%1; %2").arg(vm).arg(m_message);
            } else {
                m_message = vm;
            }
        }
    }

    Vamp::Plugin::OutputList outputs = m_plugin->getOutputDescriptors();

    if (outputs.empty()) {
        m_message = tr("Plugin \"%1\" has no outputs").arg(pluginId);
	return false;
    }

    for (int j = 0; j < (int)m_transforms.size(); ++j) {

        for (int i = 0; i < (int)outputs.size(); ++i) {
//        SVDEBUG << "comparing output " << i << " name \"" << outputs[i].identifier << "\" with expected \"" << m_transform.getOutput() << "\"" << endl;
            if (m_transforms[j].getOutput() == "" ||
                outputs[i].identifier == m_transforms[j].getOutput().toStdString()) {
                m_outputNos.push_back(i);
                m_descriptors.push_back(new Vamp::Plugin::OutputDescriptor(outputs[i]));
                m_fixedRateFeatureNos.push_back(-1); // we increment before use
                break;
            }
        }

        if (m_descriptors.size() <= j) {
            m_message = tr("Plugin \"%1\" has no output named \"%2\"")
                .arg(pluginId)
                .arg(m_transforms[j].getOutput());
            return false;
        }
    }

    for (int j = 0; j < (int)m_transforms.size(); ++j) {
        createOutputModel(j);
    }

    return true;
}

void
FeatureExtractionModelTransformer::createOutputModel(int n)
{
    DenseTimeValueModel *input = getConformingInput();

//    cerr << "FeatureExtractionModelTransformer::createOutputModel: sample type " << m_descriptor->sampleType << ", rate " << m_descriptor->sampleRate << endl;
    
    PluginRDFDescription description(m_transforms[n].getPluginIdentifier());
    QString outputId = m_transforms[n].getOutput();

    int binCount = 1;
    float minValue = 0.0, maxValue = 0.0;
    bool haveExtents = false;
    
    if (m_descriptors[n]->hasFixedBinCount) {
	binCount = m_descriptors[n]->binCount;
    }

//    cerr << "FeatureExtractionModelTransformer: output bin count "
//	      << binCount << endl;

    if (binCount > 0 && m_descriptors[n]->hasKnownExtents) {
	minValue = m_descriptors[n]->minValue;
	maxValue = m_descriptors[n]->maxValue;
        haveExtents = true;
    }

    size_t modelRate = input->getSampleRate();
    size_t modelResolution = 1;

    if (m_descriptors[n]->sampleType != 
        Vamp::Plugin::OutputDescriptor::OneSamplePerStep) {
        if (m_descriptors[n]->sampleRate > input->getSampleRate()) {
            cerr << "WARNING: plugin reports output sample rate as "
                      << m_descriptors[n]->sampleRate << " (can't display features with finer resolution than the input rate of " << input->getSampleRate() << ")" << endl;
        }
    }

    switch (m_descriptors[n]->sampleType) {

    case Vamp::Plugin::OutputDescriptor::VariableSampleRate:
	if (m_descriptors[n]->sampleRate != 0.0) {
	    modelResolution = size_t(modelRate / m_descriptors[n]->sampleRate + 0.001);
	}
	break;

    case Vamp::Plugin::OutputDescriptor::OneSamplePerStep:
	modelResolution = m_transforms[n].getStepSize();
	break;

    case Vamp::Plugin::OutputDescriptor::FixedSampleRate:
        //!!! SV doesn't actually support display of models that have
        //!!! different underlying rates together -- so we always set
        //!!! the model rate to be the input model's rate, and adjust
        //!!! the resolution appropriately.  We can't properly display
        //!!! data with a higher resolution than the base model at all
//	modelRate = size_t(m_descriptors[n]->sampleRate + 0.001);
        if (m_descriptors[n]->sampleRate > input->getSampleRate()) {
            modelResolution = 1;
        } else {
            modelResolution = size_t(input->getSampleRate() /
                                     m_descriptors[n]->sampleRate);
        }
	break;
    }

    bool preDurationPlugin = (m_plugin->getVampApiVersion() < 2);

    Model *out = 0;

    if (binCount == 0 &&
        (preDurationPlugin || !m_descriptors[n]->hasDuration)) {

        // Anything with no value and no duration is an instant

        out = new SparseOneDimensionalModel(modelRate, modelResolution, false);
        QString outputEventTypeURI = description.getOutputEventTypeURI(outputId);
        out->setRDFTypeURI(outputEventTypeURI);

    } else if ((preDurationPlugin && binCount > 1 &&
                (m_descriptors[n]->sampleType ==
                 Vamp::Plugin::OutputDescriptor::VariableSampleRate)) ||
               (!preDurationPlugin && m_descriptors[n]->hasDuration)) {

        // For plugins using the old v1 API without explicit duration,
        // we treat anything that has multiple bins (i.e. that has the
        // potential to have value and duration) and a variable sample
        // rate as a note model, taking its values as pitch, duration
        // and velocity (if present) respectively.  This is the same
        // behaviour as always applied by SV to these plugins in the
        // past.

        // For plugins with the newer API, we treat anything with
        // duration as either a note model with pitch and velocity, or
        // a region model.

        // How do we know whether it's an interval or note model?
        // What's the essential difference?  Is a note model any
        // interval model using a Hz or "MIDI pitch" scale?  There
        // isn't really a reliable test for "MIDI pitch"...  Does a
        // note model always have velocity?  This is a good question
        // to be addressed by accompanying RDF, but for the moment we
        // will do the following...

        bool isNoteModel = false;
        
        // Regions have only value (and duration -- we can't extract a
        // region model from an old-style plugin that doesn't support
        // duration)
        if (binCount > 1) isNoteModel = true;

        // Regions do not have units of Hz or MIDI things (a sweeping
        // assumption!)
        if (m_descriptors[n]->unit == "Hz" ||
            m_descriptors[n]->unit.find("MIDI") != std::string::npos ||
            m_descriptors[n]->unit.find("midi") != std::string::npos) {
            isNoteModel = true;
        }

        // If we had a "sparse 3D model", we would have the additional
        // problem of determining whether to use that here (if bin
        // count > 1).  But we don't.

        QSettings settings;
        settings.beginGroup("Transformer");
        bool flexi = settings.value("use-flexi-note-model", false).toBool();
        settings.endGroup();

        cerr << "flexi = " << flexi << endl;

        if (isNoteModel && !flexi) {

            NoteModel *model;
            if (haveExtents) {
                model = new NoteModel
                    (modelRate, modelResolution, minValue, maxValue, false);
            } else {
                model = new NoteModel
                    (modelRate, modelResolution, false);
            }
            model->setScaleUnits(m_descriptors[n]->unit.c_str());
            out = model;

        } else if (isNoteModel && flexi) {

            FlexiNoteModel *model;
            if (haveExtents) {
                model = new FlexiNoteModel
                    (modelRate, modelResolution, minValue, maxValue, false);
            } else {
                model = new FlexiNoteModel
                    (modelRate, modelResolution, false);
            }
            model->setScaleUnits(m_descriptors[n]->unit.c_str());
            out = model;

        } else {

            RegionModel *model;
            if (haveExtents) {
                model = new RegionModel
                    (modelRate, modelResolution, minValue, maxValue, false);
            } else {
                model = new RegionModel
                    (modelRate, modelResolution, false);
            }
            model->setScaleUnits(m_descriptors[n]->unit.c_str());
            out = model;
        }

        QString outputEventTypeURI = description.getOutputEventTypeURI(outputId);
        out->setRDFTypeURI(outputEventTypeURI);

    } else if (binCount == 1 ||
               (m_descriptors[n]->sampleType == 
                Vamp::Plugin::OutputDescriptor::VariableSampleRate)) {

        // Anything that is not a 1D, note, or interval model and that
        // has only one value per result must be a sparse time value
        // model.

        // Anything that is not a 1D, note, or interval model and that
        // has a variable sample rate is also treated as a sparse time
        // value model regardless of its bin count, because we lack a
        // sparse 3D model.

        SparseTimeValueModel *model;
        if (haveExtents) {
            model = new SparseTimeValueModel
                (modelRate, modelResolution, minValue, maxValue, false);
        } else {
            model = new SparseTimeValueModel
                (modelRate, modelResolution, false);
        }

        Vamp::Plugin::OutputList outputs = m_plugin->getOutputDescriptors();
        model->setScaleUnits(outputs[m_outputNos[n]].unit.c_str());

        out = model;

        QString outputEventTypeURI = description.getOutputEventTypeURI(outputId);
        out->setRDFTypeURI(outputEventTypeURI);

    } else {

        // Anything that is not a 1D, note, or interval model and that
        // has a fixed sample rate and more than one value per result
        // must be a dense 3D model.

        EditableDenseThreeDimensionalModel *model =
            new EditableDenseThreeDimensionalModel
            (modelRate, modelResolution, binCount,
             EditableDenseThreeDimensionalModel::BasicMultirateCompression,
             false);

	if (!m_descriptors[n]->binNames.empty()) {
	    std::vector<QString> names;
	    for (size_t i = 0; i < m_descriptors[n]->binNames.size(); ++i) {
		names.push_back(m_descriptors[n]->binNames[i].c_str());
	    }
	    model->setBinNames(names);
	}
        
        out = model;

        QString outputSignalTypeURI = description.getOutputSignalTypeURI(outputId);
        out->setRDFTypeURI(outputSignalTypeURI);
    }

    if (out) {
        out->setSourceModel(input);
        m_outputs.push_back(out);
    }
}

FeatureExtractionModelTransformer::~FeatureExtractionModelTransformer()
{
//    SVDEBUG << "FeatureExtractionModelTransformer::~FeatureExtractionModelTransformer()" << endl;
    delete m_plugin;
    for (int j = 0; j < m_descriptors.size(); ++j) {
        delete m_descriptors[j];
    }
}

DenseTimeValueModel *
FeatureExtractionModelTransformer::getConformingInput()
{
//    SVDEBUG << "FeatureExtractionModelTransformer::getConformingInput: input model is " << getInputModel() << endl;

    DenseTimeValueModel *dtvm =
	dynamic_cast<DenseTimeValueModel *>(getInputModel());
    if (!dtvm) {
	SVDEBUG << "FeatureExtractionModelTransformer::getConformingInput: WARNING: Input model is not conformable to DenseTimeValueModel" << endl;
    }
    return dtvm;
}

void
FeatureExtractionModelTransformer::run()
{
    DenseTimeValueModel *input = getConformingInput();
    if (!input) return;

    if (m_outputs.empty()) return;

    Transform primaryTransform = m_transforms[0];

    while (!input->isReady() && !m_abandoned) {
        SVDEBUG << "FeatureExtractionModelTransformer::run: Waiting for input model to be ready..." << endl;
        usleep(500000);
    }
    if (m_abandoned) return;

    size_t sampleRate = input->getSampleRate();

    size_t channelCount = input->getChannelCount();
    if (m_plugin->getMaxChannelCount() < channelCount) {
	channelCount = 1;
    }

    float **buffers = new float*[channelCount];
    for (size_t ch = 0; ch < channelCount; ++ch) {
	buffers[ch] = new float[primaryTransform.getBlockSize() + 2];
    }

    size_t stepSize = primaryTransform.getStepSize();
    size_t blockSize = primaryTransform.getBlockSize();

    bool frequencyDomain = (m_plugin->getInputDomain() ==
                            Vamp::Plugin::FrequencyDomain);
    std::vector<FFTModel *> fftModels;

    if (frequencyDomain) {
        for (size_t ch = 0; ch < channelCount; ++ch) {
            FFTModel *model = new FFTModel
                                  (getConformingInput(),
                                   channelCount == 1 ? m_input.getChannel() : ch,
                                   primaryTransform.getWindowType(),
                                   blockSize,
                                   stepSize,
                                   blockSize,
                                   false,
                                   StorageAdviser::PrecisionCritical);
            if (!model->isOK()) {
                delete model;
                for (int j = 0; j < (int)m_outputNos.size(); ++j) {
                    setCompletion(j, 100);
                }
                //!!! need a better way to handle this -- previously we were using a QMessageBox but that isn't an appropriate thing to do here either
                throw AllocationFailed("Failed to create the FFT model for this feature extraction model transformer");
            }
            model->resume();
            fftModels.push_back(model);
        }
    }

    long startFrame = m_input.getModel()->getStartFrame();
    long   endFrame = m_input.getModel()->getEndFrame();

    RealTime contextStartRT = primaryTransform.getStartTime();
    RealTime contextDurationRT = primaryTransform.getDuration();

    long contextStart =
        RealTime::realTime2Frame(contextStartRT, sampleRate);

    long contextDuration =
        RealTime::realTime2Frame(contextDurationRT, sampleRate);

    if (contextStart == 0 || contextStart < startFrame) {
        contextStart = startFrame;
    }

    if (contextDuration == 0) {
        contextDuration = endFrame - contextStart;
    }
    if (contextStart + contextDuration > endFrame) {
        contextDuration = endFrame - contextStart;
    }

    long blockFrame = contextStart;

    long prevCompletion = 0;

    for (int j = 0; j < (int)m_outputNos.size(); ++j) {
        setCompletion(j, 0);
    }

    float *reals = 0;
    float *imaginaries = 0;
    if (frequencyDomain) {
        reals = new float[blockSize/2 + 1];
        imaginaries = new float[blockSize/2 + 1];
    }

    QString error = "";

    while (!m_abandoned) {

        if (frequencyDomain) {
            if (blockFrame - int(blockSize)/2 >
                contextStart + contextDuration) break;
        } else {
            if (blockFrame >= 
                contextStart + contextDuration) break;
        }

//	SVDEBUG << "FeatureExtractionModelTransformer::run: blockFrame "
//		  << blockFrame << ", endFrame " << endFrame << ", blockSize "
//                  << blockSize << endl;

	long completion =
	    (((blockFrame - contextStart) / stepSize) * 99) /
	    (contextDuration / stepSize + 1);

	// channelCount is either m_input.getModel()->channelCount or 1

        if (frequencyDomain) {
            for (size_t ch = 0; ch < channelCount; ++ch) {
                int column = (blockFrame - startFrame) / stepSize;
                fftModels[ch]->getValuesAt(column, reals, imaginaries);
                for (size_t i = 0; i <= blockSize/2; ++i) {
                    buffers[ch][i*2] = reals[i];
                    buffers[ch][i*2+1] = imaginaries[i];
                }
                error = fftModels[ch]->getError();
                if (error != "") {
                    cerr << "FeatureExtractionModelTransformer::run: Abandoning, error is " << error << endl;
                    m_abandoned = true;
                    m_message = error;
                }
            }
        } else {
            getFrames(channelCount, blockFrame, blockSize, buffers);
        }

        if (m_abandoned) break;

	Vamp::Plugin::FeatureSet features = m_plugin->process
	    (buffers, Vamp::RealTime::frame2RealTime(blockFrame, sampleRate));

        if (m_abandoned) break;

        for (int j = 0; j < (int)m_outputNos.size(); ++j) {
            for (size_t fi = 0; fi < features[m_outputNos[j]].size(); ++fi) {
                Vamp::Plugin::Feature feature = features[m_outputNos[j]][fi];
                addFeature(j, blockFrame, feature);
            }
        }

	if (blockFrame == contextStart || completion > prevCompletion) {
            for (int j = 0; j < (int)m_outputNos.size(); ++j) {
                setCompletion(j, completion);
            }
	    prevCompletion = completion;
	}

	blockFrame += stepSize;
    }

    if (!m_abandoned) {
        Vamp::Plugin::FeatureSet features = m_plugin->getRemainingFeatures();

        for (int j = 0; j < (int)m_outputNos.size(); ++j) {
            for (size_t fi = 0; fi < features[m_outputNos[j]].size(); ++fi) {
                Vamp::Plugin::Feature feature = features[m_outputNos[j]][fi];
                addFeature(j, blockFrame, feature);
            }
        }
    }

    for (int j = 0; j < (int)m_outputNos.size(); ++j) {
        setCompletion(j, 100);
    }

    if (frequencyDomain) {
        for (size_t ch = 0; ch < channelCount; ++ch) {
            delete fftModels[ch];
        }
        delete[] reals;
        delete[] imaginaries;
    }
}

void
FeatureExtractionModelTransformer::getFrames(int channelCount,
                                             long startFrame, long size,
                                             float **buffers)
{
    long offset = 0;

    if (startFrame < 0) {
        for (int c = 0; c < channelCount; ++c) {
            for (int i = 0; i < size && startFrame + i < 0; ++i) {
                buffers[c][i] = 0.0f;
            }
        }
        offset = -startFrame;
        size -= offset;
        if (size <= 0) return;
        startFrame = 0;
    }

    DenseTimeValueModel *input = getConformingInput();
    if (!input) return;
    
    long got = 0;

    if (channelCount == 1) {

        got = input->getData(m_input.getChannel(), startFrame, size,
                             buffers[0] + offset);

        if (m_input.getChannel() == -1 && input->getChannelCount() > 1) {
            // use mean instead of sum, as plugin input
            float cc = float(input->getChannelCount());
            for (long i = 0; i < size; ++i) {
                buffers[0][i + offset] /= cc;
            }
        }

    } else {

        float **writebuf = buffers;
        if (offset > 0) {
            writebuf = new float *[channelCount];
            for (int i = 0; i < channelCount; ++i) {
                writebuf[i] = buffers[i] + offset;
            }
        }

        got = input->getData(0, channelCount-1, startFrame, size, writebuf);

        if (writebuf != buffers) delete[] writebuf;
    }

    while (got < size) {
        for (int c = 0; c < channelCount; ++c) {
            buffers[c][got + offset] = 0.0;
        }
        ++got;
    }
}

void
FeatureExtractionModelTransformer::addFeature(int n,
                                              size_t blockFrame,
                                              const Vamp::Plugin::Feature &feature)
{
    size_t inputRate = m_input.getModel()->getSampleRate();

//    cerr << "FeatureExtractionModelTransformer::addFeature: blockFrame = "
//              << blockFrame << ", hasTimestamp = " << feature.hasTimestamp
//              << ", timestamp = " << feature.timestamp << ", hasDuration = "
//              << feature.hasDuration << ", duration = " << feature.duration
//              << endl;

    int binCount = 1;
    if (m_descriptors[n]->hasFixedBinCount) {
	binCount = m_descriptors[n]->binCount;
    }

    size_t frame = blockFrame;

    if (m_descriptors[n]->sampleType ==
	Vamp::Plugin::OutputDescriptor::VariableSampleRate) {

	if (!feature.hasTimestamp) {
	    cerr
		<< "WARNING: FeatureExtractionModelTransformer::addFeature: "
		<< "Feature has variable sample rate but no timestamp!"
		<< endl;
	    return;
	} else {
	    frame = Vamp::RealTime::realTime2Frame(feature.timestamp, inputRate);
	}

    } else if (m_descriptors[n]->sampleType ==
	       Vamp::Plugin::OutputDescriptor::FixedSampleRate) {

        if (!feature.hasTimestamp) {
            ++m_fixedRateFeatureNos[n];
        } else {
            RealTime ts(feature.timestamp.sec, feature.timestamp.nsec);
            m_fixedRateFeatureNos[n] =
                lrint(ts.toDouble() * m_descriptors[n]->sampleRate);
        }
 
        frame = lrintf((m_fixedRateFeatureNos[n] / m_descriptors[n]->sampleRate)
                       * inputRate);
    }
	
    // Rather than repeat the complicated tests from the constructor
    // to determine what sort of model we must be adding the features
    // to, we instead test what sort of model the constructor decided
    // to create.

    if (isOutput<SparseOneDimensionalModel>(n)) {

        SparseOneDimensionalModel *model =
            getConformingOutput<SparseOneDimensionalModel>(n);
	if (!model) return;

        model->addPoint(SparseOneDimensionalModel::Point
                       (frame, feature.label.c_str()));
	
    } else if (isOutput<SparseTimeValueModel>(n)) {

	SparseTimeValueModel *model =
            getConformingOutput<SparseTimeValueModel>(n);
	if (!model) return;

        for (int i = 0; i < feature.values.size(); ++i) {

            float value = feature.values[i];

            QString label = feature.label.c_str();
            if (feature.values.size() > 1) {
                label = QString("[%1] %2").arg(i+1).arg(label);
            }

            model->addPoint(SparseTimeValueModel::Point(frame, value, label));
        }

    } else if (isOutput<FlexiNoteModel>(n) || isOutput<NoteModel>(n) || isOutput<RegionModel>(n)) { //GF: Added Note Model

        int index = 0;

        float value = 0.0;
        if (feature.values.size() > index) {
            value = feature.values[index++];
        }

        float duration = 1;
        if (feature.hasDuration) {
            duration = Vamp::RealTime::realTime2Frame(feature.duration, inputRate);
        } else {
            if (feature.values.size() > index) {
                duration = feature.values[index++];
            }
        }

		if (isOutput<FlexiNoteModel>(n)) { // GF: added for flexi note model

            float velocity = 100;
            if (feature.values.size() > index) {
                velocity = feature.values[index++];
            }
            if (velocity < 0) velocity = 127;
            if (velocity > 127) velocity = 127;

            FlexiNoteModel *model = getConformingOutput<FlexiNoteModel>(n);
            if (!model) return;
            model->addPoint(FlexiNoteModel::Point(frame, value, // value is pitch
                                             lrintf(duration),
                                             velocity / 127.f,
                                             feature.label.c_str()));
			// GF: end -- added for flexi note model
        } else  if (isOutput<NoteModel>(n)) {

            float velocity = 100;
            if (feature.values.size() > index) {
                velocity = feature.values[index++];
            }
            if (velocity < 0) velocity = 127;
            if (velocity > 127) velocity = 127;

            NoteModel *model = getConformingOutput<NoteModel>(n);
            if (!model) return;
            model->addPoint(NoteModel::Point(frame, value, // value is pitch
                                             lrintf(duration),
                                             velocity / 127.f,
                                             feature.label.c_str()));
        } else {

            RegionModel *model = getConformingOutput<RegionModel>(n);
            if (!model) return;

            if (feature.hasDuration && !feature.values.empty()) {

                for (int i = 0; i < feature.values.size(); ++i) {

                    float value = feature.values[i];

                    QString label = feature.label.c_str();
                    if (feature.values.size() > 1) {
                        label = QString("[%1] %2").arg(i+1).arg(label);
                    }

                    model->addPoint(RegionModel::Point(frame, value,
                                                       lrintf(duration),
                                                       label));
                }
            } else {
            
                model->addPoint(RegionModel::Point(frame, value,
                                                   lrintf(duration),
                                                   feature.label.c_str()));
            }
        }
	
    } else if (isOutput<EditableDenseThreeDimensionalModel>(n)) {
	
	DenseThreeDimensionalModel::Column values =
            DenseThreeDimensionalModel::Column::fromStdVector(feature.values);
	
	EditableDenseThreeDimensionalModel *model =
            getConformingOutput<EditableDenseThreeDimensionalModel>(n);
	if (!model) return;

	model->setColumn(frame / model->getResolution(), values);

    } else {
        SVDEBUG << "FeatureExtractionModelTransformer::addFeature: Unknown output model type!" << endl;
    }
}

void
FeatureExtractionModelTransformer::setCompletion(int n, int completion)
{
    int binCount = 1;
    if (m_descriptors[n]->hasFixedBinCount) {
	binCount = m_descriptors[n]->binCount;
    }

//    SVDEBUG << "FeatureExtractionModelTransformer::setCompletion("
//              << completion << ")" << endl;

    if (isOutput<SparseOneDimensionalModel>(n)) {

	SparseOneDimensionalModel *model =
            getConformingOutput<SparseOneDimensionalModel>(n);
	if (!model) return;
	model->setCompletion(completion, true);

    } else if (isOutput<SparseTimeValueModel>(n)) {

	SparseTimeValueModel *model =
            getConformingOutput<SparseTimeValueModel>(n);
	if (!model) return;
	model->setCompletion(completion, true);

    } else if (isOutput<NoteModel>(n)) {

	NoteModel *model = getConformingOutput<NoteModel>(n);
	if (!model) return;
	model->setCompletion(completion, true);
	
	} else if (isOutput<FlexiNoteModel>(n)) {

	FlexiNoteModel *model = getConformingOutput<FlexiNoteModel>(n);
	if (!model) return;
	model->setCompletion(completion, true);

    } else if (isOutput<RegionModel>(n)) {

	RegionModel *model = getConformingOutput<RegionModel>(n);
	if (!model) return;
	model->setCompletion(completion, true);

    } else if (isOutput<EditableDenseThreeDimensionalModel>(n)) {

	EditableDenseThreeDimensionalModel *model =
            getConformingOutput<EditableDenseThreeDimensionalModel>(n);
	if (!model) return;
	model->setCompletion(completion, true); //!!!m_context.updates);
    }
}

