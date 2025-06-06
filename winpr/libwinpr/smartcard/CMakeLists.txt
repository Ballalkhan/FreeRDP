# WinPR: Windows Portable Runtime
# libwinpr-smartcard cmake build script
#
# Copyright 2014 Marc-Andre Moreau <marcandre.moreau@gmail.com>
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

set(MODULE_PREFIX "WINPR_SMARTCARD")

if(PCSC_WINPR_FOUND)
  winpr_definition_add(WITH_WINPR_PCSC)
endif()

option(WITH_SMARTCARD_PCSC "Enable smartcard PCSC backend" ON)

set(${MODULE_PREFIX}_SRCS smartcard.c smartcard.h)

if(WITH_SMARTCARD_PCSC)
  winpr_definition_add(WITH_SMARTCARD_PCSC)
  list(APPEND ${MODULE_PREFIX}_SRCS smartcard_pcsc.c smartcard_pcsc.h)
endif()

if(WITH_SMARTCARD_INSPECT)
  winpr_definition_add(WITH_SMARTCARD_INSPECT)
  list(APPEND ${MODULE_PREFIX}_SRCS smartcard_inspect.c smartcard_inspect.h)
endif()

if(WIN32)
  list(APPEND ${MODULE_PREFIX}_SRCS smartcard_windows.c smartcard_windows.h)
endif()

winpr_module_add(${${MODULE_PREFIX}_SRCS})

if(PCSC_WINPR_FOUND)
  winpr_library_add_private(${PCSC_WINPR_LIBRARY})
endif()

if(BUILD_TESTING_INTERNAL OR BUILD_TESTING)
  add_subdirectory(test)
endif()
