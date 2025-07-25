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

/* Supports features matching operating system interfaces.

   This module provides Chapel declarations for the constants, types,
   and functions defined by various operating systems' programmatic
   interfaces.  It is not complete for any operating system, but does
   define those things that have been needed so far in implementing
   other Chapel modules and user programs.  It is expected to grow as
   needed.  In all respects (naming, capitalization, types, semantics)
   the symbols defined here should match their corresponding operating
   system definitions to the extent Chapel can do so.  For documentation
   on these symbols, please see the operating system manual pages.

 */
module OS {
  /* Support for features matching the POSIX programming interface.

     The ``OS.POSIX`` module specifically provides POSIX.1-2017.  That standard
     can be found at <https://pubs.opengroup.org/onlinepubs/9699919799/>.

     There is one unavoidable difference between POSIX and ``OS.POSIX``.
     POSIX defines a function named ``select()``, which ``OS.POSIX``
     could not use because ``select`` is itself a Chapel keyword.
  */
  module POSIX {
    public use CTypes;

    //
    // sys/types.h
    //
    /*
       Explicit conversions between ``blkcnt_t`` and ``c_int`` are
       also defined, to support usability.
    */
    extern type blkcnt_t;
    @chpldoc.nodoc
    inline operator :(x:blkcnt_t, type t:c_int) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:c_int, type t:blkcnt_t) do
      return __primitive("cast", t, x);

