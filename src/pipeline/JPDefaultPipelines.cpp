// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 Jason Roughley <pis.controller@gmail.com>

#include "JPDefaultPipelines.h"

#include <map>

inline namespace jf {

const std::string& JPDefaultPipelines::stripFeeder() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb0" enabled="false" prefix="strip_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="1" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DetectCircularSymmetry" name="results" enabled="true" min-diameter="10" max-diameter="100" max-distance="100" max-target-count="10" min-symmetry="1.2" corr-symmetry="0.05" property-name="sprocketHole" outer-margin="0.3" inner-margin="0.1" sub-sampling="8" super-sampling="1" symmetry-score="RingMedianVarianceVsRingVarianceSum" diagnostics="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="7" enabled="false" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawCircles" name="8" enabled="false" circles-stage-name="results" thickness="1">
         <color r="255" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb1" enabled="false" prefix="strip_result_" suffix=".png"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::loosePartFeeder() {
    static const std::string xml = R"PIPELINE(<!-- 
	Theory of operation:
	1. Threshold for bright spots. This captures the part's electrodes.
	2. Threshold for dark spots, and invert them. This captures the part's body.
	3. Add the two results above together to get both electrodes and body but no background.
	4. Blur the result to re-join electrodes and bodies.
	5. Find contours and filter out small ones. This removes electrodes without bodies - upsidedown
	resistors.
	6. Get bounding boxes for contours and orient them to landscape. This "lays down" rectangular
	components.
	7. Negate the angle of the results, as MinAreaRect renders angles that are opposite from what
	OpenPnP expects. 
 -->
<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="capture" enabled="true" settle-first="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="gray" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="highlights" enabled="true" threshold="200" auto="false" invert="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="recall1" enabled="true" image-stage-name="gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="lowlights" enabled="true" threshold="120" auto="false" invert="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Add" name="combined" enabled="true" first-stage-name="highlights" second-stage-name="lowlights"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="merged" enabled="true" kernel-size="9"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="contours" enabled="true" retrieval-mode="External" approximation-method="Simple"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="filtered_contours" enabled="true" contours-stage-name="contours" min-area="500.0" max-area="1500.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="rects" enabled="true" contours-stage-name="filtered_contours"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.OrientRotatedRects" name="oriented_rects" enabled="true" rotated-rects-stage-name="rects" orientation="Landscape" negate-angle="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.OrientRotatedRects" name="results" enabled="true" rotated-rects-stage-name="rects" orientation="Landscape" negate-angle="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="recall2" enabled="true" image-stage-name="capture"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawContours" name="draw_contours" enabled="true" contours-stage-name="filtered_contours" thickness="2" index="-1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="draw_results" enabled="true" rotated-rects-stage-name="oriented_rects" thickness="2" draw-rect-center="true" rect-center-radius="3" show-orientation="true">
         <color r="51" g="255" b="51" a="255"/>
      </cv-stage>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::advancedLoosePartFeeder() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRead" name="00" enabled="false" file="_openpnp/vision/openpnp-pipeline-tests"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" settle-first="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="1" enabled="true" kernel-size="19"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="19" enabled="true" conversion="Bgr2HsvFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="2" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="3" enabled="true" hue-min="89" hue-max="115" saturation-min="20" saturation-max="95" value-min="50" value-max="130" invert="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="4" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="5" enabled="true" threshold="8" auto="false" invert="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="6" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="7" enabled="true" contours-stage-name="6" min-area="1000.0" max-area="100000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="8" enabled="true" contours-stage-name="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ReadPartTemplateImage" name="14" enabled="true" template-file="" extension=".png" prefix="top-" log="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="10" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="16" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ComposeResult" name="13" enabled="true" image-stage-name="16" model-stage-name="14"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="11" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskModel" name="12" enabled="true" model-stage-name="7" is-mask="false">
         <color r="0" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ClosestModel" name="18" enabled="true" log="true" model-stage-name="8" filter-stage-name="13" tolerance="0.2" scale="1.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskModel" name="15" enabled="true" model-stage-name="18" is-mask="false">
         <color r="0" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="9" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="17" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MatchPartTemplate" name="results" enabled="true" template-stage-name="13" model-stage-name="18" threshold="0.2"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="21" enabled="true" conversion="Gray2Bgr"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="20" enabled="true" rotated-rects-stage-name="results" thickness="1" draw-rect-center="false" rect-center-radius="20"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::advancedLoosePartFeederTraining() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRead" name="00" enabled="false" file="/home/dz/develop/openpnp/_openpnp/vision/openpnp-pipeline-tests/TO252/original_6313736782598297700.png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" settle-first="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="1" enabled="true" prefix="original_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="2" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="3" enabled="true" conversion="Bgr2HsvFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="4" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="5" enabled="true" hue-min="90" hue-max="120" saturation-min="45" saturation-max="100" value-min="45" value-max="120" invert="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="6" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="7" enabled="true" threshold="80" auto="true" invert="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="8" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="9" enabled="true" contours-stage-name="8" min-area="3000.0" max-area="200000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="10" enabled="true" contours-stage-name="9"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="11" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskModel" name="12" enabled="true" model-stage-name="9" is-mask="false">
         <color r="0" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.CreateModelTemplateImage" name="14" enabled="true" model-stage-name="10" degrees="0.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.WritePartTemplateImage" name="13" enabled="true" template-file="" extension=".png" prefix="top-" as-package="false"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::cameraCalibration() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="image" enabled="true" default-light="true" settle-option="SettleFullArea" count="1"/>
      <cv-stage class="org.jplacer.vision.pipeline.stages.DetectRoundMark" name="detect_mark" enabled="true" min-diameter="18" max-diameter="25" max-distance="100" size-tolerance="0.25" min-shape="0.8" polarity="Either" property-name="DetectCircularSymmetry"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertModelToKeyPoints" name="results" enabled="true" model-stage-name="detect_mark"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::nozzleTipCalibration() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="false" settle-first="true" count="1">
         <light class="java.lang.Boolean">true</light>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb1" enabled="false" prefix="runout_calibration_source_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="2" enabled="false" kernel-size="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DetectCircularSymmetry" name="results" enabled="true" min-diameter="30" max-diameter="50" max-distance="200" max-target-count="1" min-symmetry="1.2" corr-symmetry="0.0" property-name="nozzleTip" outer-margin="0.2" inner-margin="0.4" sub-sampling="8" super-sampling="1" diagnostics="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawCircles" name="4" enabled="true" circles-stage-name="results" thickness="2">
         <color r="255" g="0" b="51" a="255"/>
         <center-color r="0" g="204" b="255" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb1" enabled="false" prefix="runout_calibration_result_" suffix=".png"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::feederVisionCircularSymmetry() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb0" enabled="false" prefix="push_pull_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="1" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.AffineWarp" name="2" enabled="true" length-unit="Millimeters" x-0="0.0" y-0="0.0" x-1="0.0" y-1="0.0" x-2="0.0" y-2="0.0" scale="1.0" rectify="true" region-of-interest-property="regionOfInterest"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="3" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.SimpleOcr" name="OCR" enabled="true" alphabet="0123456789.-+_RCLDQYXJIVAFH%GMKkmuµnp" font-name="Liberation Mono" font-size-pt="7.0" font-max-pixel-size="20" auto-detect-size="false" threshold="0.75" draw-style="OverOriginalImage" debug="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="5" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DetectCircularSymmetry" name="results" enabled="true" min-diameter="10" max-diameter="100" max-distance="100" max-target-count="10" min-symmetry="1.2" corr-symmetry="0.05" property-name="sprocketHole" outer-margin="0.3" inner-margin="0.1" sub-sampling="8" super-sampling="1" symmetry-score="RingMedianVarianceVsRingVarianceSum" diagnostics="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="7" enabled="false" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawCircles" name="8" enabled="false" circles-stage-name="results" thickness="1">
         <color r="255" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb1" enabled="false" prefix="push_pull_result_" suffix=".png"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::feederVisionColorKeyed() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRead" name="00" enabled="false" file="test.png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="1" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.AffineWarp" name="11" enabled="true" length-unit="Millimeters" x-0="0.0" y-0="0.0" x-1="0.0" y-1="0.0" x-2="0.0" y-2="0.0" scale="1.0" rectify="true" region-of-interest-property="regionOfInterest"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="12" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.HistogramEqualizeAdaptive" name="13" enabled="false" channels-to-equalize="First" clip-limit="2.0" number-of-tile-rows="1" number-of-tile-cols="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.SimpleOcr" name="OCR" enabled="true" alphabet="0123456789.-+_RCLDQYXJIVAFH%GMKkmuµnp" font-name="Liberation Mono" font-size-pt="7.0" font-max-pixel-size="20" auto-detect-size="false" threshold="0.75" draw-style="OverOriginalImage" debug="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="20" enabled="true" image-stage-name="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="21" enabled="true" diameter="600"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="22" enabled="true" conversion="Bgr2HsvFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="23" enabled="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="24" enabled="true" auto="false" fraction-to-mask="0.0" hue-min="240" hue-max="130" saturation-min="110" saturation-max="255" value-min="10" value-max="255" invert="false" binary-mask="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="25" enabled="true" kernel-size="13"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="27" enabled="true" retrieval-mode="List" approximation-method="Simple"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="28" enabled="true" contours-stage-name="27" min-area="1000.0" max-area="100000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FitEllipseContours" name="results" enabled="true" contours-stage-name="28"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="30" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawEllipses" name="31" enabled="false" ellipses-stage-name="results" thickness="2"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string* JPDefaultPipelines::heapFeeder(const std::string& type, const std::string& colour) {
    static const std::map<std::string, std::string> xml {
        { "DropBox-GREEN", R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="capture" enabled="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="blur" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="bgr2hsv" enabled="true" conversion="Bgr2Hsv"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="mask" enabled="true" auto="false" fraction-to-mask="0.0" hue-min="50" hue-max="110" saturation-min="30" saturation-max="255" value-min="0" value-max="240" invert="false" binary-mask="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="1" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="maskCircle" enabled="true" diameter="450"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="maskContours" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="filteredContours" enabled="true" contours-stage-name="maskContours" min-area="250.0" max-area="15000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="rectRaw" enabled="true" contours-stage-name="filteredContours"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.OrientRotatedRects" name="results" enabled="true" rotated-rects-stage-name="rectRaw" orientation="Landscape" negate-angle="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="recall" enabled="true" image-stage-name="capture"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="drawRect" enabled="true" rotated-rects-stage-name="results" thickness="2" draw-rect-center="true" rect-center-radius="10" show-orientation="true"/>
   </stages>
</cv-pipeline>)PIPELINE" },
        { "DropBox-WHITE", R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="capture" enabled="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="0" enabled="true" diameter="450"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="gray" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="highlights" enabled="true" threshold="200" auto="false" invert="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="merged" enabled="true" kernel-size="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="contours" enabled="true" retrieval-mode="Tree" approximation-method="Simple"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="filtered_contours" enabled="true" contours-stage-name="contours" min-area="100.0" max-area="9000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="rects" enabled="true" contours-stage-name="filtered_contours"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterRects" name="frects" enabled="true" width-max="90.0" width-min="10.0" length-max="180.0" length-min="20.0" aspect-ratio-max="1.0" aspect-ratio-min="0.3" enable-logging="false" rotated-rects-stage-name="rects"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.OrientRotatedRects" name="oriented_rects" enabled="true" rotated-rects-stage-name="frects" orientation="Landscape" negate-angle="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.OrientRotatedRects" name="results" enabled="true" rotated-rects-stage-name="frects" orientation="Landscape" negate-angle="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="recall2" enabled="true" image-stage-name="capture"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawContours" name="draw_contours" enabled="true" contours-stage-name="filtered_contours" thickness="2" index="-1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="draw_results" enabled="true" rotated-rects-stage-name="oriented_rects" thickness="2" draw-rect-center="true" rect-center-radius="3" show-orientation="true">
         <color r="51" g="255" b="51" a="255"/>
      </cv-stage>
   </stages>
</cv-pipeline>)PIPELINE" },
        { "DropBox-BLACK", R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="capture" enabled="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="0" enabled="true" diameter="450"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="gray" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="highlights" enabled="true" threshold="30" auto="false" invert="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="merged" enabled="true" kernel-size="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="contours" enabled="true" retrieval-mode="Tree" approximation-method="Simple"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="filtered_contours" enabled="true" contours-stage-name="contours" min-area="100.0" max-area="9000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="rects" enabled="true" contours-stage-name="filtered_contours"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterRects" name="frects" enabled="true" width-max="90.0" width-min="10.0" length-max="180.0" length-min="20.0" aspect-ratio-max="1.0" aspect-ratio-min="0.3" enable-logging="false" rotated-rects-stage-name="rects"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.OrientRotatedRects" name="oriented_rects" enabled="true" rotated-rects-stage-name="frects" orientation="Landscape" negate-angle="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.OrientRotatedRects" name="results" enabled="true" rotated-rects-stage-name="frects" orientation="Landscape" negate-angle="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="recall2" enabled="true" image-stage-name="capture"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawContours" name="draw_contours" enabled="true" contours-stage-name="filtered_contours" thickness="2" index="-1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="draw_results" enabled="true" rotated-rects-stage-name="oriented_rects" thickness="2" draw-rect-center="true" rect-center-radius="3" show-orientation="true">
         <color r="51" g="255" b="51" a="255"/>
      </cv-stage>
   </stages>
</cv-pipeline>)PIPELINE" },
        { "Part-GREEN", R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="false" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="1" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="19" enabled="true" conversion="Bgr2HsvFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="3" enabled="true" auto="false" fraction-to-mask="0.0" hue-min="55" hue-max="110" saturation-min="120" saturation-max="255" value-min="30" value-max="250" invert="false" binary-mask="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="4" enabled="true" diameter="450"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="22" enabled="true" kernel-size="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="6" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="7" enabled="true" contours-stage-name="6" min-area="750.0" max-area="10000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="8" enabled="true" contours-stage-name="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ReadPartTemplateImage" name="14" enabled="true" template-file="" extension=".png" prefix="top-" log="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="10" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="16" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ComposeResult" name="13" enabled="true" image-stage-name="16" model-stage-name="14"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="11" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskModel" name="12" enabled="true" model-stage-name="7" is-mask="false">
         <color r="0" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="9" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="17" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MatchPartsTemplate" name="results" enabled="true" log="false" template-stage-name="13" model-stage-name="8" threshold="0.85"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="24" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="20" enabled="true" rotated-rects-stage-name="results" thickness="2" draw-rect-center="false" rect-center-radius="20" show-orientation="true"/>
   </stages>
</cv-pipeline>)PIPELINE" },
        { "Part-WHITE", R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="false" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="1" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="19" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="2" enabled="true" diameter="450"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="5" enabled="true" threshold="200" auto="false" invert="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="3" enabled="true" kernel-size="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="6" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="7" enabled="true" contours-stage-name="6" min-area="500.0" max-area="100000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="8" enabled="true" contours-stage-name="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ReadPartTemplateImage" name="14" enabled="true" template-file="" extension=".png" prefix="top-" log="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="10" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="16" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ComposeResult" name="13" enabled="true" image-stage-name="16" model-stage-name="14"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="11" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskModel" name="12" enabled="true" model-stage-name="7" is-mask="false">
         <color r="0" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="9" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="17" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MatchPartsTemplate" name="results" enabled="true" log="false" template-stage-name="13" model-stage-name="8" threshold="0.85"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="15" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="20" enabled="true" rotated-rects-stage-name="results" thickness="2" draw-rect-center="false" rect-center-radius="20" show-orientation="true"/>
   </stages>
</cv-pipeline>)PIPELINE" },
        { "Part-BLACK", R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="false" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="1" enabled="true" kernel-size="19"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="19" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="2" enabled="true" diameter="450"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="5" enabled="true" threshold="70" auto="false" invert="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="3" enabled="true" kernel-size="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="6" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="7" enabled="true" contours-stage-name="6" min-area="500.0" max-area="100000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="8" enabled="true" contours-stage-name="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ReadPartTemplateImage" name="14" enabled="true" template-file="" extension=".png" prefix="top-" log="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="10" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="16" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ComposeResult" name="13" enabled="true" image-stage-name="16" model-stage-name="14"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="11" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskModel" name="12" enabled="true" model-stage-name="7" is-mask="false">
         <color r="0" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="9" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="17" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MatchPartsTemplate" name="results" enabled="true" log="false" template-stage-name="13" model-stage-name="8" threshold="0.85"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="15" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="20" enabled="true" rotated-rects-stage-name="results" thickness="2" draw-rect-center="false" rect-center-radius="20" show-orientation="true"/>
   </stages>
</cv-pipeline>)PIPELINE" },
        { "Training-GREEN", R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="2" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="3" enabled="true" conversion="Bgr2HsvFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="5" enabled="true" auto="false" fraction-to-mask="0.0" hue-min="55" hue-max="110" saturation-min="120" saturation-max="255" value-min="10" value-max="250" invert="false" binary-mask="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="6" enabled="true" diameter="450"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="1" enabled="true" kernel-size="7"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="8" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="9" enabled="true" contours-stage-name="8" min-area="300.0" max-area="200000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="10" enabled="true" contours-stage-name="9"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="11" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskModel" name="12" enabled="true" model-stage-name="9" is-mask="false">
         <color r="0" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.CreateModelTemplateImage" name="14" enabled="true" model-stage-name="10" degrees="90.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.WritePartTemplateImage" name="13" enabled="true" template-file="" extension=".png" prefix="top-" as-package="false"/>
   </stages>
</cv-pipeline>)PIPELINE" },
        { "Training-WHITE", R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="2" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="3" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="7" enabled="true" threshold="200" auto="false" invert="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="4" enabled="true" diameter="450"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="1" enabled="true" kernel-size="9"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="8" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="9" enabled="true" contours-stage-name="8" min-area="500.0" max-area="200000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="10" enabled="true" contours-stage-name="9"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="11" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskModel" name="12" enabled="true" model-stage-name="9" is-mask="false">
         <color r="0" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.CreateModelTemplateImage" name="14" enabled="true" model-stage-name="10" degrees="90.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.WritePartTemplateImage" name="13" enabled="true" template-file="" extension=".png" prefix="top-" as-package="false"/>
   </stages>
</cv-pipeline>)PIPELINE" },
        { "Training-BLACK", R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="2" enabled="true" kernel-size="5"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="3" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="7" enabled="true" threshold="80" auto="false" invert="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="4" enabled="true" diameter="450"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="1" enabled="true" kernel-size="9"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="8" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="9" enabled="true" contours-stage-name="8" min-area="500.0" max-area="200000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="10" enabled="true" contours-stage-name="9"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="11" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskModel" name="12" enabled="true" model-stage-name="9" is-mask="false">
         <color r="0" g="0" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.CreateModelTemplateImage" name="14" enabled="true" model-stage-name="10" degrees="90.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.WritePartTemplateImage" name="13" enabled="true" template-file="" extension=".png" prefix="top-" as-package="false"/>
   </stages>
</cv-pipeline>)PIPELINE" },
    };
    const auto it = xml.find(type + "-" + colour);
    return it == xml.end() ? nullptr : &it->second;
}

