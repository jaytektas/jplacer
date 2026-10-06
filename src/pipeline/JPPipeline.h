// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#pragma once

#include "JPPipelineModel.h"
#include "JPPipelineStage.h"
#include "JPPipelineValue.h"

#include "openpnp/JPXmlElement.h"
#include "openpnp/JPXmlNode.h"

#include <opencv2/core.hpp>

#include <functional>
#include <map>
#include <stdexcept>
#include <string>
#include <vector>

inline namespace jf {

// A vision pipeline (OpenPnP's CvPipeline): its stages in order, run one
// after another over a working image (each may change it, or leave it), each
// stage's result kept by its name (its image, its colour space, what it
// found, how long it took), the last thing found the working model. A stage
// that fails gives its failure as its result and the pipeline goes on; one
// that cannot (no camera to capture with) stops it. What the caller sets on
// the pipeline (OpenPnP's pipeline properties) can stand in for a stage's
// own settings; the stage then says so (overrides, for the editor).
class JPPipeline {
public:
    // What the pipeline runs with: the camera, and what the job knows.
    struct Context {
        // A picture from the camera (BGR), after it settled as `settle` says
        // (OpenPnP's SettleOption: "Skip", "Settle", "SettleFullArea"; empty:
        // as the camera's own settings say); its light switched as its
        // settings say, or to `light` (an actuator value) when given.
        std::function<bool(const std::string& settle, const std::string& light, cv::Mat& bgr, std::string& why)> capture;
        // The size of the camera's pictures (0: no camera).
        int cameraWidth = 0, cameraHeight = 0;
        // The camera's scale (pixels a millimetre; 0: not known), and a machine
        // place's pixel in its picture.
        double pixelsPerMmX = 0, pixelsPerMmY = 0;
        std::function<bool(double xMm, double yMm, double& px, double& py)> locationToPixel;
        // How the picture shows the machine (JPCameraCalibration::pictureMirrored, pictureTurnDeg): OpenPnP's are
        // turned to show it as from above, so what OpenPnP tells a pipeline in the machine's terms (an angle, a
        // footprint, a side) is told in the picture's.
        bool   pictureMirrored = false;
        double pictureTurnDeg = 0;
        // A machine angle (right-handed) as the picture shows it.
        double pictureAngle(double machineAngle) const {
            return pictureMirrored ? pictureTurnDeg - machineAngle : pictureTurnDeg + machineAngle;
        }
        // Where ImageWriteDebug writes (empty: it does not).
        std::string debugDirectory;
        // The machine's actuator by name, on a head or the machine (null: none);
        // one set to a value ("true", "1.5", a text) and waited for, or why not.
        std::function<bool(const std::string& name)> actuatorExists;
        std::function<bool(const std::string& name, const std::string& value, std::string& why)> actuate;
        // The configuration's directory: part templates live in its "templates".
        std::string configurationDirectory;
    };
    // A stage's failure that stops the pipeline (OpenPnP's TerminalException).
    struct Terminal : std::runtime_error {
        using std::runtime_error::runtime_error;
    };
    struct Result {
        cv::Mat         image;
        std::string     colorSpace;   // FluentCv.ColorSpace's name ("Bgr", "Gray", "HsvFull"…)
        JPPipelineModel model;
        double          milliseconds = 0;
    };

    JPPipeline() = default;
    // From OpenPnP's <cv-pipeline><stages>…</stages></cv-pipeline>.
    static JPPipeline fromXml(const JPXmlElement& cvPipeline);
    JPXmlNode toXml() const;

    std::vector<JPPipelineStage>&       stages() { return m_stages; }
    const std::vector<JPPipelineStage>& stages() const { return m_stages; }
    JPPipelineStage*                    stage(const std::string& name);
    // A name no stage has ("0", "1"…), as OpenPnP's generateUniqueName.
    std::string                         uniqueName() const;

    // Run every stage in order. False, and why, when a stage stopped it.
    bool process(std::string& why);
    const Result* result(const std::string& stageName) const;
    // A stage's result that must be there (OpenPnP's getExpectedResult): throws, saying why, when not.
    const Result& expectedResult(const std::string& stageName) const;
    double        totalMilliseconds() const { return m_totalMs; }

    // For stages: the working image (a picture made up when there is none), its colour space, the model.
    cv::Mat&               workingImage();
    const std::string&     workingColorSpace() const { return m_colorSpace; }
    void                   setWorkingColorSpace(const std::string& c) { m_colorSpace = c; }
    const JPPipelineModel& workingModel() const { return m_model; }

    void                   setProperty(const std::string& name, JPPipelineValue value) { m_properties[name] = std::move(value); }
    // A property no longer set (OpenPnP's setProperty(name, null)): the stages' own settings again.
    void                   removeProperty(const std::string& name) { m_properties.erase(name); }
    const JPPipelineValue* property(const std::string& name) const;
    void                   clearProperties() { m_properties.clear(); }
    Context&               context() { return m_context; }
    const Context&         context() const { return m_context; }

    // A stage's setting, or what the caller set in its place under
    // `pipelineProperty` (a length or area in pixels through the camera):
    // OpenPnP's getPossiblePipelinePropertyOverride. The override is noted.
    double     overridden(const JPPipelineStage& stage, const std::string& attribute, double value,
                          const std::string& pipelineProperty);
    // The same for a whole-number setting (OpenPnP's Integer or Long): a number given rounded as Java's Math.round.
    long        overriddenInteger(const JPPipelineStage& stage, const std::string& attribute, long value,
                                  const std::string& pipelineProperty);
    cv::Point2d overriddenPoint(const JPPipelineStage& stage, const std::string& attribute, cv::Point2d value,
                                const std::string& pipelineProperty);
    bool        overriddenFlag(const JPPipelineStage& stage, const std::string& attribute, bool value,
                               const std::string& pipelineProperty);
    std::string overriddenText(const JPPipelineStage& stage, const std::string& attribute, const std::string& value,
                               const std::string& pipelineProperty);
    // A stage's setting noted as set by the caller (a parameter stage's).
    void noteOverride(const std::string& stageName, const std::string& attribute, const std::string& value) {
        m_overrides[stageName][attribute] = value;
    }
    // Every parameter stage's controlled setting back to its default (as
    // OpenPnP does before writing a pipeline), so it is saved unchanged.
    void resetToDefaults();
    // What the caller controlled of a stage last run (attribute: value), for the editor.
    std::map<std::string, std::string> overrides(const std::string& stageName) const;

private:
    std::vector<JPPipelineStage>                                   m_stages;
    std::map<std::string, Result>                                  m_results;
    std::map<std::string, JPPipelineValue>                         m_properties;
    std::map<std::string, std::map<std::string, std::string>>      m_overrides;
    Context                                                        m_context;
    cv::Mat                                                        m_working;
    std::string                                                    m_colorSpace;
    JPPipelineModel                                                m_model;
    double                                                         m_totalMs = 0;
};

} // inline namespace jf