    /*
       Explicit conversions between ``blksize_t`` and ``c_int`` are
       also defined, to support usability.
    */
    extern type blksize_t;
    @chpldoc.nodoc
    inline operator :(x:blksize_t, type t:c_int) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:c_int, type t:blksize_t) do
      return __primitive("cast", t, x);

    /*
       Explicit conversions between ``dev_t`` and ``c_int`` are
       also defined, to support usability.
    */
    extern type dev_t;
    @chpldoc.nodoc
    inline operator :(x:dev_t, type t:c_int) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:c_int, type t:dev_t) do
      return __primitive("cast", t, x);

    /*
       Explicit conversions between ``gid_t`` and ``c_int`` are
       also defined, to support usability.
    */
    extern type gid_t;
    @chpldoc.nodoc
    inline operator :(x:gid_t, type t:c_int) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:c_int, type t:gid_t) do
      return __primitive("cast", t, x);

    /*
       Explicit conversions between ``ino_t`` and ``c_uint`` are
       also defined, to support usability.
    */
    extern type ino_t;
    @chpldoc.nodoc
    inline operator :(x:ino_t, type t:c_uint) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:c_uint, type t:ino_t) do
      return __primitive("cast", t, x);

    /*
       Bitwise-AND and bitwise-OR operators are defined on ``mode_t``
       operands, to support querying and constructing mode values.
       Explicit conversions between ``mode_t`` and ``c_int`` are
       also defined, to support usability.
    */
    extern type mode_t;
    @chpldoc.nodoc
    inline operator &(a: mode_t, b: mode_t) do
      return (a:c_int & b:c_int):mode_t;
    @chpldoc.nodoc
    inline operator |(a: mode_t, b: mode_t) do
      return (a:c_int | b:c_int):mode_t;
    @chpldoc.nodoc
    inline operator :(x:mode_t, type t:c_int) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:c_int, type t:mode_t) do
      return __primitive("cast", t, x);

    /*
       Explicit conversions between ``nlink_t`` and ``c_int`` are
       also defined, to support usability.
    */
    extern type nlink_t;
    @chpldoc.nodoc
    inline operator :(x:nlink_t, type t:c_int) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:c_int, type t:nlink_t) do
      return __primitive("cast", t, x);

    /*
       Explicit conversions between ``off_t`` and ``c_int`` and
       ``off_t`` and integral types are also defined, to support usability.
    */
    extern type off_t;
    @chpldoc.nodoc
    inline operator :(x:off_t, type t:c_int) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:c_int, type t:off_t) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:off_t, type t:integral) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:integral, type t:off_t) do
      return __primitive("cast", t, x);


    /*
       Explicit conversions between ``suseconds_t`` and ``c_int`` are
       also defined, to support usability.
    */
    extern type suseconds_t;
    @chpldoc.nodoc
    inline operator :(x:integral, type t:suseconds_t) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:suseconds_t, type t:integral) do
      return __primitive("cast", t, x);

    /*
       Explicit conversions between ``time_t`` and ``c_int`` are
       also defined, to support usability.
    */
    extern type time_t;
    @chpldoc.nodoc
    inline operator :(x:integral, type t:time_t) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:time_t, type t:integral) do
      return __primitive("cast", t, x);

    /*
       Explicit conversions between ``uid_t`` and ``c_int`` are
       also defined, to support usability.
    */
    extern type uid_t;
    @chpldoc.nodoc
    inline operator :(x:uid_t, type t:c_int) do
      return __primitive("cast", t, x);
    @chpldoc.nodoc
    inline operator :(x:c_int, type t:uid_t) do
      return __primitive("cast", t, x);

    //
    // time.h (pre-decl for struct_timespec, needed in sys/stat.h)
    //
    /* The structure ``timespec`` from ``time.h``. */
    extern "struct timespec" record struct_timespec {
      /* Seconds since the epoch, Jan. 1, 1970 */
      var tv_sec:time_t;
      /* Nanoseconds */
      var tv_nsec:c_long;
    }

    //
    // errno.h
    //

    // The descriptions of these were created by combining the FreeBSD
    // manual and the linux manuals here:
    // http://www.freebsd.org/cgi/man.cgi?query=errno&apropos=0&sektion=0&manpath=FreeBSD+10.1-RELEASE&arch=default&format=html
    // http://linux.die.net/man/3/errno
    // Verified that the POSIX annotations are correct with POSIX.1-2008:
    // http://pubs.opengroup.org/onlinepubs/9699919799/basedefs/errno.h.html
    // Note that these error descriptions are reproduced here for convenience
    // only. These descriptions aren't the authority on the matter.


    /* Argument list too long. The number of bytes used for the argument and
       environment list of the new process exceeded the current limit.
     */
    extern const E2BIG:c_int;

    /* Permission denied. An attempt was made to access a file in a way
       forbidden by its file access permissions. (POSIX.1)
     */
    extern const EACCES:c_int;

    /* Address already in use. Only one usage of each address is normally
       permitted.
     */
    extern const EADDRINUSE:c_int;

    /* Can't assign requested address. Normally results from an attempt to
       create a socket with an address not on this machine.
     */
    extern const EADDRNOTAVAIL:c_int;

    /* Address family not supported by protocol family. An address incompatible
       with the requested protocol was used. For example, you should not
       necessarily expect to be able to use NS addresses with ARPA Internet
       protocols.
     */
    extern const EAFNOSUPPORT:c_int;

    /* Resource temporarily unavailable. This is a temporary condition and
       later calls to the same routine may complete normally.
     */
    extern const EAGAIN:c_int;

    /* Operation already in progress. An operation was attempted on a
       non-blocking object that already had an operation in progress.
     */
    extern const EALREADY:c_int;

    /* Bad file descriptor. A file descriptor argument was out of range,
       referred to no open file, or a read (write) request was made to a file
       that was only open for writing (reading).
     */
    extern const EBADF:c_int;

    /* Bad message. A corrupted message was detected. (POSIX.1) */
    extern const EBADMSG:c_int;

    /* Device or resource busy. An attempt to use a system resource which was
       in use at the time in a manner which would have conflicted with the
       request.
     */
    extern const EBUSY:c_int;

    /* Operation canceled. The scheduled operation was canceled. (POSIX.1) */
    extern const ECANCELED:c_int;

    /* No child processes. A wait or waitpid system call was executed by a
       process that had no existing or unwaited-for child processes. (POSIX.1)
     */
    extern const ECHILD:c_int;

    /* Software caused connection abort. A connection abort was caused internal
       to your host machine.
     */
    extern const ECONNABORTED:c_int;

    /* Connection refused. No connection could be made because the target
       machine actively refused it. This usually results from trying to
       connect to a service that is inactive on the foreign host.
     */
    extern const ECONNREFUSED:c_int;

    /* Connection reset by peer. A connection was forcibly closed by a peer.
       This normally results from a loss of the connection on the remote
       socket due to a timeout or a reboot.
     */
    extern const ECONNRESET:c_int;

    /* Resource deadlock avoided. An attempt was made to lock a system resource
       that would have resulted in a deadlock situation. (POSIX.1)
     */
    extern const EDEADLK:c_int;

    /* Destination address required. A required address was omitted from an
       operation on a socket.
     */
    extern const EDESTADDRREQ:c_int;

    /* Numerical argument out of domain. A numerical input argument was outside
       the defined domain of the mathematical function.
     */
    extern const EDOM:c_int;

    /* Disc quota exceeded. A write system call to an ordinary file, the
       creation of a directory or symbolic link, or the creation of a directory
       entry failed because the user's quota of disk blocks was exhausted, or
       the allocation of an inode for a newly created file failed because the
       user's quota of inodes was exhausted.
     */
    extern const EDQUOT:c_int;

    /* File exists. An existing file was mentioned in an inappropriate context,
       for instance, as the new link name in a link system call.
     */
    extern const EEXIST:c_int;

    /* Bad address. The system detected an invalid address in attempting to
       use an argument of a call.
     */
    extern const EFAULT:c_int;

    /* File too large. The size of a file exceeded the maximum. */
    extern const EFBIG:c_int;

    /* No route to host. A socket operation was attempted to an unreachable
       host.
     */
    extern const EHOSTUNREACH:c_int;

    /* Identifier removed. An IPC identifier was removed while the current
       process was waiting on it.
     */
    extern const EIDRM:c_int;

    /*
       Illegal byte sequence. While decoding a multibyte character the function
       came along an invalid or an incomplete sequence of bytes or the given
       wide character is invalid.

       This error might be returned for example in the case of an illegal UTF-8
       byte sequence.
     */
    extern const EILSEQ:c_int;

    /* Operation now in progress. An operation that takes a long time to
       complete (such as a connect system call) was attempted on a
       non-blocking object.
     */
    extern const EINPROGRESS:c_int;

    /* Interrupted system call. An asynchronous signal (such as SIGINT or
       SIGQUIT) was caught by the process during the execution of an
       interruptible function.  If the signal handler performs a normal return,
       the interrupted system call will seem to have returned the error
       condition.
     */
    extern const EINTR:c_int;

    /* Invalid argument. Some invalid argument was supplied. (For example,
       specifying an undefined signal to a signal system call or a kill system
       call).
     */
    extern const EINVAL:c_int;

   /* Input/output error. Some physical input or output error occurred. This
      error will not be reported until a subsequent operation on the same file
      descriptor and may be lost (over written) by any subsequent errors.
     */
    extern const EIO:c_int;

    /* Socket is already connected. A connect system call was made on an
       already connected socket; or, a sendto or sendmsg system call on a
       connected socket specified a destination when already connected.
     */
    extern const EISCONN:c_int;

    /* Is a directory. An attempt was made to open a directory with write mode
       specified.
     */
    extern const EISDIR:c_int;

    /* Too many levels of symbolic links. A path name lookup involved more than
       32 (MAXSYMLINKS) symbolic links.
     */
    extern const ELOOP:c_int;

    /* Too many open files. Maximum number of file descriptors
       allowable in the process has been reached and requests for an
       open cannot be satisfied until at least one has been
       closed. The getdtablesize system call will obtain the current
       limit.
     */
    extern const EMFILE:c_int;

    /* Too many links. Maximum allowable hard links to a single file has been
       exceeded.
     */
    extern const EMLINK:c_int;

    /* Message too long. A message sent on a socket was larger than the
       internal message buffer or some other network limit.
     */
    extern const EMSGSIZE:c_int;

    /* Multihop attempted.
     */
    extern const EMULTIHOP:c_int;

    /* File name too long. A component of a path name exceeded {NAME_MAX}
       characters, or an entire path name exceeded {PATH_MAX} characters.
     */
    extern const ENAMETOOLONG:c_int;

    /* Network is down. A socket operation encountered a dead network. */
    extern const ENETDOWN:c_int;

    /* Network dropped connection on reset. The host you were connected to
       crashed and rebooted.
     */
    extern const ENETRESET:c_int;

    /* Network is unreachable. A socket operation was attempted to an
       unreachable network.
     */
    extern const ENETUNREACH:c_int;

    /* Too many open files in system. Maximum number of open files allowable on
       the system has been reached and requests for an open cannot be satisfied
       until at least one has been closed.
     */
    extern const ENFILE:c_int;

    /* No buffer space available. An operation on a socket or pipe was not
       performed because the system lacked sufficient buffer space or because a
       queue was full.
     */
    extern const ENOBUFS:c_int;

    /* Operation not supported by device. An attempt was made to apply an
       inappropriate function to a device, for example, trying to read a
       write-only device such as a printer.
     */
    extern const ENODEV:c_int;

    /* No such file or directory. A component of a specified pathname did not
       exist, or the pathname was an empty string.
     */
    extern const ENOENT:c_int;

    /* Exec format error. A request was made to execute a file that, although
       it has the appropriate permissions, was not in the format required for
       an executable file.
     */
    extern const ENOEXEC:c_int;

    /* No locks available. A system-imposed limit on the number of simultaneous
      file locks was reached.
     */
    extern const ENOLCK:c_int;

    /* Link has been severed.
     */
    extern const ENOLINK:c_int;

    /* Cannot allocate memory. The new process image required more memory than
       was allowed by the hardware or by system-imposed memory management
       constraints. A lack of swap space is normally temporary; however, a lack
       of core is not. Soft limits may be increased to their corresponding hard
       limits.
     */
    extern const ENOMEM:c_int;

    /* No message of desired type. An IPC message queue does not contain a
       message of the desired type, or a message catalog does not contain the
       requested message.
     */
    extern const ENOMSG:c_int;

    /* Protocol not available. A bad option or level was specified in a
       getsockopt or setsockopt system call.
     */
    extern const ENOPROTOOPT:c_int;

    /* No space left on device. A write system call to an ordinary file, the
       creation of a directory or symbolic link, or the creation of a directory
       entry failed because no more disk blocks were available on the file
       system, or the allocation of an inode for a newly created file failed
       because no more inodes were available on the file system.
     */
    extern const ENOSPC:c_int;

    /* Function not implemented. Attempted a system call that is not available
       on this system.
     */
    extern const ENOSYS:c_int;


    /* Socket is not connected. An request to send or receive data was
       disallowed because the socket was not connected and (when
       sending on a datagram socket) no address was
       supplied.
     */
    extern const ENOTCONN:c_int;

    /* Not a directory. A component of the specified pathname existed, but it
       was not a directory, when a directory was expected.
     */
    extern const ENOTDIR:c_int;

    /* Directory not empty. A directory with entries other than '.' and '..'
       was supplied to a remove directory or rename call.
     */
    extern const ENOTEMPTY:c_int;

    /* State not recoverable. */
    extern const ENOTRECOVERABLE:c_int;

    /* Socket operation on non-socket. */
    extern const ENOTSOCK:c_int;

    /* Operation not supported. The attempted operation is not supported for
       the type of object referenced. Usually this occurs when a file
       descriptor refers to a file or socket that cannot support this
       operation, for example, trying to accept a connection on a datagram
       socket.
     */
    extern const ENOTSUP:c_int;

    /* Inappropriate ioctl for device. A control function (e.g. ioctl system
       call) was attempted for a file or special device for which the operation
       was inappropriate.
     */
    extern const ENOTTY:c_int;

    /* Device not configured. Input or output on a special file referred to a
       device that did not exist, or made a request beyond the limits of the
       device. This error may also occur when, for example, a tape drive is
       not online or no disk pack is loaded on a drive.
     */
    extern const ENXIO:c_int;

    /* Operation not supported. The attempted operation is not supported for
       the type of object referenced. Usually this occurs when a file
       descriptor refers to a file or socket that cannot support this
       operation, for example, trying to accept a connection on a datagram
       socket.
     */
    extern const EOPNOTSUPP:c_int;

    /* Value too large to be stored in data type. A numerical result of the
       function was too large to be stored in the caller provided space.
     */
    extern const EOVERFLOW:c_int;

    /* Owner died. */
    extern const EOWNERDEAD:c_int;

    /* Operation not permitted. An attempt was made to perform an operation
       limited to processes with appropriate privileges or to the owner of a
       file or other resources.
     */
    extern const EPERM:c_int;

    /* Broken pipe. A write on a pipe, socket or FIFO for which there is no
       process to read the data.
     */
    extern const EPIPE:c_int;

    /* Protocol error. A device or socket encountered an unrecoverable
       protocol error.
     */
    extern const EPROTO:c_int;

    /* Protocol not supported. The protocol has not been configured into the
       system or no implementation for it exists.
     */
    extern const EPROTONOSUPPORT:c_int;

    /* Protocol wrong type for socket. A protocol was specified that does not
       support the semantics of the socket type requested. For example, you
       cannot use the ARPA Internet UDP protocol with type SOCK_STREAM.
     */
    extern const EPROTOTYPE:c_int;

    /* Result too large. A numerical result of the function was too large to
       fit in the available space (perhaps exceeded precision).
     */
    extern const ERANGE:c_int;

    /* Read-only file system. An attempt was made to modify a file or directory
       on a file system that was read-only at the time.
     */
    extern const EROFS:c_int;

    /* Illegal seek. An lseek system call was issued on a socket, pipe or FIFO.
     */
    extern const ESPIPE:c_int;

    /* No such process. No process could be found corresponding to that
       specified by the given process ID.
     */
    extern const ESRCH:c_int;

    /* Stale NFS file handle. An attempt was made to access an open file (on an
       NFS file system) which is now unavailable as referenced by the file
       descriptor. This may indicate the file was deleted on the NFS server or
       some other catastrophic event occurred.
     */
    extern const ESTALE:c_int;

    /* Operation timed out. A connect or send system call failed because the
       connected party did not properly respond after a period of time (The
       timeout period is dependent on the communication protocol).
     */
    extern const ETIMEDOUT:c_int;

    /* Text file busy. The new process was a pure procedure (shared text) file
       which was open for writing by another process, or while the pure
       procedure file was being executed an open system call requested write
       access.
     */
    extern const ETXTBSY:c_int;

    /* Operation would block (may be same value as EAGAIN).
     */
    extern const EWOULDBLOCK:c_int;

    /* Cross-device link. A hard link to a file on another file system was
       attempted.
     */
    extern const EXDEV:c_int;

    /*
       POSIX says that errno is a "modifiable lvalue of type int", but
       for now we only support reading from it, not assigning to it.
    */
    inline proc errno:c_int {
      extern proc chpl_os_posix_errno_val():c_int;
      return chpl_os_posix_errno_val();
    }

    // Signals as required by POSIX.1-2008, 2013 edition
    // See note below about signals intentionally not included

    /* Abort Signal (from abort(3))
    */
    extern const SIGABRT: c_int;
    /* Timer Signal (from alarm(2))
    */
    extern const SIGALRM: c_int;
    /* Bus error (bad memory access)
    */
    extern const SIGBUS: c_int;
    /* Child stopped or terminated
    */
    extern const SIGCHLD: c_int;
    /* Continue if stopped
    */
    extern const SIGCONT: c_int;
    /*Floating-point exception
    */
    extern const SIGFPE: c_int;
    /* Hangup detected on controlling terminal
    or death of controlling process
    */
    extern const SIGHUP: c_int;
    /* Illegal Instruction
    */
    extern const SIGILL: c_int;
    /* Interrupt from keyboard
    */
    extern const SIGINT: c_int;
    /* Kill signal
    */
    extern const SIGKILL: c_int;
    /* Broken pipe: write to pipe with no readers
    */
    extern const SIGPIPE: c_int;
    /* Quit from keyboard
    */
    extern const SIGQUIT: c_int;
    /* Invalid memory reference
    */
    extern const SIGSEGV: c_int;
    /* Stop process
    */
    extern const SIGSTOP: c_int;
    /* Termination signal
    */
    extern const SIGTERM: c_int;
    /* Trace/breakpoint trap
    */
    extern const SIGTRAP: c_int;
    /* Stop typed at terminal
    */
    extern const SIGTSTP: c_int;
    /* Terminal input for background process
    */
    extern const SIGTTIN: c_int;
    /* Terminal output for background process
    */
    extern const SIGTTOU: c_int;
    /* Urgent condition on socket
    */
    extern const SIGURG: c_int;
    /* User-defined signal 1
    */
    extern const SIGUSR1: c_int;
    /* User-defined signal 2
    */
    extern const SIGUSR2: c_int;
    /* CPU time limit exceeded
    */
    extern const SIGXCPU: c_int;
    /* File size limit exceeded
    */
    extern const SIGXFSZ: c_int;

    // These signals are not strictly required by POSIX.1.2008 2013 edition
    // and so should not be included here:

    // SIGPOLL is Obsolescent and optional as part of XSI STREAMS
    // SIGPROF is Obsolescent and optional as part of XSI STREAMS
    // SIGSYS is optional as part of X/Open Systems Interface
    // SIGVTALRM is optional as part of X/Open Systems Interface

    //
    // fcntl.h
    //
    /* Mask for file access modes. */
    extern const O_ACCMODE:c_int;
    /* Set append mode. */
    extern const O_APPEND:c_int;
    /*
      Sets the ``FD_CLOEXEC`` flag of new descriptor to close it after
      execution of an ``exec`` function.
    */
    extern const O_CLOEXEC:c_int;
    /* Create file if it does not exist. */
    extern const O_CREAT:c_int;
    /* Fail if file is a non-directory file. */
    extern const O_DIRECTORY:c_int;
    /* Write according to synchronized I/O data integrity completion. */
    extern const O_DSYNC:c_int;
    /* Exclusive use flag. */
    extern const O_EXCL:c_int;
    /* Do not assign controlling terminal. */
    extern const O_NOCTTY:c_int;
    /* Do not follow symbolic links. */
    extern const O_NOFOLLOW:c_int;
    /* Non-blocking mode. */
    extern const O_NONBLOCK:c_int;
    /* Open for reading only. */
    extern const O_RDONLY:c_int;
    /* Open for reading and writing */
    extern const O_RDWR:c_int;
    /* Write according to synchronized I/O file integrity completion. */
    extern const O_SYNC:c_int;
    /* Truncate flag. */
    extern const O_TRUNC:c_int;
    /* Open for writing only. */
    extern const O_WRONLY:c_int;
    // Note: O_EXEC, O_SEARCH, O_TTY_INIT
    // are documented in POSIX but don't seem to exist on linux
    // Note: O_RSYNC
    // is documented in POSIX but doesn't seem to exist on Mac OS

    /* Create a new file or rewrite an existing file. */
    extern proc creat(path:c_ptrConst(c_char), mode:mode_t = 0):c_int;
    /* Open a file. */
    inline proc open(path:c_ptrConst(c_char), oflag:c_int, mode:mode_t = 0:mode_t)
                  :c_int {
      extern proc chpl_os_posix_open(path:c_ptrConst(c_char), oflag:c_int, mode:mode_t)
                    :c_int;
      return chpl_os_posix_open(path, oflag, mode);
    }

    //
    // stdlib.h
    //
    /* Get the value of an environment variable. */
    extern proc getenv(name:c_ptrConst(c_char)):c_ptr(c_char);

    //
    // string.h
    //
    /* Get the error message string for ``errnum`` */
    extern proc strerror(errnum:c_int):c_ptrConst(c_char);
    /* Get the length of the null-terminated string. */
    extern proc strlen(s:c_ptrConst(c_char)):c_size_t;

    //
    // sys/select.h
    //
    /* Maximum number of file descriptors that :type:`fd_set` can hold. */
    extern const FD_SETSIZE:c_int;

    /* Contains a fixed amount of file descriptors. */
    extern record fd_set {}

    /* Clears the bit for the file descriptor ``fd`` in ``fdset``. */
    proc FD_CLR(fd:c_int, fdset:c_ptr(fd_set)) {
      extern proc chpl_os_posix_FD_CLR(fd:c_int, fdset:c_ptr(fd_set));
      chpl_os_posix_FD_CLR(fd, fdset);
    }

    /* Checks if the bit for the file descriptor ``fd`` is set in ``fdset``. */
    proc FD_ISSET(fd:c_int, fdset:c_ptr(fd_set)):c_int {
      extern proc chpl_os_posix_FD_ISSET(fd:c_int, fdset:c_ptr(fd_set)):c_int;
      return chpl_os_posix_FD_ISSET(fd, fdset);
    }

    /* Sets the bit for the file descriptor ``fd`` in ``fdset``. */
    proc FD_SET(fd:c_int, fdset:c_ptr(fd_set)) {
      extern proc chpl_os_posix_FD_SET(fd:c_int, fdset:c_ptr(fd_set));
      chpl_os_posix_FD_SET(fd, fdset);
    }

    /* Initializes all file descriptors in ``fdset`` to zero. */
    proc FD_ZERO(fdset:c_ptr(fd_set)) {
      extern proc chpl_os_posix_FD_ZERO(fdset:c_ptr(fd_set));
      chpl_os_posix_FD_ZERO(fdset);
    }

    // No way around this -- 'select' is a keyword in Chapel.
    /*
      Indicates which of the specified file descriptors is ready for reading
      or writing, or has an error condition pending. If no file descriptors are
      ready, the procedure blocks until the ``timeout``.
    */
    extern 'select' proc select_posix(nfds:c_int,
                                      readfds:c_ptr(fd_set),
                                      writefds:c_ptr(fd_set),
                                      errorfds:c_ptr(fd_set),
                                      timeout:c_ptr(struct_timeval)):c_int;

    //
    // sys/stat.h
    //
    /* Shorthand for (:proc:`S_IRUSR` | :proc:`S_IWUSR` | :proc:`S_IXUSR`). */
    inline proc S_IRWXU:mode_t {
      extern proc chpl_os_posix_S_IRWXU():mode_t;
      return chpl_os_posix_S_IRWXU();
    }
    /* Read permission bit for the file's owner. */
    inline proc S_IRUSR:mode_t {
      extern proc chpl_os_posix_S_IRUSR():mode_t;
      return chpl_os_posix_S_IRUSR();
    }
    /* Write permission bit for the file's owner. */
    inline proc S_IWUSR:mode_t {
      extern proc chpl_os_posix_S_IWUSR():mode_t;
      return chpl_os_posix_S_IWUSR();
    }
    /* Execute permission bit for the file's owner. */
    inline proc S_IXUSR:mode_t {
      extern proc chpl_os_posix_S_IXUSR():mode_t;
      return chpl_os_posix_S_IXUSR();
    }

    /* Shorthand for (:proc:`S_IRGRP` | :proc:`S_IWGRP` | :proc:`S_IXGRP`). */
    inline proc S_IRWXG:mode_t {
      extern proc chpl_os_posix_S_IRWXG():mode_t;
      return chpl_os_posix_S_IRWXG();
    }
    /* Read permission bit for the file's group. */
    inline proc S_IRGRP:mode_t {
      extern proc chpl_os_posix_S_IRGRP():mode_t;
      return chpl_os_posix_S_IRGRP();
    }
    /* Write permission bit for the file's group. */
    inline proc S_IWGRP:mode_t {
      extern proc chpl_os_posix_S_IWGRP():mode_t;
      return chpl_os_posix_S_IWGRP();
    }
    /* Execute permission bit for the file's group. */
    inline proc S_IXGRP:mode_t {
      extern proc chpl_os_posix_S_IXGRP():mode_t;
      return chpl_os_posix_S_IXGRP();
    }

     /* Shorthand for (:proc:`S_IROTH` | :proc:`S_IWOTH` | :proc:`S_IXOTH`). */
    inline proc S_IRWXO:mode_t {
      extern proc chpl_os_posix_S_IRWXO():mode_t;
      return chpl_os_posix_S_IRWXO();
    }
    /* Read permission bit for others. */
    inline proc S_IROTH:mode_t {
      extern proc chpl_os_posix_S_IROTH():mode_t;
      return chpl_os_posix_S_IROTH();
    }
    /* Write permission bit for others. */
    inline proc S_IWOTH:mode_t {
      extern proc chpl_os_posix_S_IWOTH():mode_t;
      return chpl_os_posix_S_IWOTH();
    }
    /* Execute permission bit for others. */
    inline proc S_IXOTH:mode_t {
      extern proc chpl_os_posix_S_IXOTH():mode_t;
      return chpl_os_posix_S_IXOTH();
    }

    /* Set user ID on execute bit. */
    inline proc S_ISUID:mode_t {
      extern proc chpl_os_posix_S_ISUID():mode_t;
      return chpl_os_posix_S_ISUID();
    }
    /* Set group ID on execute bit. */
    inline proc S_ISGID:mode_t {
      extern proc chpl_os_posix_S_ISGID():mode_t;
      return chpl_os_posix_S_ISGID();
    }
    /* Sticky bit */
    inline proc S_ISVTX:mode_t {
      extern proc chpl_os_posix_S_ISVTX():mode_t;
      return chpl_os_posix_S_ISVTX();
    }

    /* A Chapel version of the POSIX structure ``stat``, which contains common
    fields. This should be used with :proc:`stat`.
    */
    extern 'struct chpl_os_posix_struct_stat' record struct_stat {
      /* Device. */
      var st_dev:dev_t;
      /* File serial number. */
      var st_ino:ino_t;
      /* File mode. */
      var st_mode:mode_t;
      /* Link count. */
      var st_nlink:nlink_t;
      /* User ID of the file's owner. */
      var st_uid:uid_t;
      /* Group ID of the file's group. */
      var st_gid:gid_t;
      /* Device number, if device. */
      var st_rdev:dev_t;
      /* Size of file, in bytes. */
      var st_size:off_t;
      /* Last data access timestamp. */
      var st_atim:struct_timespec;
      /* Last data modification timestamp. */
      var st_mtim:struct_timespec;
      /* Last file status change timestamp. */
      var st_ctim:struct_timespec;
      /* Optimal block size for I/O. */
      var st_blksize:blksize_t;
      /* Number 512-byte blocks allocated. */
      var st_blocks:blkcnt_t;
    }

    /* Changes the mode of a file. */
    extern proc chmod(path:c_ptrConst(c_char), mode:mode_t):c_int;
    /* Get the status of a file, should be used with :record:`struct_stat`. */
    extern 'chpl_os_posix_stat' proc stat(path:c_ptrConst(c_char),
                                          buf:c_ptr(struct_stat)):c_int;

    //
    // sys/time.h
    //
    /* The structure ``timeval`` from ``sys/time.h``. */
    extern "struct timeval" record struct_timeval {
      /* Seconds since the epoch, Jan. 1, 1970 */
      var tv_sec:time_t;
      /* Nanoseconds */
      var tv_usec:suseconds_t;
    }

    @chpldoc.nodoc
    proc struct_timeval.init() {}

    @chpldoc.nodoc
    proc struct_timeval.init(tv_sec: integral, tv_usec: integral) {
      this.tv_sec = tv_sec:time_t;
      this.tv_usec = tv_usec:suseconds_t;
    }

    @chpldoc.nodoc
    proc struct_timeval.init(tv_sec: time_t, tv_usec: suseconds_t) {
      this.tv_sec = tv_sec;
      this.tv_usec = tv_usec;
    }

    /* The structure ``timezone`` from ``time.h``. */
    extern "struct timezone" record struct_timezone {
      /* Minutes west of Greenwich */
      var tz_minuteswest:c_int;
      /* Type of DST correction */
      var tz_dsttime:c_int;
    }

    /*
      Get the date and time, based on the timezone in ``tzp``.
      The result is stored in ``tp``.
    */
    extern proc gettimeofday(tp:c_ptr(struct_timeval),
                             tzp:c_ptr(struct_timezone)):c_int;

    //
    // time.h
    //
    /* The structure ``tm`` from ``time.h``. */
    extern "struct tm" record struct_tm {
      /* Seconds [0,60] (60 allows for leap seconds) */
      var tm_sec:c_int;
      /* Minutes [0,59] */
      var tm_min:c_int;
      /* Hour [0,23] */
      var tm_hour:c_int;
      /* Day of month [1,31] */
      var tm_mday:c_int;
      /* Month of year [0,11] */
      var tm_mon:c_int;
      /* Years since 1900 */
      var tm_year:c_int;
      /* Day of week [0,6] (Sunday =0) */
      var tm_wday:c_int;
      /* Day of year [0,365] */
      var tm_yday:c_int;
      /* Daylight Savings flag */
      var tm_isdst:c_int;
    }

    /* Get the date and time as a string. */
    extern proc asctime(timeptr:c_ptr(struct_tm)):c_ptr(c_char);
    /*
      Get the date and time as a string, using the given buffer.

      :returns: ``buf``
    */
    extern proc asctime_r(timeptr:c_ptr(struct_tm), buf:c_ptr(c_char))
                  :c_ptr(c_char);
    /* Convert the time to a local time. */
    extern proc localtime(timer:c_ptr(time_t)):c_ptr(struct_tm);
    /*
      Convert the time to a local time, storing the result in the given struct.

      :returns: ``result``
    */
    extern proc localtime_r(timer:c_ptr(time_t), result:c_ptr(struct_tm))
                  :c_ptr(struct_tm);
    /* Get the time. */
    extern proc time(tloc:c_ptr(time_t)):time_t;

    //
    // unistd.h
    //
    /* Close a file descriptor. */
    extern proc close(fildes:c_int):c_int;
    /* Create a pipe. */
    extern proc pipe(fildes:c_ptr(c_array(c_int, 2))):c_int;
    /* Read ``size`` bytes from a file descriptor into ``buf``. */
    extern proc read(fildes:c_int, buf:c_ptr(void), size:c_size_t):c_ssize_t;
    /* Write ``size`` bytes to file descriptor from ``buf``. */
    extern proc write(fildes:c_int, buf:c_ptr(void), size:c_size_t):c_ssize_t;

    /*
      Copies n potentially overlapping bytes from memory area src to memory
      area dest.

      This is a simple wrapper over the C ``memmove()`` function.

      :arg dest: the destination memory area to copy to
      :arg src: the source memory area to copy from
      :arg n: the number of bytes from src to copy to dest
    */
    pragma "fn synchronization free"
    inline proc memmove(dest:c_ptr(void), const src:c_ptr(void), n: c_size_t) {
      pragma "fn synchronization free"
      extern proc memmove(dest: c_ptr(void), const src: c_ptr(void), n: c_size_t);
      memmove(dest, src, n);
    }

    /*
      Copies n non-overlapping bytes from memory area src to memory
      area dest. Use :proc:`memmove` if memory areas do overlap.

      This is a simple wrapper over the C ``memcpy()`` function.

      :arg dest: the destination memory area to copy to
      :arg src: the source memory area to copy from
      :arg n: the number of bytes from src to copy to dest
    */
    pragma "fn synchronization free"
    inline proc memcpy(dest:c_ptr(void), const src:c_ptr(void), n: c_size_t) {
      pragma "fn synchronization free"
      extern proc memcpy(dest: c_ptr(void), const src: c_ptr(void), n: c_size_t);
      memcpy(dest, src, n);
    }

    /*
      Compares the first n bytes of memory areas s1 and s2

      This is a simple wrapper over the C ``memcmp()`` function.

      :returns: returns an integer less than, equal to, or greater than zero if
                the first n bytes of s1 are found, respectively, to be less than,
                to match, or be greater than the first n bytes of s2.
    */
    pragma "fn synchronization free"
    inline proc memcmp(const s1:c_ptr(void), const s2:c_ptr(void), n: c_size_t): int {
      pragma "fn synchronization free"
      extern proc memcmp(const s1: c_ptr(void), const s2: c_ptr(void), n: c_size_t): c_int;
      return memcmp(s1, s2, n).safeCast(int);
    }

    /*
      Fill bytes of memory with a particular byte value.

      This is a simple wrapper over the C ``memset()`` function.

      :arg s: the destination memory area to fill
      :arg c: the byte value to use
      :arg n: the number of bytes of s to fill

      :returns: ``s``
    */
    pragma "fn synchronization free"
    inline proc memset(s:c_ptr(void), c:integral, n: c_size_t) {
      pragma "fn synchronization free"
      extern proc memset(s: c_ptr(void), c: c_int, n: c_size_t): c_ptr(void);
      memset(s, c.safeCast(c_int), n);
      return s;
    }

  } // end POSIX

