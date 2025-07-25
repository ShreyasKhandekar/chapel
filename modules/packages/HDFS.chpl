/*
 * Copyright 2020-2025 Hewlett Packard Enterprise Development LP
 * Copyright 2004-2019 Cray Inc.
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

/* Support for the Hadoop Distributed File System.

This module implements support for the
`Hadoop <http://hadoop.apache.org/>`_
`Distributed Filesystem <http://hadoop.apache.org/docs/current/hadoop-project-dist/hadoop-hdfs/HdfsUserGuide.html>`_ (HDFS).

.. note::

  HDFS support in Chapel currently requires the use of ``CHPL_TASKS=fifo``.
  There is a compatibility problem with qthreads.

Using HDFS Support in Chapel
----------------------------

To open an HDFS file in Chapel, first create an :class:`HDFSFileSystem` by
connecting to an HDFS name node.

.. code-block:: chapel

  import HDFS;

  var fs = HDFS.connect(); // can pass a nameNode host and port here,
                           // otherwise uses HDFS default settings.

The filesystem connection will be closed when `fs` and any files
it refers to go out of scope.

Once you have a :record:`hdfs`, you can open files within that
filesystem using :proc:`HDFSFileSystem.open` and perform I/O on them using
the usual functionality in the :mod:`IO` module:

.. code-block:: chapel

  var f = fs.open("/tmp/testfile.txt", ioMode.cw);
  var writer = f.writer();
  writer.writeln("This is a test");
  writer.close();
  f.close();

.. note::

  Please note that ``ioMode.cwr`` and ``ioMode.rw`` are not supported with HDFS
  files due to limitations in HDFS itself. ``ioMode.r`` and ``ioMode.cw`` are
  the only modes supported with HDFS.

Dependencies
------------

Please refer to the Hadoop and HDFS documentation for instructions on setting up
HDFS.

Once you have a working HDFS, it's a good idea to test your HDFS installation
with a C program before proceeding with Chapel HDFS support. Try compiling the
below C program:

.. code-block:: c

  // hdfs-test.c

  #include <hdfs.h>

  #include <string.h>
  #include <stdio.h>
  #include <stdlib.h>

  int main(int argc, char **argv) {

      hdfsFS fs = hdfsConnect("default", 0);
      const char* writePath = "/tmp/testfile.txt";
      hdfsFile writeFile = hdfsOpenFile(fs, writePath, O_WRONLY|O_CREAT, 0, 0, 0);
      if(!writeFile) {
            fprintf(stderr, "Failed to open %s for writing!\n", writePath);
            exit(-1);
      }
      char* buffer = "Hello, World!";
      tSize num_written_bytes = hdfsWrite(fs, writeFile, (void*)buffer, strlen(buffer)+1);
      if (hdfsFlush(fs, writeFile)) {
             fprintf(stderr, "Failed to 'flush' %s\n", writePath);
            exit(-1);
      }
     hdfsCloseFile(fs, writeFile);
  }

This program will probably not compile without some special environment
variables set.  The following commands worked for us to compile this program,
but you will almost certainly need different settings depending on your HDFS
installation.

.. code-block:: bash

  export JAVA_HOME=/usr/lib/jvm/default-java/lib
  export HADOOP_HOME=/usr/local/hadoop/
  gcc hdfs-test.c -I$HADOOP_HOME/include -L$HADOOP_HOME/lib/native -lhdfs
  export CLASSPATH=`$HADOOP_HOME/bin/hadoop classpath --glob`
  export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$HADOOP_HOME/lib/native:$JAVA_HOME/lib
  ./a.out

  # verify that the new test file was created
  $HADOOP_HOME/bin/hdfs dfs  -ls /tmp


HDFS Support Types and Functions
--------------------------------

 */
@deprecated("The 'HDFS' module is being deprecated.  If you are still interested in using this module, please let us know!")
module HDFS {

  use IO, OS.POSIX, OS;
  public use CTypes;

  require "hdfs.h";

  /*
   Local copy of IO.EEOF as it is being phased out and is private in IO
   */
  private extern proc chpl_macro_int_EEOF():c_int;

  @chpldoc.nodoc
  extern "struct hdfs_internal" record hdfs_internal { }
  @chpldoc.nodoc
  extern "struct hdfsFile_internal" record hdfsFile_internal { }

  @chpldoc.nodoc
  extern type hdfsFS = c_ptr(hdfs_internal);
  @chpldoc.nodoc
  extern type hdfsFile = c_ptr(hdfsFile_internal);

