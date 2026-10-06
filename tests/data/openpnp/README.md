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