const std::string& JPDefaultPipelines::blindsFeeder() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRead" name="00" enabled="false" file="test.png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="1" enabled="true" kernel-size="3"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.AffineWarp" name="ocr1" enabled="true" length-unit="Millimeters" x-0="0.0" y-0="0.0" x-1="0.0" y-1="0.0" x-2="0.0" y-2="0.0" scale="1.0" rectify="true" region-of-interest-property="regionOfInterest"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="ocr2" enabled="false" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.SimpleOcr" name="OCR" enabled="true" alphabet="0123456789.-+_RCLDQYXJIVAFH%GMKkmuµnp" font-name="Liberation Mono" font-size-pt="7.0" font-max-pixel-size="28" auto-detect-size="false" threshold="0.75" draw-style="OverScaledImage" debug="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="11" enabled="true" image-stage-name="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="2" enabled="true" conversion="Bgr2HsvFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Normalize" name="3" enabled="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="4" enabled="true" auto="false" fraction-to-mask="0.0" hue-min="65" hue-max="115" saturation-min="50" saturation-max="255" value-min="40" value-max="255" invert="false" binary-mask="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurMedian" name="5" enabled="true" kernel-size="13"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="7" enabled="true" retrieval-mode="List" approximation-method="Simple"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="8" enabled="true" contours-stage-name="7" min-area="1000.0" max-area="100000.0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRectContours" name="results" enabled="true" contours-stage-name="8"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="10" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="12" enabled="false" rotated-rects-stage-name="results" thickness="3" draw-rect-center="true" rect-center-radius="20" show-orientation="false"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::bottomVision() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
    <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pThreshold" enabled="true" parameter-label="Threshold" 
          parameter-description="Set the brightness threshold that isolates the shiny contacts of a part." stage-name="threshold" property-name="threshold" effect-stage-name="threshold" preview-result="true" minimum-value="1.0" maximum-value="254.0" default-value="100.0" numeric-type="Integer"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pDetail" enabled="true" parameter-label="Min. Detail Size" parameter-description="Minimal size of a detail that should be included in the detected shape." stage-name="filterContours" property-name="minArea" effect-stage-name="contours" preview-result="true" minimum-value="0.0" maximum-value="0.25" default-value="0.01" numeric-type="SquareMillimetersToPixels"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb0" enabled="true" prefix="bv_source_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="3" enabled="true" kernel-size="9" property-name="BlurGaussian"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="4" enabled="true" diameter="525" property-name="MaskCircle"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="4b" enabled="true" diameter="100000" property-name="partmask"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="5" enabled="true" conversion="Bgr2HsvFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="6" enabled="true" auto="false" fraction-to-mask="0.0" hue-min="60" hue-max="130" saturation-min="32" saturation-max="255" value-min="64" value-max="255" invert="false" binary-mask="false" property-name="MaskHsv"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="7" enabled="true" conversion="Hsv2BgrFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="8" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.Threshold" name="threshold" enabled="true" threshold="100" auto="false" invert="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="findCountours" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="filterContours" enabled="true" contours-stage-name="findCountours" min-area="0.01" max-area="900000.0" property-name="FilterContours"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="11" enabled="true" diameter="0" property-name=""/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawContours" name="contours" enabled="true" contours-stage-name="filterContours" thickness="1" index="-1">
         <color r="255" g="255" b="255" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRect" name="results" enabled="true" threshold-min="100" threshold-max="255"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="14" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="15" enabled="true" rotated-rects-stage-name="results" thickness="2" draw-rect-center="false" rect-center-radius="20" show-orientation="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb1" enabled="true" prefix="bv_result_" suffix=".png"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::fiducialLocator() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="maxDistance" enabled="true" parameter-label="Max. Distance" parameter-description="Maximum allowed distance between nominal fiducial location and detected location." stage-name="circular" property-name="maxDistance" effect-stage-name="circular" preview-result="true" minimum-value="0.0" maximum-value="10.0" default-value="4.0" numeric-type="MillimetersToPixels"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="image" enabled="true" default-light="true" settle-option="Settle" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb0" enabled="false" prefix="fidloc_source_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="blur" enabled="false" kernel-size="3" property-name="BlurGaussian"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DetectCircularSymmetry" name="circular" enabled="true" min-diameter="10" max-diameter="100" max-distance="4" search-width="0" search-height="0" max-target-count="1" min-symmetry="1.2" corr-symmetry="0.0" outer-margin="0.2" inner-margin="0.4" sub-sampling="8" super-sampling="8" symmetry-score="OverallVarianceVsRingVarianceSum" property-name="fiducial" diagnostics="true" heat-map="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="0" enabled="true" image-stage-name="image"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertModelToKeyPoints" name="results" enabled="true" model-stage-name="circular"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawCircles" name="draw" enabled="true" circles-stage-name="circular" thickness="1">
         <color r="255" g="255" b="0" a="255"/>
         <center-color r="255" g="153" b="0" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb1" enabled="false" prefix="fidloc_results_" suffix=".png"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::bottomVisionRectlinear() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ParameterBool" name="pSymmetricLeftRight" enabled="true" parameter-label="Symmetry Left/Right" parameter-description="Switch On if parts are symmetric Left/Right (seen in 0° rotation)." stage-name="results" property-name="symmetricLeftRight" effect-stage-name="results" preview-result="true" invert="false" default-value="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ParameterBool" name="pSymmetricUpperLower" enabled="true" parameter-label="Symmetry Upper/Lower" parameter-description="Switch On if parts are symmetric Upper/Lower (seen in 0° rotation)" stage-name="results" property-name="symmetricUpperLower" effect-stage-name="results" preview-result="true" invert="false" default-value="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pThreshold" enabled="true" parameter-label="Threshold" parameter-description="Threshold brightness for relevant image elements (shiny contacts). Only relevant for asymmetric detection." stage-name="results" property-name="threshold" effect-stage-name="results" preview-result="true" minimum-value="1.0" maximum-value="254.0" default-value="128.0" numeric-type="Integer"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pMinDetail" enabled="true" parameter-label="Min. Detail Size" parameter-description="Minimum Detail Size that should be considered masked. Only relevant for asymmetric detection." stage-name="results" property-name="minFeatureSize" effect-stage-name="results" preview-result="true" minimum-value="0.0" maximum-value="0.8" default-value="0.05" numeric-type="MillimetersToPixels"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-first="true" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb0" enabled="true" prefix="bv_source_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="2" enabled="true" kernel-size="7" property-name="BlurGaussian"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="3" enabled="true" diameter="900" property-name="MaskCircle"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="4" enabled="true" conversion="Bgr2HsvFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="5" enabled="true" auto="false" fraction-to-mask="0.0" hue-min="80" hue-max="150" saturation-min="64" saturation-max="255" value-min="0" value-max="225" 
        soft-edge="16" soft-factor="0.75" invert="false" binary-mask="false" property-name="MaskHsv"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="6" enabled="true" conversion="Hsv2BgrFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DetectRectlinearSymmetry" name="results" enabled="true" expected-angle="225.0" search-distance="100.0" search-angle="30.0" max-width="510.0" max-height="510.0" min-feature-size="0.05" symmetric-left-right="true" symmetric-upper-lower="true" symmetric-function="FullSymmetry" asymmetric-function="OutlineSymmetryMasked" min-symmetry="10.0" sub-sampling="8" super-sampling="2" smoothing="5" gamma="2.5" threshold="128" property-name="DetectRectlinearSymmetry" diagnostics="true" diagnostics-map="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="8" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="9" enabled="true" rotated-rects-stage-name="results" thickness="2" draw-rect-center="false" rect-center-radius="20" show-orientation="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb1" enabled="true" prefix="bv_result_" suffix=".png"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::bottomVisionBody() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="pDetail" enabled="true" parameter-label="Min. Detail Size" parameter-description="Minimal size of a detail that should be included in the detected shape." stage-name="filterContours" property-name="minArea" effect-stage-name="contours" preview-result="true" minimum-value="0.0" maximum-value="0.25" default-value="0.01" numeric-type="SquareMillimetersToPixels"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="0" enabled="true" default-light="true" settle-option="Settle" count="1"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb0" enabled="true" prefix="bv_source_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.BlurGaussian" name="3" enabled="true" kernel-size="9" property-name="BlurGaussian"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="5" enabled="true" conversion="Bgr2HsvFull"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskHsv" name="threshold" enabled="true" auto="false" fraction-to-mask="0.0" hue-min="60" hue-max="130" saturation-min="32" saturation-max="255" value-min="64" value-max="100" soft-edge="0" soft-factor="1.0" invert="false" binary-mask="true" property-name="MaskHsv"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="4" enabled="true" diameter="525" property-name="MaskCircle"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="4b" enabled="true" diameter="100000" property-name="partmask"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FindContours" name="findCountours" enabled="true" retrieval-mode="List" approximation-method="None"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.FilterContours" name="filterContours" enabled="true" contours-stage-name="findCountours" min-area="0.01" max-area="900000.0" property-name="FilterContours"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MaskCircle" name="11" enabled="true" diameter="0" property-name=""/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawContours" name="contours" enabled="true" contours-stage-name="filterContours" thickness="1" index="-1">
         <color r="255" g="255" b="255" a="255"/>
      </cv-stage>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MinAreaRect" name="results" enabled="true" threshold-min="100" threshold-max="255" expected-angle="0.0" search-angle="45.0" left-edge="true" right-edge="true" top-edge="true" bottom-edge="true" diagnostics="false" property-name="MinAreaRect"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="14" enabled="true" image-stage-name="0"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawRotatedRects" name="15" enabled="true" rotated-rects-stage-name="results" thickness="2" draw-rect-center="false" rect-center-radius="20" show-orientation="false"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="deb1" enabled="true" prefix="bv_result_" suffix=".png"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