  @chpldoc.nodoc
  extern record hdfsFileInfo {
    var mSize:tOffset;
    var mBlockSize:tOffset;
  }

  @chpldoc.nodoc
  extern type tSize = int(32);
  @chpldoc.nodoc
  extern type tOffset = int(64);
  @chpldoc.nodoc
  extern type tPort = uint(16);

  private extern proc hdfsConnect(nn:c_ptrConst(c_char), port:tPort):hdfsFS;
  private extern proc hdfsDisconnect(fs:hdfsFS):c_int;
  private extern proc hdfsOpenFile(fs:hdfsFS, path:c_ptrConst(c_char), flags:c_int,
                                   bufferSize:c_int, replication:c_short,
                                   blockSize:tSize):hdfsFile;
  private extern proc hdfsCloseFile(fs:hdfsFS, file:hdfsFile):c_int;
  private extern proc hdfsPread(fs:hdfsFS, file:hdfsFile, position:tOffset,
                                buffer:c_ptr(void), length:tSize):tSize;
  private extern proc hdfsWrite(fs:hdfsFS, file:hdfsFile,
                                buffer:c_ptr(void), length:tSize):tSize;
  private extern proc hdfsFlush(fs:hdfsFS, file:hdfsFile):c_int;
  private extern proc hdfsGetPathInfo(fs:hdfsFS, path:c_ptrConst(c_char)):c_ptr(hdfsFileInfo);
  private extern proc hdfsFreeFileInfo(info:c_ptr(hdfsFileInfo), numEntries:c_int);

  // QIO extern stuff
  private extern proc qio_strdup(s: c_ptrConst(c_char)): c_ptrConst(c_char);
  private extern proc qio_mkerror_errno():errorCode;
  private extern proc qio_channel_get_allocated_ptr_unlocked(ch:qio_channel_ptr_t, amt_requested:int(64), ref ptr_out:c_ptr(void), ref len_out:c_ssize_t, ref offset_out:int(64)):errorCode;
  private extern proc qio_channel_advance_available_end_unlocked(ch:qio_channel_ptr_t, len:c_ssize_t);
  private extern proc qio_channel_get_write_behind_ptr_unlocked(ch:qio_channel_ptr_t, ref ptr_out:c_ptr(void), ref len_out:c_ssize_t, ref offset_out:int(64)):errorCode;
  private extern proc qio_channel_advance_write_behind_unlocked(ch:qio_channel_ptr_t, len:c_ssize_t);

  private param verbose = false;

  /*

  Connect to an HDFS filesystem. If ``nameNode`` or ``port`` are not provided,
  the HDFS defaults will be used.

  :arg nameNode: the hostname for an HDFS name node to connect to
  :arg port: the port on which the HDFS service is running on the name node
  :returns: a :record:`hdfs` representing the connected filesystem.
  :throws SystemError: If the connection fails.
  */

  proc connect(nameNode: string = "default", port:int=0) throws {
    var fs = new unmanaged HDFSFileSystem(nameNode, port);
    if fs.hfs == nil {
      var err = qio_mkerror_errno();
      delete fs;
      throw createSystemError(err, "in hdfsConnect");
    }
    return new hdfs(fs);
  }

  /*

   Record storing an open HDFS filesystem. Please see :class:`HDFSFileSystem` for
   the forwarded methods available, in particular :proc:`HDFSFileSystem.open`.

  */
  record hdfs {
    @chpldoc.nodoc
    var instance:unmanaged HDFSFileSystem;
    forwarding instance;

    @chpldoc.nodoc
    proc init(instance:unmanaged HDFSFileSystem) {
      this.instance = instance;
    }
    @chpldoc.nodoc
    proc deinit() {
      var count = instance.release();
      if count == 0 then
        delete instance;
    }
  }

  /*
   Class representing a connected HDFS file system. This connected is
   reference counted and shared by open files.
   */
  class HDFSFileSystem {
    @chpldoc.nodoc
    var nameNode: string;
    @chpldoc.nodoc
    var port: int;
    @chpldoc.nodoc
    var hfs:hdfsFS;
    @chpldoc.nodoc
    var refCount: atomic int;

