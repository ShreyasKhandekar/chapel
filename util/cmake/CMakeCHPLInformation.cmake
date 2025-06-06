# Copyright 2023-2025 Hewlett Packard Enterprise Development LP
# Other additional copyright holders may be indicated within.
#
# The entirety of this work is licensed under the Apache License,
# Version 2.0 (the "License"); you may not use this file except
# in compliance with the License.
#
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.


# TODO: this is a hack, how do we tell cmake that we can't compile to object files?
# TODO: how do we respect <DEFINES> and <INCLUDES>, CMake will only fill that in for CMAKE_*_COMPILER_OBJECT
set(CMAKE_CHPL_COMPILE_OBJECT "test -f <OBJECT> || ln -s <SOURCE> <OBJECT>")
set(CMAKE_CHPL_LINK_EXECUTABLE "<CMAKE_CHPL_COMPILER> -o <TARGET> <OBJECTS> <FLAGS> <LINK_FLAGS> <LINK_LIBRARIES>")

# TODO: support --library directly to build Chapel shared libs for interoperability
# CMAKE_*_CREATE_SHARED_LIBRARY

set(CMAKE_CHPL_SOURCE_FILE_EXTENSIONS chpl)

set(CMAKE_CHPL_OUTPUT_EXTENSION ".chpl")


set(CMAKE_CHPL_FLAGS_RELEASE "--fast")
set(CMAKE_CHPL_FLAGS_DEBUG "-g")