const std::string& JPDefaultPipelines::fiducialLocatorTemplate() {
    static const std::string xml = R"PIPELINE(<cv-pipeline>
   <stages>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ParameterNumeric" name="maxDistance" enabled="true" parameter-label="Max. Distance" parameter-description="Maximum allowed distance between nominal fiducial location and detected location." stage-name="match_template" property-name="maxDistance" effect-stage-name="draw_keypoints" preview-result="true" minimum-value="0.0" maximum-value="10.0" default-value="4.0" numeric-type="MillimetersToPixels"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.CreateFootprintTemplateImage" name="template" enabled="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="debug_template" enabled="true" prefix="fidloc_template_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="template_gray" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageCapture" name="image" enabled="true" settle-first="true"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertColor" name="image_gray" enabled="true" conversion="Bgr2Gray"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="debug_original" enabled="true" prefix="fidloc_original_" suffix=".png"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.MatchTemplate" name="match_template" enabled="true" template-stage-name="template_gray" threshold="0.699999988079071" corr="0.8500000238418579" normalize="true" property-name="fiducial" />
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageRecall" name="recall_image" enabled="true" image-stage-name="image"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawTemplateMatches" name="draw_matches" enabled="true" template-matches-stage-name="match_template"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ConvertModelToKeyPoints" name="results" enabled="true" model-stage-name="match_template"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.DrawKeyPoints" name="draw_keypoints" enabled="true" key-points-stage-name="results"/>
      <cv-stage class="org.openpnp.vision.pipeline.stages.ImageWriteDebug" name="debug_results" enabled="true" prefix="fidloc_results_" suffix=".png"/>
   </stages>
</cv-pipeline>)PIPELINE";
    return xml;
}

} // inline namespace jf
