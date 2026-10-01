#pragma once

namespace Jde::IO{
	enum class EFileFlags : uint16{ None = 0x0, Hidden = 0x2, System = 0x4, Directory = 0x10, Archive = 0x20, Temporary = 0x100 };
	struct IDirEntry{
		IDirEntry()=default;
		IDirEntry( EFileFlags flags, const fs::path& path, uint size, const TimePoint& createTime=TimePoint(), const TimePoint& modifyTime=TimePoint() ):
			Flags{ flags },
			Path{ path },
			Size{ size },
			CreatedTime{ createTime },
			ModifiedTime{ modifyTime }
		{}
		virtual ~IDirEntry()
		{}

		bool IsDirectory()Ι{return !empty(Flags & EFileFlags::Directory);}
		EFileFlags Flags{EFileFlags::None};
		fs::path Path;
		uint Size{0};
		TimePoint AccessedTime;
		TimePoint CreatedTime;
		TimePoint ModifiedTime;
	};
	struct IDrive : std::enable_shared_from_this<IDrive>{
		β Recursive( const fs::path& path, SRCE )ε->flat_map<string,up<IDirEntry>> =0;
		β Get( const fs::path& path )ε->up<IDirEntry> =0;
		β Save( const fs::path& path, const vector<char>& bytes, const IDirEntry& dirEntry )ε->up<IDirEntry> =0;
		β CreateFolder( const fs::path& path, const IDirEntry& dirEntry )->up<IDirEntry> =0;
		β Remove( const fs::path& path )->void=0;
		β Trash( const fs::path& path )->void=0;
		β TrashDisposal( TimePoint latestDate )->void=0;
		β Load( const IDirEntry& dirEntry )->vector<char> =0;
		β Restore( sv name )ε->void=0;
		β SoftLink( const fs::path& existingFile, const fs::path& newSymLink )ε->void=0;
	};
}
extern "C"{
	Jde::IO::IDrive* LoadDrive();
}