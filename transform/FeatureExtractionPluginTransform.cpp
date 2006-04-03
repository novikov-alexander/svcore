
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

#include "FeatureExtractionPluginTransform.h"

#include "plugin/FeatureExtractionPluginFactory.h"
#include "plugin/PluginXml.h"
#include "vamp-sdk/Plugin.h"

#include "base/Model.h"
#include "base/Window.h"
#include "model/SparseOneDimensionalModel.h"
#include "model/SparseTimeValueModel.h"
#include "model/DenseThreeDimensionalModel.h"
#include "model/DenseTimeValueModel.h"

#include <fftw3.h>

#include <iostream>

FeatureExtractionPluginTransform::FeatureExtractionPluginTransform(Model *inputModel,
								   QString pluginId,
                                                                   int channel,
                                                                   QString configurationXml,
								   QString outputName) :
    Transform(inputModel),
    m_plugin(0),
    m_channel(channel),
    m_stepSize(0),
    m_blockSize(0),
    m_descriptor(0),
    m_outputFeatureNo(0)
{
    std::cerr << "FeatureExtractionPluginTransform::FeatureExtractionPluginTransform: plugin " << pluginId.toStdString() << ", outputName " << outputName.toStdString() << std::endl;

    FeatureExtractionPluginFactory *factory =
	FeatureExtractionPluginFactory::instanceFor(pluginId);

    if (!factory) {
	std::cerr << "FeatureExtractionPluginTransform: No factory available for plugin id \""
		  << pluginId.toStdString() << "\"" << std::endl;
	return;
    }

    m_plugin = factory->instantiatePlugin(pluginId, m_input->getSampleRate());

    if (!m_plugin) {
	std::cerr << "FeatureExtractionPluginTransform: Failed to instantiate plugin \""
		  << pluginId.toStdString() << "\"" << std::endl;
	return;
    }

    if (configurationXml != "") {
        PluginXml(m_plugin).setParametersFromXml(configurationXml);
    }

    m_blockSize = m_plugin->getPreferredBlockSize();
    m_stepSize = m_plugin->getPreferredStepSize();

    if (m_blockSize == 0) m_blockSize = 1024; //!!! todo: ask user
    if (m_stepSize == 0) m_stepSize = m_blockSize; //!!! likewise

    Vamp::Plugin::OutputList outputs =
	m_plugin->getOutputDescriptors();

    if (outputs.empty()) {
	std::cerr << "FeatureExtractionPluginTransform: Plugin \""
		  << pluginId.toStdString() << "\" has no outputs" << std::endl;
	return;
    }
    
    for (size_t i = 0; i < outputs.size(); ++i) {
	if (outputName == "" || outputs[i].name == outputName.toStdString()) {
	    m_outputFeatureNo = i;
	    m_descriptor = new Vamp::Plugin::OutputDescriptor
		(outputs[i]);
	    break;
	}
    }

    if (!m_descriptor) {
	std::cerr << "FeatureExtractionPluginTransform: Plugin \""
		  << pluginId.toStdString() << "\" has no output named \""
		  << outputName.toStdString() << "\"" << std::endl;
	return;
    }

    std::cerr << "FeatureExtractionPluginTransform: output sample type "
	      << m_descriptor->sampleType << std::endl;

    int valueCount = 1;
    float minValue = 0.0, maxValue = 0.0;
    
    if (m_descriptor->hasFixedValueCount) {
	valueCount = m_descriptor->valueCount;
    }

    if (valueCount > 0 && m_descriptor->hasKnownExtents) {
	minValue = m_descriptor->minValue;
	maxValue = m_descriptor->maxValue;
    }

    size_t modelRate = m_input->getSampleRate();
    size_t modelResolution = 1;
    
    switch (m_descriptor->sampleType) {

    case Vamp::Plugin::OutputDescriptor::VariableSampleRate:
	if (m_descriptor->sampleRate != 0.0) {
	    modelResolution = size_t(modelRate / m_descriptor->sampleRate + 0.001);
	}
	break;

    case Vamp::Plugin::OutputDescriptor::OneSamplePerStep:
	modelResolution = m_plugin->getPreferredStepSize();
	break;

    case Vamp::Plugin::OutputDescriptor::FixedSampleRate:
	modelRate = m_descriptor->sampleRate;
	break;
    }

    if (valueCount == 0) {

	m_output = new SparseOneDimensionalModel(modelRate, modelResolution,
						 false);

    } else if (valueCount == 1 ||

	       // We don't have a sparse 3D model
	       m_descriptor->sampleType ==
	       Vamp::Plugin::OutputDescriptor::VariableSampleRate) {
	
        SparseTimeValueModel *model = new SparseTimeValueModel
            (modelRate, modelResolution, minValue, maxValue, false);
        model->setScaleUnits(outputs[m_outputFeatureNo].unit.c_str());

        m_output = model;

    } else {
	
	m_output = new DenseThreeDimensionalModel(modelRate, modelResolution,
						  valueCount, false);

	if (!m_descriptor->valueNames.empty()) {
	    std::vector<QString> names;
	    for (size_t i = 0; i < m_descriptor->valueNames.size(); ++i) {
		names.push_back(m_descriptor->valueNames[i].c_str());
	    }
	    (dynamic_cast<DenseThreeDimensionalModel *>(m_output))
		->setBinNames(names);
	}
    }
}

