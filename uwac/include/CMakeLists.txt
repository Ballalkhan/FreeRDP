# UWAC: Using Wayland As Client
# cmake build script
#
# Copyright 2015 David FORT <contact@hardening-consulting.com>
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

if(NOT UWAC_FORCE_STATIC_BUILD)
  install(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/ DESTINATION ${UWAC_INCLUDE_DIR} FILES_MATCHING PATTERN "*.h")
  install(DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/uwac DESTINATION ${UWAC_INCLUDE_DIR} FILES_MATCHING PATTERN "*.h")
endif()
