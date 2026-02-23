#pragma once

#ifdef PROXYRES_TESTING
#  define PROXYRES_TESTABLE /* non-static */
#else
#  define PROXYRES_TESTABLE static
#endif