    /* nameNode is the name node hostname, can be 'default'
       port is the port, can be '0' for default behavior */
    @chpldoc.nodoc
    proc init(nameNode: string="default", port:int=0) {
      if verbose then
        writeln("hdfsConnect");

      this.nameNode = nameNode;
      this.port = port;
      this.hfs = hdfsConnect(this.nameNode.c_str(), this.port.safeCast(uint(16)));
      init this;
      refCount.write(1);
    }
    @chpldoc.nodoc
    proc deinit() {
      if verbose then
        writeln("hdfsDisconnect");
      if this.hfs != nil then
        hdfsDisconnect(this.hfs);
    }
    @chpldoc.nodoc
    proc retain() {
      refCount.add(1);
    }
    // should deallocate if the returned count is 0
    @chpldoc.nodoc
    proc release() {
      var oldValue = refCount.fetchSub(1);
      return oldValue - 1;
    }

    /*

      Open an HDFS file stored at a particular path.  Note that once the file is
      open, you will need to use :proc:`IO.file.reader` or :proc:`IO.file.writer`
      to create a channel to actually perform I/O operations.

      :arg path: which file to open (for example, "some/file.txt").
      :arg ioMode: specify whether to open the file for reading or writing and whether or not to create the file if it doesn't exist.  See :type:`IO.ioMode`.
      :arg flags: flags to pass to the HDFS open call. Uses flags appropriate for ``mode`` if not provided.
      :arg bufferSize: buffer size to pass to the HDFS open call.  Uses the HDFS default value if not provided.
      :arg replication: replication factor to pass to the HDFS open call.  Uses the HDFS default value if not provided.
      :arg blockSize: blockSize to pass to the HDFS open call.  Uses the HDFS default value if not provided.
      :throws SystemError: If opening the file fails.
     */
    proc open(path:string, mode:ioMode,
              in flags:c_int = 0, // default to based on mode
              bufferSize:c_int = 0,    // 0 -> use hdfs default value
              replication:c_short = 0, // 0 -> use hdfs default value
              blockSize:tSize = 0      // 0 -> use hdfs default value
             ) throws {
      if flags == 0 {
        // set flags based upon ioMode
        select mode {
          when ioMode.r {
            flags |= O_RDONLY;
          }
          when ioMode.cw {
            flags |= O_CREAT | O_TRUNC | O_WRONLY;
          }
          when ioMode.rw {
            flags |= O_RDWR;
          }
          when ioMode.cwr {
            flags |= O_CREAT | O_TRUNC | O_RDWR;
          }
        }
      }

      if verbose then
        writeln("hdfsOpenFile");
      var hfile = hdfsOpenFile(this.hfs, path.localize().c_str(),
                               flags, bufferSize, replication, blockSize);

      if verbose then
        writeln("after hdfsOpenFile");

      if hfile == nil {
        throw createSystemError(qio_mkerror_errno(), "in hdfsOpenFile");
      }

      // Create an HDFSFile and return the QIO file containing it
      // this initializer bumps the reference count to this
      var fl = new unmanaged HDFSFile(this:unmanaged, hfile, path);

      var ret: file;
      try {
        ret = openplugin(fl, mode, seekable=true, defaultIOStyleInternal());
      } catch e {
        fl.close();
        delete fl;
        throw e;
      }

      if verbose then
        writeln("after openplugin");

      return ret;
    }
  }

  @chpldoc.nodoc
  class HDFSFile : QioPluginFile {
    var fs: unmanaged HDFSFileSystem;
    var hfile: hdfsFile;
    var path: string;

    proc init(fs:unmanaged HDFSFileSystem, hfile:hdfsFile, path:string) {
      if verbose then
        writeln("HDFSFile.init");

      this.fs = fs;
      this.fs.retain(); // bump reference count
      this.hfile = hfile;
      this.path = path;
    }

    override proc setupChannel(out pluginChannel:unmanaged QioPluginChannel?,
                        start:int(64),
                        end:int(64),
                        qioChannelPtr:qio_channel_ptr_t):errorCode {
      if verbose then
        writeln("HDFSFile.setupChannel");

      var hdfsch = new unmanaged HDFSChannel(this:unmanaged, qioChannelPtr);
      pluginChannel = hdfsch;
      return 0;
    }

