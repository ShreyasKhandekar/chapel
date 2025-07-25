/*
 * Copyright 2021-2025 Hewlett Packard Enterprise Development LP
 * Other additional copyright holders may be indicated within.
 *
 * The entirety of this work is licensed under the Apache License,
 * Version 2.0 (the "License"); you may not use this file except
 * in compliance with the License.
 *
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include "filesystem_help.h"

#include "./my_strerror_r.h"

#include "chpl/framework/ErrorMessage.h"
#include "chpl/framework/Location.h"

#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/Config/llvm-config.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"

#include "llvm/Support/SHA256.h"

#include <cerrno>

#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pwd.h>

namespace chpl {


static void removeSpacesBackslashesFromString(char* str)
{
 char* src = str;
 char* dst = str;
 while (*src != '\0')
 {
   *dst = *src++;
   if (*dst != ' ' && *dst != '\\')
       dst++;
 }
 *dst = '\0';
}

static std::string my_strerror(int errno_) {
  char errbuf[256];
  int rc;
  errbuf[0] = '\0';
  rc = my_strerror_r(errno_, errbuf, sizeof(errbuf));
  if (rc != 0)
    strncpy(errbuf, "<unknown error>", sizeof(errbuf));
  return std::string(errbuf);
}

static std::error_code errorCodeFromCError(int err) {
  return std::error_code(err, std::generic_category());
}

/*
 * Find the default tmp directory. Try getting the tmp dir from the ISO/IEC
 * 9945 env var options first, then P_tmpdir, then "/tmp".
 */
static std::string getTempDir() {
  std::vector<std::string> possibleDirsInEnv = {"TMPDIR", "TMP", "TEMP", "TEMPDIR"};
  for (unsigned int i = 0; i < possibleDirsInEnv.size(); i++) {
    auto curDir = getenv(possibleDirsInEnv[i].c_str());
    if (curDir != NULL) {
      return curDir;
    }
  }
#ifdef P_tmpdir
  return P_tmpdir;
#else
  return "/tmp";
#endif
}

FILE* openfile(const char* path, const char* mode, std::string& errorOut) {
  FILE* fp = fopen(path, mode);
  if (fp == nullptr) {
    // set errorOut. NULL will be returned.
    errorOut = my_strerror(errno) + ".";
  }

  return fp;
}

bool closefile(FILE* fp, const char* path, std::string& errorOut) {
  int rc = fclose(fp);
  if (rc != 0) {
    errorOut = my_strerror(errno) + ".";
    return false;
  }
  return true;
}

// TODO: Should this produce an llvm::MemoryBuffer?
// TODO: Should this return std::error_code?
bool readFile(const char* path, std::string& strOut, std::string& errorOut) {
  FILE* fp = openfile(path, "r", errorOut);
  if (!fp) {
    return false;
  }

  // Try to get the file length in order to optimize string allocation
  long len = 0;
  fseek(fp, 0, SEEK_END);
  len = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  if (len > 0) strOut.reserve(len);

  char buf[256];
  while (true) {
    size_t got = fread(buf, 1, sizeof(buf), fp);
    if (got > 0) {
      strOut.append(buf, got);
    } else {
      if (ferror(fp)) {
        std::string ignoredErrorOut;
        closefile(fp, path, ignoredErrorOut);
        return false;
      }
      // otherwise, end of file reached
      break;
    }
  }

  return closefile(fp, path, errorOut);
}

std::error_code writeFile(const char* path, const std::string& data) {
  FILE* fp = fopen(path, "w");
  if (fp == nullptr) {
    return errorCodeFromCError(errno);
  }

  size_t got = fwrite(data.data(), 1, data.size(), fp);
  if (got != data.size()) {
    int err = ferror(fp);
    if (err == 0) err = EIO;
    fclose(fp);
    return errorCodeFromCError(errno);
  }

  int closeval = fclose(fp);
  if (closeval != 0) {
    return errorCodeFromCError(errno);
  }

  return std::error_code();
}


bool fileExists(const char* path) {
  struct stat s;
  int err = stat(path, &s);
  return err == 0;
}

std::error_code deleteDir(const llvm::Twine& dirname) {
  // LLVM 5 added remove_directories
  return llvm::sys::fs::remove_directories(dirname, false);
}

