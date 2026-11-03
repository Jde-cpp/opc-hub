#include "WindowsDrive.h"
#include <jde/fwk/chrono.h>
#include <jde/fwk/io/file.h>
#include "WindowsUtilities.h"
#define let const auto

namespace Jde{
	IO::WindowsDrive _native;
	constexpr ELogTags _tags{ ELogTags::IO };

	//Ω IO::Drive()ι->IDrive&{ return _native; }
}

namespace Jde::IO{
	α WindowsPath( const fs::path& path )->std::wstring{
		return std::wstring(L"\\\\?\\")+path.wstring();
	}
	α GetInfo( const fs::path& path )ε->WIN32_FILE_ATTRIBUTE_DATA{
		WIN32_FILE_ATTRIBUTE_DATA fInfo;
		THROW_IFX( !GetFileAttributesExW(WindowsPath(path).c_str(), GetFileExInfoStandard, &fInfo), IOException(path, GetLastError(), "Could not get file attributes.") );
		return fInfo;
	}

	struct DirEntry : IDirEntry{
		DirEntry( const fs::path& path ):
			DirEntry( path, GetInfo(path) )
		{}
		DirEntry( const fs::path& path, const WIN32_FILE_ATTRIBUTE_DATA& fInfo ):
			IDirEntry{ (EFileFlags)fInfo.dwFileAttributes, path, static_cast<size_t>(fInfo.nFileSizeHigh) <<32 | fInfo.nFileSizeLow }
		{
			SYSTEMTIME systemTime;
			FileTimeToSystemTime( &fInfo.ftCreationTime, &systemTime );
			CreatedTime = Chrono::ToTimePoint( systemTime.wYear, (uint8)systemTime.wMonth, (uint8)systemTime.wDay, (uint8)systemTime.wHour, (uint8)systemTime.wMinute, (uint8)systemTime.wSecond, std::chrono::milliseconds(systemTime.wMilliseconds) );

			FileTimeToSystemTime( &fInfo.ftLastWriteTime, &systemTime );
			ModifiedTime = Chrono::ToTimePoint( systemTime.wYear, (uint8)systemTime.wMonth, (uint8)systemTime.wDay, (uint8)systemTime.wHour, (uint8)systemTime.wMinute, (uint8)systemTime.wSecond, std::chrono::milliseconds(systemTime.wMilliseconds) );
		}
	};

	α WindowsDrive::Recursive( const fs::path& dir, SL )ε->flat_map<string,up<IDirEntry>>{
		CHECK_PATH( dir, SRCE_CUR );
		let dirString = dir.string();
		flat_map<string,up<IDirEntry>> entries;

		std::function<void(const fs::directory_entry&)> fnctn;
		fnctn = [&dirString, &entries, &fnctn]( const fs::directory_entry& entry )
		{
			let status = entry.status();
			let relativeDir = entry.path().string().substr( dirString.size()+1 );

			sp<DirEntry> pEntry;
			if( fs::is_directory(status) || fs::is_regular_file(status) ){
				let fInfo = GetInfo( entry.path() );
				entries.emplace( relativeDir, mu<DirEntry>(entry.path(), fInfo) );
				if( fs::is_directory(status) ){
					for( let& dirEntry : fs::directory_iterator(entry.path()) )
						fnctn( dirEntry );
				}
			}
		};
		for( let& dirEntry : fs::directory_iterator(dir) )
			fnctn( dirEntry );

		return entries;
	}

	α WindowsDrive::Get( const fs::path& path )ε->up<IDirEntry>{
		return mu<DirEntry>( path );
	}

	α GetTimes( const IDirEntry& dirEntry )->tuple<FILETIME,FILETIME,FILETIME>{
		FILETIME createTime, modifiedTime;
		let entryCreateTime = Windows::ToSystemTime( dirEntry.CreatedTime );
		SystemTimeToFileTime( &entryCreateTime, &createTime );
		if( dirEntry.ModifiedTime.time_since_epoch()!=Duration::zero() )
		{
			let entryModifiedTime = Windows::ToSystemTime( dirEntry.ModifiedTime );
			SystemTimeToFileTime( &entryModifiedTime, &modifiedTime );
		}
		else
			modifiedTime = createTime;
		return std::make_tuple( createTime, modifiedTime, modifiedTime );
	}

	α WindowsDrive::CreateFolder( const fs::path& dir, const IDirEntry& dirEntry )ε->up<IDirEntry>{
		THROW_IFX( !CreateDirectory(dir.string().c_str(), nullptr), IOException(dir, GetLastError(), "Could not create.") );
		if( dirEntry.CreatedTime.time_since_epoch()!=Duration::zero() )
		{
			let [createTime, modifiedTime, lastAccessedTime] = GetTimes( dirEntry );
			auto hFile = CreateFileW( WindowsPath(dir).c_str(), GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_BACKUP_SEMANTICS, nullptr );  THROW_IFX( hFile==INVALID_HANDLE_VALUE, IOException(dir, GetLastError(), "Could not create.") );
			if( !SetFileTime(hFile, &createTime, &lastAccessedTime, &modifiedTime) )
				WARN( "Could not update dir times '{}' - {}.", dir.string(), GetLastError() );
			CloseHandle( hFile );
		}
		return mu<DirEntry>( dir );
	}
	α WindowsDrive::Save( const fs::path& path, const vector<char>& bytes, const IDirEntry& dirEntry )ε->up<IDirEntry>{
		IO::SaveBinary<const char>( path, bytes );
		if( dirEntry.CreatedTime.time_since_epoch()!=Duration::zero() ){
			let [createTime, modifiedTime, lastAccessedTime] = GetTimes( dirEntry );
			auto hFile = ::CreateFileW( WindowsPath(path).c_str(), GENERIC_READ | GENERIC_WRITE, FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL|FILE_FLAG_BACKUP_SEMANTICS, nullptr );
			THROW_IFX( hFile==INVALID_HANDLE_VALUE, IOException(path, GetLastError(), "Could not create.") );
			if( !SetFileTime(hFile, &createTime, &lastAccessedTime, &modifiedTime) )
				WARN( "Could not update file times '{}' - {}."sv, path.string(), GetLastError() );
			CloseHandle( hFile );
		}
		return mu<DirEntry>( path );
	}

	α WindowsDrive::Load( const IDirEntry& dirEntry )ε->vector<char>{
		return IO::LoadBinary( dirEntry.Path );
	}

	void WindowsDrive::SoftLink( const fs::path& from, const fs::path& to )ε{
		let hr = CreateSymbolicLinkW( ((const std::wstring&)to).c_str(), ((const std::wstring&)from).c_str(), 0 );
		THROW_IFX( !hr, IOException( from, GetLastError(), Ƒ("Creating symbolic link from to '{}'", to.string().c_str())) );
	}

}