/* A type storing an error code or an error message.
   An :type:`errorCode` can be compared using == or != to a
   :type:`CTypes.c_int` or to another :type:`errorCode`. An :type:`errorCode`
   can be cast to or from a :type:`CTypes.c_int`. It can be assigned the
   value of a :type:`CTypes.c_int` or another :type:`errorCode`. In addition,
   :type:`errorCode` can be checked directly in an if statement like so:

   .. literalinclude:: ../../../../test/library/standard/OS/doc-examples/example_errorCode.chpl
    :language: chapel
    :start-after: START_EXAMPLE
    :end-before: STOP_EXAMPLE

   The default intent for a formal of type :type:`errorCode` is `const in`.

   The default value of the :type:`errorCode` type is undefined.
*/
  extern "syserr" type errorCode; // opaque so we can manually override ==,!=,etc

  // error numbers

  private extern proc qio_err_eq(a:errorCode, b:errorCode):c_int;
  private extern proc qio_err_to_int(a:errorCode):int(32);
  private extern proc qio_int_to_err(a:int(32)):errorCode;
  private extern proc qio_err_iserr(a:errorCode):c_int;

  @chpldoc.nodoc
  inline operator errorCode.==(a: errorCode, b: errorCode) {
    return (qio_err_eq(a,b) != 0:c_int);
  }
  @chpldoc.nodoc
  inline operator errorCode.==(a: errorCode, b: int(32)) do
    return (qio_err_to_int(a) == b:int(32));
  @chpldoc.nodoc
  inline operator errorCode.==(a: errorCode, b: int(64)) do
    return (qio_err_to_int(a) == b:int(32));
  @chpldoc.nodoc
  inline operator errorCode.==(a: int(32), b: errorCode) do
    return (a:int(32) == qio_err_to_int(b));
  @chpldoc.nodoc
  inline operator errorCode.==(a: int(64), b: errorCode) do
    return (a:int(32) == qio_err_to_int(b));
  @chpldoc.nodoc
  inline operator errorCode.!=(a: errorCode, b: errorCode) do return !(a == b);
  @chpldoc.nodoc
  inline operator errorCode.!=(a: errorCode, b: int(32)) do return !(a == b);
  @chpldoc.nodoc
  inline operator errorCode.!=(a: errorCode, b: int(64)) do return !(a == b);
  @chpldoc.nodoc
  inline operator errorCode.!=(a: int(32), b: errorCode) do return !(a == b);
  @chpldoc.nodoc
  inline operator errorCode.!=(a: int(64), b: errorCode) do return !(a == b);
  @chpldoc.nodoc
  inline operator errorCode.!(a: errorCode) do return (qio_err_iserr(a) == 0:c_int);
  inline proc errorCode.chpl_cond_test_method() do return (qio_err_iserr(this) != 0:c_int);
  @chpldoc.nodoc
  inline operator :(x: errorCode, type t: int(32)) do return qio_err_to_int(x);
  @chpldoc.nodoc
  inline operator :(x: errorCode, type t: int(64)) do return qio_err_to_int(x):int(64);
  @chpldoc.nodoc
  inline operator :(x: int(32), type t: errorCode) do return qio_int_to_err(x);
  @chpldoc.nodoc
  inline operator :(x: int(64), type t: errorCode) do return qio_int_to_err(x:int(32));
  @chpldoc.nodoc
  inline operator errorCode.=(ref ret:errorCode, x:errorCode) { __primitive("=", ret, x); }
  @chpldoc.nodoc
  inline operator errorCode.=(ref ret:errorCode, x:int(32))
  { __primitive("=", ret, qio_int_to_err(x)); }
  @chpldoc.nodoc
  inline operator errorCode.=(ref ret:errorCode, x:int(64))
  { __primitive("=", ret, qio_int_to_err(x:int(32))); }
  @chpldoc.nodoc
  inline operator errorCode.=(ref ret:c_int, x:errorCode)
  { __primitive("=", ret, qio_err_to_int(x):c_int); }


  private use CTypes;
  private use POSIX;

  /*
   Private local copies of IO.{EEOF,ESHORT,EFORMAT}, since if we import IO
   here it modifies module initialization order and causes issues in
   ChapelLocale.chpl_localeID_to_locale.
   */
  private extern proc chpl_macro_int_EEOF():c_int;
  private extern proc chpl_macro_int_ESHORT():c_int;
  private extern proc chpl_macro_int_EFORMAT():c_int;
  private inline proc EEOF do return chpl_macro_int_EEOF():c_int;
  private inline proc ESHORT do return chpl_macro_int_ESHORT():c_int;
  private inline proc EFORMAT do return chpl_macro_int_EFORMAT():c_int;

  /*
     :class:`SystemError` is a base class for :class:`Errors.Error` s
     generated from ``errorCode``. It provides factory methods to create
     different subtypes based on the ``errorCode`` that is passed.
  */
  class SystemError : Error {
    @chpldoc.nodoc
    var err:     errorCode;
    @chpldoc.nodoc
    var details: string;

    /*
      Construct a :class:`SystemError` with a specific error code and optional
      extra details.
    */
    proc init(err: errorCode, details: string = "") {
      this.err     = err;
      this.details = details;
    }

    /*
       Provides a formatted string output for :class:`SystemError`, generated
       from the internal ``err`` and the ``details`` string.
    */
    override proc message() {
      var strerror_err: c_int = 0;
      var errstr              = sys_strerror_syserr_str(err, strerror_err);
      var err_msg: string;
      try! {
        err_msg = string.createAdoptingBuffer(errstr);
      }

      if !details.isEmpty() then
        err_msg += " (" + details + ")";

      return err_msg;
    }
  }

  /*
    Gets the corresponding Chapel-specific IO error class for the given
    ``errorCode``, or simply the matching :class:`SystemError` subtype if this
    is not a Chapel-specific IO error.
  */
  pragma "insert line file info"
  pragma "always propagate line file info"
  @chpldoc.nodoc
  proc createSystemOrChplError(err: errorCode, details: string = ""): Error {
    // extract qioerr pointer error message, if present
    var strerror_err: c_int = 0;
    var errstr = sys_strerror_syserr_str(err, strerror_err);
    var err_msg: string;
    try! {
      err_msg = string.createAdoptingBuffer(errstr);
    }

    // return appropriate error
    select err {
      when EEOF do return new owned EofError(details, err_msg);
      when ESHORT do return new owned UnexpectedEofError(details, err_msg);
      when EFORMAT do return new owned BadFormatError(details, err_msg);
      otherwise do return createSystemError(err, details);
    }
  }

  /*
    Version of createSystemOrChplError accepting an integer error code.
  */
  pragma "insert line file info"
  pragma "always propagate line file info"
  @chpldoc.nodoc
  proc createSystemOrChplError(err: int, details: string = ""): Error {
    return createSystemOrChplError(err:errorCode, details);
  }

  /*
    Return the matching :class:`SystemError` subtype for a given ``errorCode``,
    with an optional string containing extra details.

    :arg err: the errorCode to generate from
    :arg details: extra information to include with the error
  */
  pragma "insert line file info"
  pragma "always propagate line file info"
  proc createSystemError(err: errorCode, details: string = ""): SystemError {
    if err == EAGAIN || err == EALREADY || err == EWOULDBLOCK || err == EINPROGRESS {
      return new owned BlockingIoError(details, err);
    } else if err == ECHILD {
      return new owned ChildProcessError(details, err);
    } else if err == EPIPE {
      return new owned BrokenPipeError(details, err);
    } else if err == ECONNABORTED {
      return new owned ConnectionAbortedError(details, err);
    } else if err == ECONNREFUSED {
      return new owned ConnectionRefusedError(details, err);
    } else if err == ECONNRESET {
      return new owned ConnectionResetError(details, err);
    } else if err == EEXIST {
      return new owned FileExistsError(details, err);
    } else if err == ENOENT {
      return new owned FileNotFoundError(details, err);
    } else if err == EINTR {
      return new owned InterruptedError(details, err);
    } else if err == EISDIR {
      return new owned IsADirectoryError(details, err);
    } else if err == ENOTDIR {
      return new owned NotADirectoryError(details, err);
    } else if err == EACCES || err == EPERM {
      return new owned PermissionError(details, err);
    } else if err == ESRCH {
      return new owned ProcessLookupError(details, err);
    } else if err == ETIMEDOUT {
      return new owned TimeoutError(details, err);
    } else if err == EIO {
      return new owned IoError(err, details);
    }

    return new owned SystemError(err, details);
  }

  /*
    Return the matching :class:`SystemError` subtype for a given error number,
    with an optional string containing extra details.

    :arg err: the number to generate from
    :arg details: extra information to include with the error
  */
  pragma "insert line file info"
  pragma "always propagate line file info"
  proc createSystemError(err: int, details: string = "") {
    return createSystemError(err:errorCode, details);
  }


  /*
     :class:`BlockingIoError` is the subclass of :class:`SystemError`
     corresponding to :const:`~POSIX.EAGAIN`, :const:`~POSIX.EALREADY`,
     :const:`~POSIX.EWOULDBLOCK`, and :const:`~POSIX.EINPROGRESS`.
  */
  class BlockingIoError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = EWOULDBLOCK:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`ChildProcessError` is the subclass of :class:`SystemError`
     corresponding to :const:`~POSIX.ECHILD`.
  */
  class ChildProcessError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = ECHILD:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`ConnectionError` is the subclass of :class:`SystemError` that
     serves as the base class for all system errors regarding connections.
  */
  class ConnectionError : SystemError {
    @chpldoc.nodoc
    proc init(err: errorCode, details: string = "") {
      super.init(err, details);
    }
  }

  /*
     :class:`BrokenPipeError` is the subclass of :class:`ConnectionError`
     corresponding to :const:`~POSIX.EPIPE`
  */
  class BrokenPipeError : ConnectionError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = EPIPE:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`ConnectionAbortedError` is the subclass of :class:`ConnectionError`
     corresponding to :const:`~POSIX.ECONNABORTED`.
  */
  class ConnectionAbortedError : ConnectionError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = ECONNABORTED:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`ConnectionRefusedError` is the subclass of :class:`ConnectionError`
     corresponding to :const:`~POSIX.ECONNREFUSED`.
  */
  class ConnectionRefusedError : ConnectionError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = ECONNREFUSED:errorCode) {
      super.init(err, details);
    }
}

  /*
     :class:`ConnectionResetError` is the subclass of :class:`ConnectionError`
     corresponding to :const:`~POSIX.ECONNRESET`.
  */
  class ConnectionResetError : ConnectionError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = ECONNRESET:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`FileExistsError` is the subclass of :class:`SystemError`
     corresponding to :const:`~POSIX.EEXIST`.
  */
  class FileExistsError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = EEXIST:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`FileNotFoundError` is the subclass of :class:`SystemError`
     corresponding to :const:`~POSIX.ENOENT`.
  */
  class FileNotFoundError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = ENOENT:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`InterruptedError` is the subclass of :class:`SystemError`
     corresponding to :const:`~POSIX.EINTR`.
  */
  class InterruptedError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = EINTR:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`IsADirectoryError` is the subclass of :class:`SystemError`
     corresponding to :const:`~POSIX.EISDIR`.
  */
  class IsADirectoryError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = EISDIR:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`NotADirectoryError` is the subclass of :class:`SystemError`
     corresponding to :const:`~POSIX.ENOTDIR`.
  */
  class NotADirectoryError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = ENOTDIR:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`PermissionError` is the subclass of :class:`SystemError`
     corresponding to :const:`~POSIX.EACCES` and :const:`~POSIX.EPERM`.
  */
  class PermissionError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = EPERM:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`ProcessLookupError` is the subclass of :class:`SystemError`
     corresponding to :const:`~POSIX.ESRCH`.
  */
  class ProcessLookupError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = ESRCH:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`TimeoutError` is the subclass of :class:`SystemError` corresponding
     to :const:`~POSIX.ETIMEDOUT`.
  */
  class TimeoutError : SystemError {
    @chpldoc.nodoc
    proc init(details: string = "", err: errorCode = ETIMEDOUT:errorCode) {
      super.init(err, details);
    }
  }

  /*
     :class:`IoError` is the subclass of :class:`SystemError` corresponding to
     EIO.
  */
  class IoError : SystemError {
    @chpldoc.nodoc
    proc init(err: errorCode = EIO, details: string = "") {
      super.init(err, details);
    }
  }

  /*
     :class:`EofError` is the the Chapel-specific error corresponding to
     encountering end-of-file.
  */
  class EofError : Error {
    @chpldoc.nodoc
    var details: string;

    @chpldoc.nodoc
    proc init(details: string = "", err_msg: string = "") {
      this.details = details;
      this._msg = err_msg;
    }

    @chpldoc.nodoc
    override proc message() {
      var generatedMsg: string;

      if !_msg.isEmpty() {
        generatedMsg += _msg;
      } else {
        // use default error message based on error code
        var err:errorCode = EEOF;
        var strerror_err_ignore: c_int = 0;
        var errstr = sys_strerror_syserr_str(err, strerror_err_ignore);
        var errorcode_msg: string;
        try! {
          errorcode_msg = string.createAdoptingBuffer(errstr);
        }
        generatedMsg += errorcode_msg;
      }

      // add details if present
      if !details.isEmpty() then
        generatedMsg += " (" + details + ")";

      return generatedMsg;
    }
  }

  /*
     :class:`UnexpectedEofError` is the Chapel-specific error
     corresponding to encountering end-of-file before the requested amount of
     input could be read.

     This error can also occur on some writing operations when a
     :record:`~IO.fileWriter`'s range has been specified, and the write exceeds
     the valid range.
  */
  class UnexpectedEofError : Error {
    @chpldoc.nodoc
    var details: string;

    @chpldoc.nodoc
    proc init(details: string = "", err_msg: string = "") {
      this.details = details;
      this._msg = err_msg;
    }

    @chpldoc.nodoc
    override proc message() {
      var generatedMsg: string;

      if !_msg.isEmpty() {
        generatedMsg += _msg;
      } else {
        // use default error message based on error code
        var err:errorCode = ESHORT;
        var strerror_err_ignore: c_int = 0;
        var errstr = sys_strerror_syserr_str(err, strerror_err_ignore);
        var errorcode_msg: string;
        try! {
          errorcode_msg = string.createAdoptingBuffer(errstr);
        }
        generatedMsg += errorcode_msg;
      }

      // add details if present
      if !details.isEmpty() then
        generatedMsg += " (" + details + ")";

      return generatedMsg;
    }
  }

  /*
     :class:`BadFormatError` is the Chapel-specific error corresponding
     to incorrectly-formatted input.
  */
  class BadFormatError : Error {
    @chpldoc.nodoc
    var details: string;

    @chpldoc.nodoc
    proc init(details: string = "", err_msg: string = "") {
      this.details = details;
      this._msg = err_msg;
    }

    @chpldoc.nodoc
    override proc message() {
      var generatedMsg: string;

      if !_msg.isEmpty() {
        generatedMsg += _msg;
      } else {
        // use default error message based on error code
        var err:errorCode = EFORMAT;
        var strerror_err_ignore: c_int = 0;
        var errstr = sys_strerror_syserr_str(err, strerror_err_ignore);
        var errorcode_msg: string;
        try! {
          errorcode_msg = string.createAdoptingBuffer(errstr);
        }
        generatedMsg += errorcode_msg;
      }

      // add details if present
      if !details.isEmpty() then
        generatedMsg += " (" + details + ")";

      return generatedMsg;
    }
  }

  /*
    :class:`InsufficientCapacityError` is a subclass of :class:`IoError`
    indicating that an IO operation required more storage than was provided
  */
  class InsufficientCapacityError : IoError {
    @chpldoc.nodoc
    proc init(details: string = "") {
      super.init(ERANGE: errorCode, details);
    }

    @chpldoc.nodoc
    override proc message() {
      return
        if details.isEmpty() then
          "Result too large"
        else
          details;
    }
  }

  // here's what we need from Sys
  private extern proc
  sys_strerror_syserr_str(error
                          : errorCode, out err_in_strerror
                          : c_int)
      : c_ptrConst(c_char);

  /* This function takes in a string and returns it in double-quotes,
     with internal double-quotes escaped with backslash.
     */
  private proc quote_string(s:string, len:c_ssize_t) {
    extern const QIO_STRING_FORMAT_CHPL: uint(8);
    extern proc qio_quote_string(s:uint(8), e:uint(8), f:uint(8),
                                 ptr:c_ptrConst(c_char), len:c_ssize_t,
                                 ref ret:c_ptrConst(c_char), ti: c_ptr(void)): errorCode;
    extern proc qio_strdup(s): c_ptrConst(c_char);

    var ret: c_ptrConst(c_char);
    // 34 is ASCII double quote
    var err: errorCode = qio_quote_string(34:uint(8), 34:uint(8),
                                      QIO_STRING_FORMAT_CHPL,
                                      s.localize().c_str(), len, ret, nil);
    // This doesn't handle the case where ret==NULL as did the previous
    // version in QIO, but I'm not sure how that was used.

    try! {
      if err then return string.createAdoptingBuffer(qio_strdup("<error>"));
      return string.createAdoptingBuffer(ret);
    }
}

  /* Create and throw an :class:`Errors.Error` if an error occurred, formatting
     a useful message based on the provided arguments. Do nothing if the error
     argument does not indicate an error occurred.

     :arg error: the error code
     :arg msg: extra information to include in the thrown error
     :arg path: optionally, a path to include in the thrown error
     :arg offset: optionally, an offset to include in the thrown error

     :throws Error: A subtype is thrown when the error argument
                          indicates an error occurred
 */
  pragma "insert line file info"
  pragma "always propagate line file info"
  @chpldoc.nodoc
  proc ioerror(error:errorCode, msg:string, path:string, offset:int(64)) throws
  {
    if error {
      const quotedpath = quote_string(path, path.numBytes:c_ssize_t);
      var   details    = msg + " with path " + quotedpath +
                         " offset " + offset:string;
      throw createSystemOrChplError(error, details);
    }
  }

  pragma "insert line file info"
  pragma "always propagate line file info"
  @chpldoc.nodoc // documented in the offset version
  proc ioerror(error:errorCode, msg:string, path:string) throws
  {
    if error {
      const quotedpath = quote_string(path, path.numBytes:c_ssize_t);
      var   details    = msg + " with path " + quotedpath;
      throw createSystemOrChplError(error, details);
    }
  }

  pragma "insert line file info"
  pragma "always propagate line file info"
  @chpldoc.nodoc // documented in the offset version
  proc ioerror(error:errorCode, msg:string) throws
  {
    if error then throw createSystemOrChplError(error, msg);
  }

  /* Create and throw an :class:`IoError` and include a formatted message
     based on the provided arguments.

     :arg errstr: the error string
     :arg msg: extra information to print after the error description
     :arg path: a path to print out that is related to the error
     :arg offset: an offset to print out that is related to the error

     :throws IoError: always throws an IoError
   */
  pragma "insert line file info"
  pragma "always propagate line file info"
  @chpldoc.nodoc
  proc ioerror(errstr:string, msg:string, path:string, offset:int(64)) throws
  {
    const quotedpath = quote_string(path, path.numBytes:c_ssize_t);
    const details    = errstr + " " + msg + " with path " + quotedpath +
                       " offset " + offset:string;
    throw createSystemOrChplError(EIO:errorCode, details);
  }

  /* Convert a errorCode code to a human-readable string describing the error.

     :arg error: the error code
     :returns: a string describing the error
   */
  proc errorToString(error:errorCode):string
  {
    var strerror_err:c_int = 0;
    const errstr = sys_strerror_syserr_str(error, strerror_err);
    try! {
      return string.createAdoptingBuffer(errstr);
    }
  }

}
