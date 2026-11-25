# Licensing and notices

The A1 Revival restoration's integration code, native editor, helper/client
code, tests and build tools are licensed under the GNU Affero General Public
License, version 3 or (at your option) any later version
**AGPL-3.0-or-later**. The full text is in [LICENSE](LICENSE). This retains the
licensing of the integration/build code in the source Audio Plugins workspace.

JUCE 9.0.5 is fetched separately using its AGPL licensing option. Its notices
and dependency inventory are in [licenses/JUCE-LICENSE.md](licenses/JUCE-LICENSE.md)
and [licenses/JUCE.spdx.json](licenses/JUCE.spdx.json). The bundled VST3 SDK's
notice is retained in [licenses/VST3-SDK-LICENSE.txt](licenses/VST3-SDK-LICENSE.txt).

The pinned x87sidecar translator source is MIT licensed. Its notice is retained
in [licenses/x87sidecar-LICENSE.txt](licenses/x87sidecar-LICENSE.txt). The local
build fetches that source and applies [the reviewed patch](patches/x87sidecar-a1.patch).

## Original materials

The original Steinberg A1 binary, banks, XML, graphics, original marks and any
other commercial payloads remain subject to their original rights. CrossOver
and its components remain subject to their own licences. None of those local
payloads or prepared compatibility environments is included in this public
source repository. Their use as local build inputs does not grant a right to
redistribute them or place them under AGPL.

This is an independent compatibility restoration, not an official Steinberg,
CodeWeavers, Apple or JUCE release. Applicable licences provide the software
without warranty.