FeatureExtractionPluginTransform::~FeatureExtractionPluginTransform()
{
    delete m_plugin;
    delete m_descriptor;
}

DenseTimeValueModel *
FeatureExtractionPluginTransform::getInput()
{
    DenseTimeValueModel *dtvm =
	dynamic_cast<DenseTimeValueModel *>(getInputModel());
    if (!dtvm) {
	std::cerr << "FeatureExtractionPluginTransform::getInput: WARNING: Input model is not conformable to DenseTimeValueModel" << std::endl;
    }
    return dtvm;
}

void
FeatureExtractionPluginTransform::run()
{
    DenseTimeValueModel *input = getInput();
    if (!input) return;

    if (!m_output) return;

    size_t channelCount = input->getChannelCount();
    if (m_plugin->getMaxChannelCount() < channelCount) {
	channelCount = 1;
    }
    if (m_plugin->getMinChannelCount() > channelCount) {
	std::cerr << "FeatureExtractionPluginTransform::run: "
		  << "Can't provide enough channels to plugin (plugin min "
		  << m_plugin->getMinChannelCount() << ", max "
		  << m_plugin->getMaxChannelCount() << ", input model has "
		  << input->getChannelCount() << ")" << std::endl;
	return;
    }

    size_t sampleRate = m_input->getSampleRate();

    if (!m_plugin->initialise(channelCount, m_stepSize, m_blockSize)) {
        std::cerr << "FeatureExtractionPluginTransform::run: Plugin "
                  << m_plugin->getName() << " failed to initialise!" << std::endl;
        return;
    }

    float **buffers = new float*[channelCount];
    for (size_t ch = 0; ch < channelCount; ++ch) {
	buffers[ch] = new float[m_blockSize];
    }

    double *fftInput = 0;
    fftw_complex *fftOutput = 0;
    fftw_plan fftPlan = 0;
    Window<double> windower(HanningWindow, m_blockSize);

    if (m_plugin->getInputDomain() == Vamp::Plugin::FrequencyDomain) {

        fftInput = (double *)fftw_malloc(m_blockSize * sizeof(double));
        fftOutput = (fftw_complex *)fftw_malloc(m_blockSize * sizeof(fftw_complex));
        fftPlan = fftw_plan_dft_r2c_1d(m_blockSize, fftInput, fftOutput,
                                       FFTW_ESTIMATE);
        if (!fftPlan) {
            std::cerr << "ERROR: FeatureExtractionPluginTransform::run(): fftw_plan failed! Results will be garbage" << std::endl;
        }
    }

    size_t startFrame = m_input->getStartFrame();
    size_t   endFrame = m_input->getEndFrame();
    size_t blockFrame = startFrame;

    size_t prevCompletion = 0;

    while (blockFrame < endFrame) {

//	std::cerr << "FeatureExtractionPluginTransform::run: blockFrame "
//		  << blockFrame << std::endl;

	size_t completion =
	    (((blockFrame - startFrame) / m_stepSize) * 99) /
	    (   (endFrame - startFrame) / m_stepSize);

	// channelCount is either m_input->channelCount or 1

	size_t got = 0;

	if (channelCount == 1) {
	    got = input->getValues
		(m_channel, blockFrame, blockFrame + m_blockSize, buffers[0]);
	    while (got < m_blockSize) {
		buffers[0][got++] = 0.0;
	    }
	} else {
	    for (size_t ch = 0; ch < channelCount; ++ch) {
		got = input->getValues
		    (ch, blockFrame, blockFrame + m_blockSize, buffers[ch]);
		while (got < m_blockSize) {
		    buffers[ch][got++] = 0.0;
		}
	    }
	}

        if (fftPlan) {
            for (size_t ch = 0; ch < channelCount; ++ch) {
                for (size_t i = 0; i < m_blockSize; ++i) {
                    fftInput[i] = buffers[ch][i];
                }
                windower.cut(fftInput);
                for (size_t i = 0; i < m_blockSize/2; ++i) {
                    double temp = fftInput[i];
                    fftInput[i] = fftInput[i + m_blockSize/2];
                    fftInput[i + m_blockSize/2] = temp;
                }
                fftw_execute(fftPlan);
                for (size_t i = 0; i < m_blockSize/2; ++i) {
                    buffers[ch][i*2] = fftOutput[i][0];
                    buffers[ch][i*2 + 1] = fftOutput[i][1];
                }
            }
        }

	Vamp::Plugin::FeatureSet features = m_plugin->process
	    (buffers, Vamp::RealTime::frame2RealTime(blockFrame, sampleRate));

	for (size_t fi = 0; fi < features[m_outputFeatureNo].size(); ++fi) {
	    Vamp::Plugin::Feature feature =
		features[m_outputFeatureNo][fi];
	    addFeature(blockFrame, feature);
	}

	if (blockFrame == startFrame || completion > prevCompletion) {
	    setCompletion(completion);
	    prevCompletion = completion;
	}

	blockFrame += m_stepSize;
    }

    if (fftPlan) {
        fftw_destroy_plan(fftPlan);
        fftw_free(fftInput);
        fftw_free(fftOutput);
    }

    Vamp::Plugin::FeatureSet features = m_plugin->getRemainingFeatures();

    for (size_t fi = 0; fi < features[m_outputFeatureNo].size(); ++fi) {
	Vamp::Plugin::Feature feature =
	    features[m_outputFeatureNo][fi];
	addFeature(blockFrame, feature);
    }

    setCompletion(100);
}