std::error_code makeTempDir(llvm::StringRef prefix,
                            std::string& tmpDirPathOut) {
  std::string tmpdirprefix = std::string(getTempDir()) + "/" + prefix.str();
  std::string tmpdirsuffix = ".deleteme-XXXXXX";

  struct passwd* passwdinfo = getpwuid(geteuid());
  const char* userid;
  if (passwdinfo == NULL) {
    userid = "anon";
  } else {
    userid = passwdinfo->pw_name;
  }
  char* myuserid = strdup(userid);
  removeSpacesBackslashesFromString(myuserid);
  std::string tmpDir = tmpdirprefix + std::string(myuserid) + tmpdirsuffix;
  char* tmpDirMut = strdup(tmpDir.c_str());
  // TODO: we could use llvm::sys::fs::createUniqueDirectory instead of mkdtemp
  // see the comment in LLVM source
  // https://llvm.org/doxygen/Path_8cpp_source.html#l00883
  auto dirRes = std::string(mkdtemp(tmpDirMut));

  // get the posix error code if mkdtemp failed.
  int err = errno;

  if (dirRes.empty()) {
    std::error_code ret = make_error_code(static_cast<std::errc>(err));
    return ret;
  }

  free(myuserid); myuserid = NULL;
  free(tmpDirMut); tmpDirMut = NULL;

  tmpDirPathOut = dirRes;
  return std::error_code();
}

std::error_code ensureDirExists(const llvm::Twine& dirname) {
  return llvm::sys::fs::create_directories(dirname);
}

bool isPathWriteable(const llvm::Twine& path) {
  return llvm::sys::fs::can_write(path);
}

// Functionality also exists in runtime/src/qio/sys.c
// returns empty std::error_code on success.
std::error_code currentWorkingDir(std::string& path_out) {
  llvm::SmallString<128> buf;
  if (std::error_code err = llvm::sys::fs::current_path(buf)) {
    return err;
  } else {
    // buf.str() returns an LLVM::StringRef,
    // so we call .str() on that to get a std::string.
    path_out = buf.str().str();
    return std::error_code();
  }
}

std::error_code makeDir(const llvm::Twine& dirpath, bool makeParents) {
  using namespace llvm::sys::fs;
  if (makeParents) {
    return create_directories(dirpath, true, perms::all_all);
  } else {
    return create_directory(dirpath, true, perms::all_all);
  }
}

std::string getExecutablePath(const char* argv0, void* MainExecAddr) {
  using namespace llvm::sys::fs;
  return getMainExecutable(argv0, MainExecAddr);
}

static llvm::SmallVector<char> normalizePath(llvm::StringRef path) {
  // return an empty string instead of cwd for an empty input path
  if (path.empty())
    return llvm::SmallVector<char>();

  std::error_code err;
  llvm::SmallVector<char> abspath(path.begin(), path.end());
  err = llvm::sys::fs::make_absolute(abspath);
  if (err) {
    // ignore error making it absolute & just use path
    abspath = llvm::SmallVector<char>(path.begin(), path.end());
  }

  // collapse .. etc (ignoring errors)
  llvm::SmallVector<char> realpath;
  err = llvm::sys::fs::real_path(abspath, realpath);
  if (err) {
    // ignore error making it real & try it a different way
    realpath = abspath;
    auto style = llvm::sys::path::Style::posix;
    llvm::sys::path::remove_dots(realpath, /* remove_dot_dot */ true, style);
  }

  return realpath;
}

bool isSameFile(llvm::StringRef path1, llvm::StringRef path2) {
  // first, handle "" as documented for this function
  if (path1.empty() && path2.empty())
    return true;
  if (path1.empty() || path2.empty())
    return false;

  // next, consider the filesystem
  std::error_code err;
  bool result = false;
  err = llvm::sys::fs::equivalent(path1, path2, result);
  if (!err) {
    return result;
  }

  // if there was an error, it could be that the paths don't exist
  // on the file system. Normalize the paths and compare them.
  auto n1 = normalizePath(path1);
  auto n2 = normalizePath(path2);
  return n1 == n2;
}

std::vector<std::string>
deduplicateSamePaths(const std::vector<std::string>& paths)
{
  std::vector<std::string> ret;
  std::set<llvm::sys::fs::UniqueID> idsSet;
  std::set<std::string> pathsSet;

  for (const auto& path : paths) {
    // normalize the path
    auto norm = normalizePath(path);
    std::string normPath = std::string(norm.data(), norm.size());

    auto pair1 = pathsSet.insert(normPath);
    if (pair1.second) {
      // it was inserted into the normalized paths set, so proceed

      // gather the filesystem ID
      llvm::sys::fs::file_status status;
      std::error_code err = llvm::sys::fs::status(path, status);
      if (err) {
        // Proceed based on the normalized path alone, without
        // consulting the filesystem.
        // Expect to reach this case if the path does not exist.
        ret.push_back(path);
      } else {
        // otherwise, add it only if the filesystem ID is unique
        auto pair2 = idsSet.insert(status.getUniqueID());
        if (pair2.second) {
          ret.push_back(path);
        }
      }
    }
  }

  return ret;
}

