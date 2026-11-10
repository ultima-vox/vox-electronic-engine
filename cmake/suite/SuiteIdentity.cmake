# ULTIMA VOX MUSIC — frozen plugin identities (owner decision D2, #112).
#
# JUCE 9 derives the VST3 component class ID from the manufacturer code and
# the plugin code (juce_VST3Interface.h, jucePluginId): the CID string is
#   ABCDEF01 9182FAEB <manufacturer code hex> <plugin code hex>.
# These values MUST NOT change once a build has been published: a DAW would
# no longer find the plugin in saved projects. suite_identity_tests checks the
# codes against the frozen CIDs below, IDENTITY.md repeats them, and the
# Windows suite workflow compares them with moduleinfo.json in each bundle.
#
# Never reuse the legacy identities: com.ultimavox.vstengine / DkP1,
# com.ultimavox.fullon-drums, or slotNN.pKKK parameter IDs.

set(VOX_SUITE_COMPANY_NAME "Ultima Vox")
set(VOX_SUITE_MANUFACTURER_CODE UlVx)
set(VOX_SUITE_VERSION 0.1.0)

set(VOX_SUITE_PRODUCTS syn drm fx)

set(VOX_SUITE_syn_TARGET VoxTranceSynthesis)
set(VOX_SUITE_syn_DIR trance-synthesis)
set(VOX_SUITE_syn_NAME "VOX Trance Synthesis")
set(VOX_SUITE_syn_BUNDLE_ID com.ultimavox.vox-trance-synthesis)
set(VOX_SUITE_syn_PLUGIN_CODE VxTS)
set(VOX_SUITE_syn_STATE_TAG VXTS)
set(VOX_SUITE_syn_CID ABCDEF019182FAEB556C567856785453)

set(VOX_SUITE_drm_TARGET VoxDrums)
set(VOX_SUITE_drm_DIR drums)
set(VOX_SUITE_drm_NAME "VOX Drums")
set(VOX_SUITE_drm_BUNDLE_ID com.ultimavox.vox-drums)
set(VOX_SUITE_drm_PLUGIN_CODE VxDr)
set(VOX_SUITE_drm_STATE_TAG VXDR)
set(VOX_SUITE_drm_CID ABCDEF019182FAEB556C567856784472)

set(VOX_SUITE_fx_TARGET VoxFX)
set(VOX_SUITE_fx_DIR fx)
set(VOX_SUITE_fx_NAME "VOX FX")
set(VOX_SUITE_fx_BUNDLE_ID com.ultimavox.vox-fx)
set(VOX_SUITE_fx_PLUGIN_CODE VxFX)
set(VOX_SUITE_fx_STATE_TAG VXFX)
set(VOX_SUITE_fx_CID ABCDEF019182FAEB556C567856784658)
