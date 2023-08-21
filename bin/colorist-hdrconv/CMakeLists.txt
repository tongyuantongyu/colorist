# ---------------------------------------------------------------------------
#                         Copyright Joe Drago 2018.
#         Distributed under the Boost Software License, Version 1.0.
#            (See accompanying file LICENSE_1_0.txt or copy at
#                  http://www.boost.org/LICENSE_1_0.txt)
# ---------------------------------------------------------------------------

enable_language(CXX)
set(CMAKE_CXX_STANDARD 20)

set(COLORIST_HDRCONV_SRCS
        main.cpp
)

add_executable(colorist-hdrconv
        ${COLORIST_HDRCONV_SRCS}
)
target_link_libraries(colorist-hdrconv PRIVATE colorist)
set_target_properties(colorist-hdrconv PROPERTIES OUTPUT_NAME colorist-hdrconv)
