#include <jde/fwk/exceptions/IOException.h>

namespace Jde{
	α IO::ToIOError( uint32 nativeCode )ι->EIOError{
		switch( nativeCode ){
#ifdef _MSC_VER
		case ERROR_FILE_NOT_FOUND: case ERROR_PATH_NOT_FOUND: return EIOError::NotFound;
		case ERROR_FILE_EXISTS: case ERROR_ALREADY_EXISTS: return EIOError::Exists;
		case ERROR_ACCESS_DENIED: case ERROR_WRITE_PROTECT: return EIOError::Permission;
		case ERROR_SHARING_VIOLATION: case ERROR_LOCK_VIOLATION: case ERROR_BUSY: return EIOError::Busy;
		case ERROR_DISK_FULL: case ERROR_HANDLE_DISK_FULL: return EIOError::NoSpace;
		case ERROR_INVALID_PARAMETER: case ERROR_INVALID_NAME: case ERROR_DIRECTORY: case ERROR_FILENAME_EXCED_RANGE: return EIOError::Invalid;
		case ERROR_READ_FAULT: case ERROR_WRITE_FAULT: case ERROR_CRC: case ERROR_IO_DEVICE: return EIOError::Device;
#else
		case ENOENT: return EIOError::NotFound;
		case EEXIST: return EIOError::Exists;
		case EACCES: case EPERM: case EROFS: return EIOError::Permission;
		case EBUSY: case EAGAIN: case ETXTBSY: case ENOLCK: return EIOError::Busy;
		case ENOSPC: case EDQUOT: return EIOError::NoSpace;
		case EINVAL: case EISDIR: case ENOTDIR: case ENAMETOOLONG: return EIOError::Invalid;
		case EIO: return EIOError::Device;
#endif
		default: return EIOError::None;
		}
	}
}
