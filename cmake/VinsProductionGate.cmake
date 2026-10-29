# Production release gate (#77 gate 3), shared by configure and its CTest.
# A production build must be signed with the owner's release publisher key;
# there is no fallback to a development key.
if(VOX_PRODUCTION_BUILD AND NOT VOX_PUBLISHER_PRIVATE_KEY)
  message(FATAL_ERROR "VOX_PRODUCTION_BUILD requires VOX_PUBLISHER_PRIVATE_KEY (the owner's "
    "release publisher key). Release is BLOCKED until key custody is set up.")
endif()