std::string cleanLocalPath(std::string path) {
  // TODO: this could/should use remove_leading_dotslash
  // or remove_dots from the LLVM Support Library's Path.h
  while (path.length() >= 2 && path[0] == '.' && path[1] == '/') {
    // string starts with ./
    path = path.substr(2);
  }

  return path;
}

static bool filePathInDirPath(const char* filePathPtr, size_t filePathLen,
                              const char* dirPathPtr, size_t dirPathLen) {
  if (dirPathLen == 0)
    return false; // documented behavior; use "." for the current dir.

  // create SmallVectors for the relevant paths so we can use LLVM Path stuff
  auto path = llvm::SmallVector<char>(filePathPtr, filePathPtr+filePathLen);
  auto dirPath = llvm::SmallVector<char>(dirPathPtr, dirPathPtr+dirPathLen);

  // set 'path' to filePath without the filename (i.e. the directory)
  auto style = llvm::sys::path::Style::posix;
  llvm::sys::path::remove_filename(path, style);
  llvm::sys::path::remove_dots(path, /* remove_dot_dot */ false, style);
  // remove_dots on foo.chpl returns "" but we want to match "." in that case
  if (path.size() == 0)
    path.push_back('.');

  // also normalize dirPath
  llvm::sys::path::remove_dots(dirPath, /* remove_dot_dot */ false, style);
  if (dirPath.size() == 0)
    dirPath.push_back('.');

  // add / to the end of path and dirPath if they are not present already
  if (path.back() != '/')
    path.push_back('/');
  if (dirPath.back() != '/')
    dirPath.push_back('/');

  // now, check that 'dirPath' is a prefix or equal to 'path'
  return dirPath.size() <= path.size() &&
         0 == memcmp(path.data(), dirPath.data(), dirPath.size());
}

bool filePathInDirPath(llvm::StringRef filePath, llvm::StringRef dirPath) {
  return filePathInDirPath(filePath.data(), filePath.size(),
                           dirPath.data(), dirPath.size());
}

bool filePathInDirPath(UniqueString filePath, UniqueString dirPath) {
  return filePathInDirPath(filePath.c_str(), filePath.length(),
                           dirPath.c_str(), dirPath.length());
}

std::string fileHashToHex(const HashFileResult& hash) {
  return llvm::toHex(hash, /* lower case */ false);
}

llvm::ErrorOr<HashFileResult> hashFile(const llvm::Twine& path) {
  FILE* fp = fopen(path.str().c_str(), "r");
  if (!fp) {
    return errorCodeFromCError(errno);
  }

  llvm::SHA256 hasher;

  uint8_t buf[256];
  while (true) {
    size_t got = fread(buf, 1, sizeof(buf), fp);
    if (got > 0) {
      hasher.update(llvm::ArrayRef<uint8_t>(buf, got));
    } else {
      int err = ferror(fp);
      if (err != 0) {
        fclose(fp);
        return errorCodeFromCError(err);
      }
      // otherwise, end of file reached
      break;
    }
  }

  fclose(fp);

  // In LLVM 15, SHA256::final returns a std::array.
  // In LLVM 14 an earlier, it returns a StringRef.
#if LLVM_VERSION_MAJOR >= 15
  return hasher.final();
#else
  HashFileResult result;
  llvm::StringRef s = hasher.final();
  CHPL_ASSERT(s.size() == sizeof(HashFileResult));
  memcpy(&result, s.data(), sizeof(HashFileResult));
  return result;
#endif
}

HashFileResult hashString(llvm::StringRef data) {
  llvm::SHA256 hasher;

  hasher.update(data);

  // In LLVM 15, SHA256::final returns a std::array.
  // In LLVM 14 an earlier, it returns a StringRef.
#if LLVM_VERSION_MAJOR >= 15
  return hasher.final();
#else
  HashFileResult result;
  llvm::StringRef s = hasher.final();
  CHPL_ASSERT(s.size() == sizeof(HashFileResult));
  memcpy(&result, s.data(), sizeof(HashFileResult));
  return result;
#endif
}

std::error_code copyModificationTime(const llvm::Twine& srcPath,
                                     const llvm::Twine& dstPath) {
  std::error_code err;
  llvm::sys::fs::file_status status;
  err = llvm::sys::fs::status(srcPath, status);
  if (err) {
    return err;
  }
  auto time = status.getLastModificationTime();
  //std::cout << "Copying time from " << time.time_since_epoch().count() << "\n";
  int fd = 0;
  err = llvm::sys::fs::openFileForWrite(dstPath, fd,
                                        llvm::sys::fs::CD_OpenExisting);
  if (!err) {
    err = llvm::sys::fs::setLastAccessAndModificationTime(fd, time);
    llvm::sys::Process::SafelyCloseFileDescriptor(fd);
  }
  return err;
}


} // namespace chpl
