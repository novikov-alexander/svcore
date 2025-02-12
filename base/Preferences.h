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

#ifndef SV_PREFERENCES_H
#define SV_PREFERENCES_H

#include "PropertyContainer.h"

#include "Window.h"

class Preferences : public PropertyContainer
{
    Q_OBJECT

public:
    static Preferences *getInstance();

    PropertyList getProperties() const override;
    QString getPropertyLabel(const PropertyName &) const override;
    PropertyType getPropertyType(const PropertyName &) const override;
    int getPropertyRangeAndValue(const PropertyName &, int *, int *, int *) const override;
    QString getPropertyValueLabel(const PropertyName &, int value) const override;
    QString getPropertyContainerName() const override;
    QString getPropertyContainerIconName() const override;

    enum SpectrogramSmoothing {
        NoSpectrogramSmoothing,
        SpectrogramInterpolated
    };

    enum SpectrogramXSmoothing {
        NoSpectrogramXSmoothing,
        SpectrogramXInterpolated
    };

    SpectrogramSmoothing getSpectrogramSmoothing() const { return m_spectrogramSmoothing; }
    SpectrogramXSmoothing getSpectrogramXSmoothing() const { return m_spectrogramXSmoothing; }
    double getTuningFrequency() const { return m_tuningFrequency; }
    WindowType getWindowType() const { return m_windowType; }

    bool getRunPluginsInProcess() const { return m_runPluginsInProcess; }
    
    //!!! harmonise with PaneStack
    enum PropertyBoxLayout {
        VerticallyStacked,
        Layered
    };
    PropertyBoxLayout getPropertyBoxLayout() const { return m_propertyBoxLayout; }

    int getViewFontSize() const { return m_viewFontSize; }

    bool getOmitTempsFromRecentFiles() const { return m_omitRecentTemps; }

    QString getTemporaryDirectoryRoot() const { return m_tempDirRoot; }

    /// True if we should always mix down recorded audio to a single
    /// channel regardless of how many channels the device opens
    bool getRecordMono() const { return m_recordMono; }
    
    /// If we should always resample audio to the same rate, return it; otherwise (the normal case) return 0
    sv_samplerate_t getFixedSampleRate() const { return m_fixedSampleRate; }

    /// True if we should resample second or subsequent audio file to match first audio file's rate
    bool getResampleOnLoad() const { return m_resampleOnLoad; }

    /// True if mp3 files should be loaded "gaplessly", i.e. compensating for encoder/decoder delay and padding
    bool getUseGaplessMode() const { return m_gapless; }
    
    /// True if audio files should be loaded with normalisation (max == 1)
    bool getNormaliseAudio() const { return m_normaliseAudio; }

    /// True if we should use higher-quality time stretcher where available
    bool getFinerTimeStretch() const { return m_finerTimeStretch; }
    
    enum BackgroundMode {
        BackgroundFromTheme,
        DarkBackground,
        LightBackground 
    };
    BackgroundMode getBackgroundMode() const { return m_backgroundMode; }

    enum TimeToTextMode {
        TimeToTextMs,
        TimeToTextUs,
        TimeToText24Frame,
        TimeToText25Frame,
        TimeToText30Frame,
        TimeToText50Frame,
        TimeToText60Frame
    };
    TimeToTextMode getTimeToTextMode() const { return m_timeToTextMode; }

    bool getShowHMS() const { return m_showHMS; }
    
    int getOctaveOfMiddleC() const {
        // weed out unsupported octaves
        return getOctaveOfMiddleCInSystem(getSystemWithMiddleCInOctave(m_octave));
    }
    int getOctaveOfLowestMIDINote() const {
        return getOctaveOfMiddleC() - 5;
    }
    
    bool getShowSplash() const { return m_showSplash; }

public slots:
    void setProperty(const PropertyName &, int) override;

    void setSpectrogramSmoothing(SpectrogramSmoothing smoothing);
    void setSpectrogramXSmoothing(SpectrogramXSmoothing smoothing);
    void setTuningFrequency(double freq);
    void setPropertyBoxLayout(PropertyBoxLayout layout);
    void setWindowType(WindowType type);
    void setRunPluginsInProcess(bool r);
    void setOmitTempsFromRecentFiles(bool omit);
    void setTemporaryDirectoryRoot(QString tempDirRoot);
    void setFixedSampleRate(sv_samplerate_t);
    void setRecordMono(bool);
    void setResampleOnLoad(bool);
    void setUseGaplessMode(bool);
    void setNormaliseAudio(bool);
    void setFinerTimeStretch(bool);
    void setBackgroundMode(BackgroundMode mode);
    void setTimeToTextMode(TimeToTextMode mode);
    void setTimeToTextModeUnsaved(TimeToTextMode mode);
    void setShowHMS(bool show);
    void setOctaveOfMiddleC(int oct);
    void setViewFontSize(int size);
    void setShowSplash(bool);

private:
    Preferences(); // may throw DirectoryCreationFailed
    virtual ~Preferences();

    static Preferences *m_instance;

    // We don't support arbitrary octaves in the gui, because we want
    // to be able to label what the octave system comes from. These
    // are the ones we support. (But we save and load as octave
    // numbers, so as not to make the prefs format really confusing)
    enum OctaveNumberingSystem {
        C0_Centre,
        C3_Logic,
        C4_ASA,
        C5_Sonar
    };
    static int getOctaveOfMiddleCInSystem(OctaveNumberingSystem s);
    static OctaveNumberingSystem getSystemWithMiddleCInOctave(int o);

    SpectrogramSmoothing m_spectrogramSmoothing;
    SpectrogramXSmoothing m_spectrogramXSmoothing;
    double m_tuningFrequency;
    PropertyBoxLayout m_propertyBoxLayout;
    WindowType m_windowType;
    bool m_runPluginsInProcess;
    bool m_omitRecentTemps;
    QString m_tempDirRoot;
    sv_samplerate_t m_fixedSampleRate;
    bool m_recordMono;
    bool m_resampleOnLoad;
    bool m_gapless;
    bool m_normaliseAudio;
    bool m_finerTimeStretch;
    int m_viewFontSize;
    BackgroundMode m_backgroundMode;
    TimeToTextMode m_timeToTextMode;
    bool m_showHMS;
    int m_octave;
    bool m_showSplash;
};

#endif