    override proc filelength(out length:int(64)):errorCode {
      if verbose then
        writeln("HDFSFile.filelength path=", path);
      var fInfoPtr = hdfsGetPathInfo(fs.hfs, path.c_str());
      if fInfoPtr == nil {
        return EINVAL;
      }
      length = fInfoPtr.deref().mSize;
      hdfsFreeFileInfo(fInfoPtr, 1);
      if verbose then
        writeln("HDFSFile.filelength length=", length);
      return 0;
    }
    override proc getpath(out path:c_ptr(uint(8)), out len:int(64)):errorCode {
      if verbose then
        writeln("HDFSFile.getpath path=", this.path);
      path = qio_strdup(this.path.c_str()):c_ptr(uint(8));
      len = this.path.size;
      if verbose then
        writeln("HDFSFile.getpath returning ", (path:string, len));
      return 0;
    }

    override proc fsync():errorCode {
      if verbose then
        writeln("HDFSFile.fsync");
      var rc = hdfsFlush(fs.hfs, hfile);
      if rc < 0 then
        return qio_mkerror_errno();
      return 0;
    }
    override proc getChunk(out length:int(64)):errorCode {
      if verbose then
        writeln("HDFSFile.getChunk");
      var fInfoPtr = hdfsGetPathInfo(fs.hfs, path.c_str());
      if fInfoPtr == nil then
        return EINVAL;
      length = fInfoPtr.deref().mBlockSize;
      hdfsFreeFileInfo(fInfoPtr, 1);
      return 0;
    }
    override proc getLocalesForRegion(start:int(64), end:int(64), out
        localeNames:c_ptr(c_ptrConst(c_char)), ref nLocales:int(64)):errorCode {
      if verbose then
        writeln("HDFSFile.getLocalesForRegion");
      return ENOSYS;
    }

    override proc close():errorCode {
      if verbose then
        writeln("hdfsCloseFile");

      var err:errorCode = 0;
      var rc = hdfsCloseFile(fs.hfs, hfile);
      if rc != 0 then
        err = qio_mkerror_errno();

      var count = fs.release();
      if count == 0 {
        delete fs;
      }
      return 0;
    }
  }

  @chpldoc.nodoc
  class HDFSChannel : QioPluginChannel {
    var file: unmanaged HDFSFile;
    var qio_ch:qio_channel_ptr_t;

    proc init(file: unmanaged HDFSFile, qio_ch:qio_channel_ptr_t) {
      if verbose then
        writeln("HDFSChannel.init");

      this.file = file;
      this.qio_ch = qio_ch;
    }

    override proc readAtLeast(amt:int(64)):errorCode {
      if verbose then
        writeln("HDFSChannel.readAtLeast");

      if amt == 0 then
        return 0;

      var err:errorCode = 0;

      var remaining = amt;
      while remaining > 0 {
        var ptr:c_ptr(void) = nil;
        var len = 0:c_ssize_t;
        var offset = 0;
        err = qio_channel_get_allocated_ptr_unlocked(qio_ch, amt, ptr, len, offset);
        if err then
          return err;
        if ptr == nil || len == 0 then
          return EINVAL;

        if len >= max(int(32)) then
          len = max(int(32));

        if verbose then
          writeln("hdfsPread offset=", offset, " len=", len);

        // Now read len bytes into ptr
        var rc = hdfsPread(file.fs.hfs, file.hfile, offset, ptr, len:int(32));
        if rc == 0 {
          // end of file
          return chpl_macro_int_EEOF():errorCode;
        } else if rc < 0 {
          // error
          return qio_mkerror_errno();
        }
        // otherwise, rc is the number of bytes read.
        // advance the available region by the amount read
        qio_channel_advance_available_end_unlocked(qio_ch, rc);
        remaining -= rc:int(64);
      }

      return 0;
    }
    override proc write(amt:int(64)):errorCode {
      if verbose then
        writeln("HDFSChannel.write");

      var err:errorCode = 0;
      var remaining = amt;
      while remaining > 0 {
        var ptr:c_ptr(void) = nil;
        var len = 0:c_ssize_t;
        var offset = 0;
        err = qio_channel_get_write_behind_ptr_unlocked(qio_ch, ptr, len, offset);
        if err then
          return err;
        if ptr == nil || len == 0 then
          return EINVAL;

        len = min(len, amt.safeCast(c_ssize_t));
        if len >= max(int(32)) then
          len = max(int(32));

        if verbose then
          writeln("hdfsWrite");

        var rc = hdfsWrite(file.fs.hfs, file.hfile, ptr, len:int(32));
        if rc < 0 then
          return qio_mkerror_errno();

        qio_channel_advance_write_behind_unlocked(qio_ch, rc);
        remaining -= rc:int(64);
      }
      return 0;
    }
    override proc close():errorCode {
      return 0;
    }
  }

} /* end of module */
