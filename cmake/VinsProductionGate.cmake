# Release-candidate gate (#77 gate 3, #78 §5), shared by configure and its
# CTests. A release candidate pins the owner's release PUBLIC key and signs
# nothing; a private key is never an input of any build.
if(DEFINED VOX_PUBLISHER_PRIVATE_KEY AND NOT "${VOX_PUBLISHER_PRIVATE_KEY}" STREQUAL "")
  message(FATAL_ERROR "VOX_PUBLISHER_PRIVATE_KEY is not accepted: private keys never enter a "
    "build. Build a release candidate and sign it offline (docs/release/RELEASE_SIGNING.md).")
endif()
if(VOX_PRODUCTION_BUILD AND "${VOX_RELEASE_PUBLIC_KEY}" STREQUAL "")
  message(FATAL_ERROR "VOX_PRODUCTION_BUILD requires VOX_RELEASE_PUBLIC_KEY (the owner's release "
    "publisher public key). Release is BLOCKED until the owner provisions the release key.")
endif()