void
FeatureExtractionPluginTransform::addFeature(size_t blockFrame,
					     const Vamp::Plugin::Feature &feature)
{
    size_t inputRate = m_input->getSampleRate();

//    std::cerr << "FeatureExtractionPluginTransform::addFeature("
//	      << blockFrame << ")" << std::endl;

    int valueCount = 1;
    if (m_descriptor->hasFixedValueCount) {
	valueCount = m_descriptor->valueCount;
    }

    size_t frame = blockFrame;

    if (m_descriptor->sampleType ==
	Vamp::Plugin::OutputDescriptor::VariableSampleRate) {

	if (!feature.hasTimestamp) {
	    std::cerr
		<< "WARNING: FeatureExtractionPluginTransform::addFeature: "
		<< "Feature has variable sample rate but no timestamp!"
		<< std::endl;
	    return;
	} else {
	    frame = Vamp::RealTime::realTime2Frame(feature.timestamp, inputRate);
	}

    } else if (m_descriptor->sampleType ==
	       Vamp::Plugin::OutputDescriptor::FixedSampleRate) {

	if (feature.hasTimestamp) {
	    //!!! warning: sampleRate may be non-integral
	    frame = Vamp::RealTime::realTime2Frame(feature.timestamp,
                                                   m_descriptor->sampleRate);
	} else {
	    frame = m_output->getEndFrame() + 1;
	}
    }
	
    if (valueCount == 0) {

	SparseOneDimensionalModel *model = getOutput<SparseOneDimensionalModel>();
	if (!model) return;
	model->addPoint(SparseOneDimensionalModel::Point(frame, feature.label.c_str()));
	
    } else if (valueCount == 1 ||
	       m_descriptor->sampleType == 
	       Vamp::Plugin::OutputDescriptor::VariableSampleRate) {

	float value = 0.0;
	if (feature.values.size() > 0) value = feature.values[0];

	SparseTimeValueModel *model = getOutput<SparseTimeValueModel>();
	if (!model) return;
	model->addPoint(SparseTimeValueModel::Point(frame, value, feature.label.c_str()));
	
    } else {
	
	DenseThreeDimensionalModel::BinValueSet values = feature.values;
	
	DenseThreeDimensionalModel *model = getOutput<DenseThreeDimensionalModel>();
	if (!model) return;

	model->setBinValues(frame, values);
    }
}

void
FeatureExtractionPluginTransform::setCompletion(int completion)
{
    int valueCount = 1;
    if (m_descriptor->hasFixedValueCount) {
	valueCount = m_descriptor->valueCount;
    }

    if (valueCount == 0) {

	SparseOneDimensionalModel *model = getOutput<SparseOneDimensionalModel>();
	if (!model) return;
	model->setCompletion(completion);

    } else if (valueCount == 1 ||
	       m_descriptor->sampleType ==
	       Vamp::Plugin::OutputDescriptor::VariableSampleRate) {

	SparseTimeValueModel *model = getOutput<SparseTimeValueModel>();
	if (!model) return;
	model->setCompletion(completion);

    } else {

	DenseThreeDimensionalModel *model = getOutput<DenseThreeDimensionalModel>();
	if (!model) return;
	model->setCompletion(completion);
    }
}

