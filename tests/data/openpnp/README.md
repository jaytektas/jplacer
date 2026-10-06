Sample files from OpenPnP (https://github.com/openpnp/openpnp, GPL-3.0),
`samples/pnp-test`, `samples/EAT001` and `src/main/resources/config`, used
as they are to test that jplacer reads, converts and writes OpenPnP's
boards, panels, jobs, parts and packages as OpenPnP does.

`compositing/packages.xml` and `compositing/parts.xml` are OpenPnP's
`src/test/resources/config/VisionCompositingTest` packages and parts (its VisionCompositingTest); `compositing/expected.txt` is what OpenPnP's VisionCompositing.Composite
works out for each (solution and shots), for its default simulated up camera
(640 x 480 at 0.0134375 mm a pixel), a nozzle tip whose largest part is 20 mm
with a 1 mm pick tolerance, and a 30 mm roaming radius.

`bottom-vision-offset/packages.xml` and `parts.xml` are OpenPnP's
`src/test/resources/config/ReferenceBottomVisionOffset` configuration, used by its
ReferenceBottomVisionOffsetTest and ReferenceBottomVisionInheritanceTest.

`eagle/` holds OpenPnP's `samples/Demo Board/Demo Board v2.mn[tb]`, `samples/EAT001/EAT001.mn[tb]`,
`samples/test/mountsmd_whole_numbers.mnt` and `src/test/resources/samples/eagle/eagle.brd` and
`eagle.sch`, read by its EagleMountsmdUlpImporterTest and EagleLoaderTest.

`job-processor/` is OpenPnP's `src/test/resources/config/JobProcessorTest`: its two-nozzle test machine,
parts, packages and its panelized job of the pnp-test board, run by its JobProcessorTest.

`basic-job/` is OpenPnP's `src/test/resources/config/BasicJobTest`: its basic test machine (two
nozzles, two tips with changer locations, a tube feeder), parts and packages, run by its BasicJobTest.
Its machine's old single `<driver>` is OpenPnP's test driver (`org.openpnp.machine.reference.driver.test.TestDriver`,
which only passes moves on to the test); here it is the NullDriver, whose migration makes the same axes.

`sample-job/machine.xml` is OpenPnP's `src/test/resources/config/SampleJobTest/machine.xml`: its imperfect
simulated machine, run with OpenPnP's own parts, packages and sample jobs (`openpnp-defaults`) by its
SampleJobTest and SamplePanelizedJobTest.
